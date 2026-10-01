module;
#include <common.hxx>
#include <ddraw.h>
#include <atomic>
#include <bit>
#include "ClassicPresentation.hxx"

export module ClassicMenu;
import ClassicGame;
import ClassicInput;
import Localization;

export namespace ClassicMenu
{
    bool opened = false, ready = false;
    std::atomic<bool> request = false;
    HWND window = nullptr;
    bool confirmation = false, yes = false;
    int selection = 0, scroll = 0;
    int maxSlot = 30;
    uint32_t previousInput = 0;
    uint64_t repeatAt = 0;
    IDirectDrawSurface* snapshot = nullptr;
    IDirectDrawSurface* menuSurface = nullptr;
    IDirectDraw* lastDraw = nullptr;
    IDirectDrawSurface* lastSurface = nullptr;
    std::function<bool()> canLoad;
    std::function<void(int)> load;
    std::function<int()> availableSlots;
    std::vector<std::function<void(bool)>> pause;
    constexpr std::array<const char*, 11> rows = { "Resume", "Widescreen", "Alternate controls", "Keyboard movement",
        "Skip intro", "Skip doors", "Fast load", "Auto load", "Load slot", "Load game", "Exit game" };

    bool Capture(IDirectDraw* draw, IDirectDrawSurface* surface)
    {
        DDSURFACEDESC description{ .dwSize = sizeof(DDSURFACEDESC) };
        if (!draw || !surface || FAILED(surface->GetSurfaceDesc(&description))) return false;
        description.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        description.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        if (FAILED(draw->CreateSurface(&description, &snapshot, nullptr))) return false;
        if (SUCCEEDED(snapshot->Blt(nullptr, surface, nullptr, DDBLT_WAIT, nullptr))) return true;
        snapshot->Release(); snapshot = nullptr;
        return false;
    }

    void Close()
    {
        const bool wasOpen = opened;
        opened = confirmation = false;
        if (wasOpen) for (const auto& callback : pause) callback(false);
        if (menuSurface) { menuSurface->Release(); menuSurface = nullptr; }
        if (snapshot) { snapshot->Release(); snapshot = nullptr; }
    }

    void Change(int delta)
    {
        auto& settings = ClassicGame::GetSettings();
        if (selection == 3) settings.keyboardRunMode = (settings.keyboardRunMode.load() + delta + 4) % 4;
        else if (selection == 8) settings.loadSlot = (std::clamp(settings.loadSlot.load(), 0, maxSlot) + delta + maxSlot + 1) % (maxSlot + 1);
        else if (selection >= 1 && selection <= 7)
            ClassicGame::Toggle(ClassicGame::Option(selection - 1 - int(selection > 3)));
        else return;
        ClassicGame::SaveSettings();
    }

    void Activate()
    {
        if (confirmation)
        {
            if (yes) { Close(); if (window) PostMessageW(window, WM_CLOSE, 0, 0); }
            else confirmation = false;
        }
        else if (!selection) Close();
        else if (selection == 10) { confirmation = true; yes = false; }
        else if (selection == 9)
        {
            if (canLoad && canLoad() && load) { load(ClassicGame::GetSettings().loadSlot); Close(); }
        }
        else Change(1);
    }

    void Navigate(uint32_t pressed)
    {
        if (confirmation) { if (pressed & 15) yes = !yes; }
        else
        {
            if (pressed & 1) selection = (selection + int(rows.size()) - 1) % int(rows.size());
            if (pressed & 2) selection = (selection + 1) % int(rows.size());
            if (pressed & 4) Change(-1);
            if (pressed & 8) Change(1);
            scroll = std::clamp(scroll, std::max(0, selection - 9), selection);
        }
        if (pressed & 16) Activate();
        if (pressed & 32) { if (confirmation) confirmation = false; else Close(); }
    }

    bool Message(HWND target, UINT message, WPARAM key, LPARAM flags)
    {
        window = target;
        if (message == WM_CLOSE || message == WM_DESTROY || message == WM_DISPLAYCHANGE)
        {
            Close(); lastDraw = nullptr; lastSurface = nullptr; ready = false;
            return false;
        }
        if (!ready) return false;
        if (message == WM_KEYDOWN && !(flags & (1L << 30)))
        {
            if (key == VK_ESCAPE && (opened || !ClassicInput::InNativeMenu())) { request = true; return true; }
            if (opened)
            {
                switch (key)
                {
                    case VK_UP: Navigate(1); break;
                    case VK_DOWN: Navigate(2); break;
                    case VK_LEFT: Navigate(4); break;
                    case VK_RIGHT: Navigate(8); break;
                    case VK_RETURN: Navigate(16); break;
                }
                return true;
            }
        }
        return opened && (message == WM_KEYDOWN || message == WM_KEYUP);
    }

    bool Update()
    {
        ClassicInput::Update();
        const auto& pad = ClassicInput::pad;
        const bool toggle = request.exchange(false)
            || (ClassicInput::escapePressed && (opened || !ClassicInput::InNativeMenu())) || (pad.pressedButtons & 1);
        if (toggle && ready)
        {
            if (opened) { if (confirmation) confirmation = false; else Close(); }
            else
            {
                if (!Capture(lastDraw, lastSurface)) return false;
                if (availableSlots) maxSlot = std::clamp(availableSlots(), 0, 65535);
                opened = true; confirmation = false; selection = scroll = 0;
                for (const auto& callback : pause) callback(true);
            }
        }
        const uint32_t input = uint32_t((pad.dpad & 1) || pad.left.y > 0.5f)
            | uint32_t((pad.dpad & 4) || pad.left.y < -0.5f) << 1
            | uint32_t((pad.dpad & 8) || pad.left.x < -0.5f) << 2
            | uint32_t((pad.dpad & 2) || pad.left.x > 0.5f) << 3
            | uint32_t(pad.buttons & 2) << 3 | uint32_t(pad.buttons & 4) << 3;
        if (opened && !toggle)
        {
            const auto now = GetTickCount64();
            auto pressed = input & ~previousInput;
            if ((input & 15) && input == previousInput && now >= repeatAt) { pressed |= input & 15; repeatAt = now + 100; }
            else if (pressed & 15) repeatAt = now + 400;
            Navigate(pressed);
        }
        previousInput = input;
        return opened;
    }

    bool Draw(IDirectDraw* draw, IDirectDrawSurface* surface)
    {
        if (!draw || !surface) return false;
        lastDraw = draw; lastSurface = surface;
        ready = true;
        if (!opened) return false;
        if (!snapshot)
        {
            if (!Capture(draw, surface)) { Close(); return false; }
        }
        if (FAILED(surface->Blt(nullptr, snapshot, nullptr, DDBLT_WAIT, nullptr))) { Close(); return false; }
        DDSURFACEDESC description{ .dwSize = sizeof(DDSURFACEDESC) };
        if (FAILED(surface->GetSurfaceDesc(&description))) { Close(); return false; }
        const auto destination = Presentation::Viewport::Fit(float(description.dwWidth), float(description.dwHeight), 4.0f / 3.0f);
        if (!menuSurface)
        {
            description.dwWidth = 320; description.dwHeight = 240;
            description.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
            description.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
            if (FAILED(draw->CreateSurface(&description, &menuSurface, nullptr))) { Close(); return false; }
        }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 320; info.bmiHeader.biHeight = -240;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        void* pixels = nullptr;
        const auto dc = CreateCompatibleDC(nullptr);
        const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!dc || !bitmap) { if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); Close(); return false; }
        const auto oldBitmap = SelectObject(dc, bitmap);
        const auto viewport = Presentation::Viewport::Fit(320.0f, 240.0f, 4.0f / 3.0f);
        const float scale = 1.0f;
        const auto panel = [&](float x, float y, float w, float h, COLORREF color)
        {
            RECT rect{ LONG(viewport.x + x * scale), LONG(viewport.y + y * scale),
                LONG(viewport.x + (x + w) * scale), LONG(viewport.y + (y + h) * scale) };
            auto brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush);
        };
        const auto font = CreateFontW(-int(12 * scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH,
            Localization::language == "japanese" ? L"MS Gothic" : L"Times New Roman");
        const auto oldFont = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        const auto text = [&](float x, float y, const std::wstring& label, COLORREF color, float available = 264.0f)
        {
            SetTextColor(dc, color);
            SIZE size{};
            GetTextExtentPoint32W(dc, label.c_str(), int(label.size()), &size);
            const float factor = size.cx > available ? available / size.cx : 1.0f;
            SetGraphicsMode(dc, GM_ADVANCED);
            XFORM transform{ factor, 0.0f, 0.0f, 1.0f, x * (1.0f - factor), 0.0f };
            SetWorldTransform(dc, &transform);
            TextOutW(dc, int(viewport.x + x * scale), int(viewport.y + y * scale), label.c_str(), int(label.size()));
            ModifyWorldTransform(dc, nullptr, MWT_IDENTITY);
        };
        panel(17, 17, 286, 206, RGB(119, 119, 119));
        panel(18, 18, 284, 204, RGB(8, 8, 8));
        text(29, 25, Localization::Text("FUSION FIX OPTIONS"), RGB(224, 224, 224));
        panel(27, 43, 266, 1, RGB(85, 85, 85));
        if (confirmation)
        {
            text(33, 83, Localization::Text("Quit the game?"), RGB(255, 255, 255));
            text(65, 122, (yes ? L"> " : L"  ") + Localization::Text("Yes"), yes ? RGB(232, 188, 104) : RGB(170, 170, 170));
            text(173, 122, (!yes ? L"> " : L"  ") + Localization::Text("No"), !yes ? RGB(232, 188, 104) : RGB(170, 170, 170));
        }
        else
        {
            for (int row = scroll; row < std::min(scroll + 10, int(rows.size())); ++row)
            {
                std::wstring value;
                if (row == 3)
                {
                    constexpr const char* modes[] = { "Hold: run", "Toggle: run", "Hold: walk", "Toggle: walk" };
                    value = Localization::Text(modes[ClassicGame::GetSettings().keyboardRunMode]);
                }
                else if (row == 8)
                {
                    const int slot = ClassicGame::GetSettings().loadSlot;
                    value = slot ? Localization::Text("Slot") + L" " + std::to_wstring(slot) : Localization::Text("Latest");
                }
                else if (row >= 1 && row <= 7)
                    value = Localization::Text(ClassicGame::Enabled(ClassicGame::Option(row - 1 - int(row > 3))) ? "On" : "Off");
                const auto color = row == selection ? RGB(232, 188, 104)
                    : row == 9 && (!canLoad || !canLoad()) ? RGB(85, 85, 85) : RGB(204, 204, 204);
                const float y = 52.0f + (row - scroll) * 14.5f;
                text(26, y, row == selection ? L">" : L"", color);
                text(37, y, Localization::Text(rows[row]), color, value.empty() ? 254.0f : 157.0f);
                if (!value.empty()) text(203, y, value, color, 88.0f);
            }
            text(28, 205, Localization::Text("Navigate / Change / Confirm / Back"), RGB(136, 136, 136));
        }
        SelectObject(dc, oldFont); DeleteObject(font);
        GdiFlush();
        bool drawn = false;
        DDSURFACEDESC locked{ .dwSize = sizeof(DDSURFACEDESC) };
        if (SUCCEEDED(menuSurface->Lock(nullptr, &locked, DDLOCK_WAIT | DDLOCK_WRITEONLY, nullptr)))
        {
            const auto color = [](uint32_t value, DWORD mask) -> DWORD
            {
                if (!mask) return 0u;
                const auto shift = std::countr_zero(mask);
                return ((value * (mask >> shift) + 127) / 255 << shift) & mask;
            };
            const auto* source = static_cast<const uint32_t*>(pixels);
            const auto bytes = locked.ddpfPixelFormat.dwRGBBitCount / 8;
            if (locked.lpSurface && (locked.ddpfPixelFormat.dwFlags & DDPF_RGB) && (bytes == 2 || bytes == 3 || bytes == 4))
            {
                for (int y = 17; y < 223; ++y)
                    for (int x = 17; x < 303; ++x)
                    {
                        const auto pixel = source[y * 320 + x];
                        const auto converted = color((pixel >> 16) & 255, locked.ddpfPixelFormat.dwRBitMask)
                            | color((pixel >> 8) & 255, locked.ddpfPixelFormat.dwGBitMask)
                            | color(pixel & 255, locked.ddpfPixelFormat.dwBBitMask);
                        memcpy(static_cast<uint8_t*>(locked.lpSurface) + y * locked.lPitch + x * bytes, &converted, bytes);
                    }
                drawn = true;
            }
            drawn = SUCCEEDED(menuSurface->Unlock(nullptr)) && drawn;
            if (drawn)
            {
                RECT from{ 17, 17, 303, 223 };
                const float factor = destination.width / 320.0f;
                RECT to{ LONG(destination.x + 17 * factor), LONG(destination.y + 17 * factor),
                    LONG(destination.x + 303 * factor), LONG(destination.y + 223 * factor) };
                drawn = SUCCEEDED(surface->Blt(&to, menuSurface, &from, DDBLT_WAIT, nullptr));
            }
        }
        SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc);
        if (!drawn) Close();
        return drawn;
    }
}

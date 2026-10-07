module;
#include <common.hxx>
#include <ddraw.h>
#include <atomic>
#include <bit>
#include "ClassicPresentation.hxx"
#include "NativeMenu.hxx"
#include "NativeFont.hxx"
#include "MouseInput.hxx"

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
    int style = 1;
    NativeMenu::State menu;
    int maxSlot = 30;
    uint32_t previousInput = 0;
    uint64_t repeatAt = 0;
    IDirectDrawSurface* snapshot = nullptr;
    NativeFont::Font font;
    IDirectDraw* lastDraw = nullptr;
    IDirectDrawSurface* lastSurface = nullptr;
    std::function<bool()> canLoad;
    std::function<void(int)> load;
    std::function<int()> availableSlots;
    std::vector<std::function<void(bool)>> pause;

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
        MouseInput::Reset();
        if (wasOpen) for (const auto& callback : pause) callback(false);
        if (snapshot) { snapshot->Release(); snapshot = nullptr; }
    }

    int SelectedRow()
    {
        switch (menu.Selected(style))
        {
        case NativeMenu::Resume: return 0;
        case NativeMenu::Widescreen: return 1;
        case NativeMenu::Controls: return 2;
        case NativeMenu::DefaultPace: return 3;
        case NativeMenu::SkipIntro: return 4;
        case NativeMenu::SkipDoors: return 5;
        case NativeMenu::FastLoad: return 6;
        case NativeMenu::AutoLoad: return 7;
        case NativeMenu::LoadSlot: return 8;
        case NativeMenu::Load: return 9;
        case NativeMenu::Quit: return 10;
        case NativeMenu::AutoPush: return 11;
        case NativeMenu::Bindings: return 12;
        case NativeMenu::AspectLimit: return 13;
        case NativeMenu::Portable: return 14;
        case NativeMenu::MouseSteering: return 15;
        case NativeMenu::MouseSensitivity: return 16;
        case NativeMenu::ShiftBehavior: return 17;
        default: return -1;
        }
    }

    void Back()
    {
        if (confirmation) confirmation = false;
        else if (!menu.Return()) Close();
    }

    void Change(int delta)
    {
        const int selection = SelectedRow();
        auto& settings = ClassicGame::GetSettings();
        if (selection == 15)
        {
            auto& enabled = ClassicGame::GetSettings().mouseSteering;
            enabled = !enabled.load();
            MouseInput::Reset();
        }
        else if (selection == 17)
        {
            auto& toggle = ClassicGame::GetSettings().shiftToggle;
            toggle = !toggle.load();
        }
        else if (selection == 16)
        {
            auto& sensitivity = ClassicGame::GetSettings().mouseSensitivity;
            sensitivity = std::clamp(sensitivity.load() + delta * 0.25f, 0.25f, 4.0f);
            MouseInput::Reset();
        }
        else if (selection == 12)
        {
            auto& bindings = ClassicGame::GetSettings().hdControls;
            bindings = !bindings.load();
        }
        else if (selection == 13)
        {
            auto& aspect = ClassicGame::GetSettings().maxAspectRatio;
            aspect = NativeMenu::ChangeAspect(aspect.load(), delta);
            ClassicGame::onSettingsChanged().executeAll();
        }
        else if (selection == 14) ClassicGame::GetSettings().portableMode = !ClassicGame::GetSettings().portableMode;
        else if (selection == 11) ClassicGame::Toggle(ClassicGame::Option::AutoPush);
        else if (selection == 3) settings.defaultRun = !settings.defaultRun.load();
        else if (selection == 8) settings.loadSlot = (std::clamp(settings.loadSlot.load(), 0, maxSlot) + delta + maxSlot + 1) % (maxSlot + 1);
        else if (selection >= 1 && selection <= 7)
            ClassicGame::Toggle(ClassicGame::Option(selection - 1 - int(selection > 3)));
        else return;
        ClassicGame::SaveSettings();
    }

    void Activate()
    {
        if (!confirmation && menu.Enter(menu.Selected(style))) return;
        const int selection = SelectedRow();
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
            if (pressed & 1) menu.Move(-1, style);
            if (pressed & 2) menu.Move(1, style);
            if (pressed & 4) Change(-1);
            if (pressed & 8) Change(1);
        }
        if (pressed & 16) Activate();
        if (pressed & 32) Back();
    }

    bool Message(HWND target, UINT message, WPARAM key, LPARAM flags)
    {
        window = target;
        MouseInput::WheelMessage(message, key, ClassicGame::Enabled(ClassicGame::Option::PanAndScan)
            && !opened && !ClassicInput::InNativeMenu());
        MouseInput::Message(target, message, flags, ClassicGame::GetSettings().mouseSteering, opened);
        if (message == WM_KEYDOWN) menu.controller = false;
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
        if (opened || ClassicInput::gameInputSuppressed || (ClassicInput::nativeMenu && ClassicInput::nativeMenu())) MouseInput::Reset();
        ClassicInput::Update();
        const auto& pad = ClassicInput::pad;
        const bool toggle = request.exchange(false)
            || (ClassicInput::escapePressed && (opened || !ClassicInput::InNativeMenu())) || (pad.pressedButtons & 1);
        if (pad.pressedButtons || pad.pressedDpad) { menu.controller = true; menu.xinput = pad.xinput; }
        if (toggle && ready)
        {
            if (opened) { if (pad.pressedButtons & 1) Close(); else Back(); }
            else
            {
                if (!Capture(lastDraw, lastSurface)) return false;
                if (availableSlots) maxSlot = std::clamp(availableSlots(), 0, 65535);
                MouseInput::Reset();
                opened = true; confirmation = false; menu.Reset();
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
            if (pressed) { menu.controller = true; menu.xinput = pad.xinput; }
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
        if (!font.Ready() && !font.Files(style, Localization::language)) { Close(); return false; }
        DDSURFACEDESC locked{ .dwSize = sizeof(DDSURFACEDESC) };
        if (FAILED(surface->Lock(nullptr, &locked, DDLOCK_WAIT, nullptr))) { Close(); return false; }
        const auto bytes = locked.ddpfPixelFormat.dwRGBBitCount / 8;
        if (!locked.lpSurface || !(locked.ddpfPixelFormat.dwFlags & DDPF_RGB) || (bytes != 2 && bytes != 3 && bytes != 4))
        { surface->Unlock(nullptr); Close(); return false; }
        const auto pack = [](uint32_t value, DWORD mask) -> DWORD
        {
            if (!mask) return 0u;
            const auto shift = std::countr_zero(mask);
            return ((value * (mask >> shift) + 127) / 255 << shift) & mask;
        };
        const auto unpack = [](DWORD value, DWORD mask) -> DWORD
        {
            if (!mask) return 0u;
            const auto shift = std::countr_zero(mask);
            return ((value & mask) >> shift) * 255 / (mask >> shift);
        };
        const auto pixel = [&](int x, int y, uint32_t rgba)
        {
            if (x < 0 || y < 0 || x >= int(locked.dwWidth) || y >= int(locked.dwHeight)) return;
            auto* target = static_cast<uint8_t*>(locked.lpSurface) + y * locked.lPitch + x * bytes;
            DWORD original = 0; memcpy(&original, target, bytes);
            const auto alpha = rgba >> 24;
            const auto blend = [&](int shift, DWORD mask)
            { return pack((((rgba >> shift) & 255) * alpha + unpack(original, mask) * (255 - alpha) + 127) / 255, mask); };
            const auto& f = locked.ddpfPixelFormat;
            const DWORD result = blend(16, f.dwRBitMask) | blend(8, f.dwGBitMask) | blend(0, f.dwBBitMask);
            memcpy(target, &result, bytes);
        };
        // Fullscreen translucent black quad, composited directly at output
        // resolution. No scaled surface blits or wrapper texture filtering.
        for (DWORD y = 0; y < locked.dwHeight; ++y) for (DWORD x = 0; x < locked.dwWidth; ++x) pixel(x, y, 0xB0000000);
        const auto nativePanel = [](float, float, float, float, uint32_t) {};
        const auto nativeText = [&](float x, float y, const std::wstring& label, uint32_t color, float available, bool compact)
        {
            const auto image = font.Render(label, color == 0xFF00FF00);
            if (!image.width || !image.height) return;
            const float scale = destination.width / 320.0f * std::min(compact ? 0.65f : 1.0f, available / image.width);
            const int left = int(destination.x + x * destination.width / 320.0f);
            const int top = int(destination.y + y * destination.height / 240.0f);
            const int width = std::max(1, int(image.width * scale)), height = std::max(1, int(image.height * scale));
            for (int dy = 0; dy < height; ++dy) for (int dx = 0; dx < width; ++dx)
            {
                auto c = image.pixels[std::min(image.height - 1, int(dy / scale)) * image.width + std::min(image.width - 1, int(dx / scale))];
                if (!(c >> 24)) continue;
                if (color != 0xFF00FF00 && color != 0xFFFFFFFF)
                    c = (c & 0xFF000000) | ((((c >> 16) & 255) * ((color >> 16) & 255) / 255) << 16)
                        | ((((c >> 8) & 255) * ((color >> 8) & 255) / 255) << 8) | ((c & 255) * (color & 255) / 255);
                pixel(left + dx, top + dy, c);
            }
        };
        const auto value = [&](NativeMenu::Action action) -> std::wstring
        {
            if (action == NativeMenu::MouseSteering)
                return Localization::Text(ClassicGame::GetSettings().mouseSteering ? "On" : "Off");
            if (action == NativeMenu::MouseSensitivity)
            {
                auto number = std::to_wstring(ClassicGame::GetSettings().mouseSensitivity.load());
                number.resize(number.find(L'.') + 3);
                return number;
            }
            if (action == NativeMenu::Bindings)
                return Localization::Text(ClassicGame::GetSettings().hdControls ? "Remaster" : "Original");
            if (action == NativeMenu::AspectLimit)
                return NativeMenu::AspectName(ClassicGame::GetSettings().maxAspectRatio.load());
            if (action == NativeMenu::Portable)
                return Localization::Text(ClassicGame::GetSettings().portableMode ? "On" : "Off");
            if (action == NativeMenu::Controls)
                return Localization::Text(ClassicGame::Enabled(ClassicGame::Option::AlternateControls) ? "Alternate" : "Original");
            if (action == NativeMenu::DefaultPace)
                return Localization::Text(ClassicGame::GetSettings().defaultRun.load() ? "Run" : "Walk");
            if (action == NativeMenu::ShiftBehavior)
                return Localization::Text(ClassicGame::GetSettings().shiftToggle.load() ? "Toggle" : "Hold");
            if (action == NativeMenu::LoadSlot)
            {
                const int slot = ClassicGame::GetSettings().loadSlot;
                return slot ? Localization::Text("Slot") + L" " + std::to_wstring(slot) : Localization::Text("Latest");
            }
            int option = -1;
            switch (action)
            {
            case NativeMenu::Widescreen: option = 0; break;
            case NativeMenu::SkipIntro: option = 2; break;
            case NativeMenu::SkipDoors: option = 3; break;
            case NativeMenu::FastLoad: option = 4; break;
            case NativeMenu::AutoLoad: option = 5; break;
            case NativeMenu::AutoPush: option = 6; break;
            }
            return option >= 0 ? Localization::Text(ClassicGame::Enabled(ClassicGame::Option(option)) ? "On" : "Off") : L"";
        };
        NativeMenu::Draw(style, menu, confirmation, yes, canLoad && canLoad(), nativePanel, nativeText, Localization::Text, value);
        const bool drawn = SUCCEEDED(surface->Unlock(nullptr));
        if (!drawn) Close();
        return drawn;
    }
}

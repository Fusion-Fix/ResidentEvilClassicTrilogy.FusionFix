module;
#include <common.hxx>
#include <safetyhook.hpp>
#include <ddraw.h>
#include <d3d.h>
#include <atomic>
#include <bit>
#include <unordered_map>
#include <vector>
#include "Presentation.hxx"

export module Menu;
import common;
import Game;
import Input;
import Geometry;
import Localization;
import Saves;

export namespace Menu
{
    std::atomic<bool> opened = false;
    std::atomic<bool> rendererReady = false;
    bool processTerminating = false;
    bool Active() { return opened; }
    void Draw(void* renderer, void* device, void* flat, void* textured);
    void UpdateMovie(void* renderer);
}

namespace GameMenu
{
    using WobbleFix::Read;
    using WobbleFix::Write;
    SafetyHookInline shWindow, shScheduler, shInterface, shResetGraphics;
    Game::State game;
    std::atomic<bool> request = false;
    std::mutex menuMutex;
    HWND window = nullptr;
    bool confirmation = false, yes = false;
    bool previousStart = false;
    int selection = 0, scroll = 0;
    uint32_t previousInput = 0;
    std::atomic<uint32_t> keyboardHeld = 0, keyboardPressed = 0;
    uint64_t repeatAt = 0;
    void* textureDevice = nullptr;
    void* pausedMovie = nullptr;
    DWORD movieState = 0;
    uint32_t* playTimeAnchor = nullptr;
    uint32_t(__cdecl* frameCounter)(int) = nullptr;
    uint32_t pauseTime = 0;
    bool pauseClock = false;
    using CreateTexture = int(__thiscall*)(void*, void*, int, int, int, int);
    CreateTexture createTexture = nullptr;
    constexpr std::array<const char*, 12> rows = { "Resume", "Wobble fix", "Widescreen", "Alternate controls", "Keyboard movement",
        "Skip intro", "Skip doors", "Fast load", "Auto load", "Load slot", "Load game", "Exit game" };
    constexpr std::array<Game::Option, 7> options = { Game::Option::WobbleFix, Game::Option::PanAndScan, Game::Option::AlternateControls,
        Game::Option::SkipIntro, Game::Option::SkipDoor, Game::Option::FastLoad, Game::Option::AutoLoad };

    struct TextTexture
    {
        std::array<uint32_t, 37> native{};
        int width = 0, height = 0;
        TextTexture() = default;
        TextTexture(const TextTexture&) = delete;
        TextTexture& operator=(const TextTexture&) = delete;
        ~TextTexture()
        {
            // ExitProcess can bypass native graphics cleanup. Driver calls are
            // unsafe once process detach has begun; the OS reclaims resources.
            if (Menu::processTerminating) return;
            if (native[0]) reinterpret_cast<IDirectDrawSurface4*>(native[0])->Release();
            if (native[1]) reinterpret_cast<IDirect3DTexture2*>(native[1])->Release();
        }
    };
    std::unordered_map<std::wstring, std::unique_ptr<TextTexture>> textures;

    uint32_t Channel(unsigned value, DWORD mask)
    {
        if (!mask) return 0;
        const auto shift = std::countr_zero(mask);
        return ((value * (mask >> shift) + 127) / 255 << shift) & mask;
    }

    TextTexture* Texture(const std::wstring& text, void* device)
    {
        if (textureDevice != device)
        {
            textures.clear();
            textureDevice = device;
        }
        if (const auto found = textures.find(text); found != textures.end())
        {
            if (reinterpret_cast<IDirectDrawSurface4*>(found->second->native[0])->IsLost() == DD_OK)
                return found->second.get();
            textures.erase(found);
        }
        const auto dc = CreateCompatibleDC(nullptr);
        if (!dc) return nullptr;
        // Rasterize localized glyphs, including Japanese, into an engine texture.
        // Panels, text, blending and presentation all use the game's renderer.
        const auto font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH,
            Localization::language == "japanese" ? L"MS Gothic" : L"Times New Roman");
        const auto oldFont = SelectObject(dc, font);
        SIZE size{};
        GetTextExtentPoint32W(dc, text.c_str(), int(text.size()), &size);
        const int width = int(std::bit_ceil(unsigned(std::clamp(size.cx + 2L, 8L, 512L))));
        constexpr int height = 16;
        BITMAPINFO bitmap{};
        bitmap.bmiHeader = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB };
        uint32_t* pixels = nullptr;
        const auto image = CreateDIBSection(dc, &bitmap, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        const auto oldImage = SelectObject(dc, image);
        bool ready = image && pixels;
        std::unique_ptr<TextTexture> texture;
        if (ready)
        {
            memset(pixels, 0, width * height * 4);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            TextOutW(dc, 1, 0, text.c_str(), int(text.size()));
            GdiFlush();
            texture = std::make_unique<TextTexture>();
            ready = createTexture(texture->native.data(), device, width, height, 0, 0x2A) != 0;
            if (ready)
            {
                auto* surface = reinterpret_cast<IDirectDrawSurface4*>(texture->native[0]);
                DDSURFACEDESC2 locked = { .dwSize = sizeof(DDSURFACEDESC2) };
                ready = surface->Lock(nullptr, &locked, DDLOCK_WAIT | DDLOCK_WRITEONLY, nullptr) == DD_OK;
                if (ready)
                {
                    const auto& format = locked.ddpfPixelFormat;
                    const int bytes = int(format.dwRGBBitCount / 8);
                    ready = bytes == 2 || bytes == 4;
                    if (ready)
                    {
                        for (DWORD y = 0; y < locked.dwHeight; ++y)
                            memset(static_cast<uint8_t*>(locked.lpSurface) + y * locked.lPitch, 0, locked.dwWidth * bytes);
                        for (int y = 0; y < height; ++y)
                            for (int x = 0; x < width; ++x)
                            {
                                const auto alpha = pixels[y * width + x] & 255;
                                const uint32_t color = Channel(255, format.dwRBitMask) | Channel(255, format.dwGBitMask)
                                    | Channel(255, format.dwBBitMask) | Channel(alpha, format.dwRGBAlphaBitMask);
                                memcpy(static_cast<uint8_t*>(locked.lpSurface) + y * locked.lPitch + x * bytes, &color, bytes);
                            }
                    }
                    surface->Unlock(nullptr);
                    texture->width = width;
                    texture->height = height;
                }
            }
        }
        SelectObject(dc, oldImage); SelectObject(dc, oldFont);
        if (image) DeleteObject(image);
        if (font) DeleteObject(font);
        DeleteDC(dc);
        if (!ready) return nullptr;
        auto* result = texture.get();
        textures.emplace(text, std::move(texture));
        return result;
    }

    void Close()
    {
        if (pauseClock)
        {
            *playTimeAnchor += frameCounter(-1) - pauseTime;
            pauseClock = false;
        }
        Menu::opened = confirmation = false;
        Input::suppressUntilRelease = true;
        *game.held = *game.pressed = 0;
    }

    void Change(int delta)
    {
        if (selection == 4)
        {
            auto& mode = Game::GetSettings().keyboardRunMode;
            mode = (mode.load() + delta + 4) % 4;
        }
        else if (selection == 9)
        {
            auto& slot = Game::GetSettings().loadSlot;
            slot = (slot.load() + delta + 31) % 31;
        }
        else if (selection >= 1 && selection <= 8)
        {
            const auto index = selection - 1 - int(selection > 4);
            Game::Toggle(options[index]);
        }
        else return;
        Game::SaveSettings();
    }

    void Activate()
    {
        if (confirmation)
        {
            if (yes)
            {
                Close();
                if (window) PostMessageW(window, WM_CLOSE, 0, 0);
            }
            else confirmation = false;
        }
        else if (selection == 0) Close();
        else if (selection == 11) { confirmation = true; yes = false; }
        else if (selection == 10)
        {
            if (Saves::CanLoad())
            {
                Saves::Request(Game::GetSettings().loadSlot.load());
                Close();
            }
        }
        else Change(1);
    }

    bool Handle(uint32_t input, bool start, bool escape)
    {
        const std::lock_guard lock(menuMutex);
        const auto pressed = (input & ~previousInput) | keyboardPressed.exchange(0);
        const bool toggle = request.exchange(false) || (start && !previousStart)
            || ((pressed & 32) && escape && (Menu::opened || !Input::InNativeMenu()));
        const bool wasOpen = Menu::opened;
        if (toggle)
        {
            if (Menu::opened)
            {
                if (confirmation) confirmation = false;
                else Close();
            }
            else if (Menu::rendererReady)
            {
                Menu::opened = true;
                Input::suppressUntilRelease = true;
                pauseClock = playTimeAnchor && frameCounter && (*game.flags & 0x08000000);
                if (pauseClock) pauseTime = frameCounter(-1);
                confirmation = false;
                selection = scroll = 0;
                repeatAt = GetTickCount64() + 400;
            }
        }
        if (Menu::opened && wasOpen && !toggle)
        {
            const auto now = GetTickCount64();
            uint32_t navigation = pressed & 15;
            if (input & 15 && input == previousInput && now >= repeatAt)
            {
                navigation |= input & 15;
                repeatAt = now + 100;
            }
            else if (navigation) repeatAt = now + 400;
            if (confirmation)
            {
                if (navigation) yes = !yes;
            }
            else
            {
                if (navigation & 1) selection = (selection + int(rows.size()) - 1) % int(rows.size());
                if (navigation & 2) selection = (selection + 1) % int(rows.size());
                if (navigation & 4) Change(-1);
                if (navigation & 8) Change(1);
                scroll = std::clamp(scroll, std::max(0, selection - 9), selection);
            }
            if (pressed & 16) Activate();
            if (pressed & 32)
            {
                if (confirmation) confirmation = false;
                else Close();
            }
        }
        previousInput = input;
        previousStart = start;
        return wasOpen || Menu::opened;
    }

    bool Update()
    {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        const bool focused = process == GetCurrentProcessId();
        if (focused && !window) window = GetForegroundWindow();
        const auto key = [&](int code) { return focused && (GetAsyncKeyState(code) & 0x8000) != 0; };
        const auto pad = Input::GetPad();
        const bool start = focused && (pad.buttons & 1);
        const uint32_t input = (focused ? keyboardHeld.load() : 0)
            | uint32_t(focused && ((pad.dpad & 1) || pad.left.y > 0.5f))
            | uint32_t(focused && ((pad.dpad & 4) || pad.left.y < -0.5f)) << 1
            | uint32_t(focused && ((pad.dpad & 8) || pad.left.x < -0.5f)) << 2
            | uint32_t(focused && ((pad.dpad & 2) || pad.left.x > 0.5f)) << 3
            | uint32_t(focused && (pad.xinput ? (pad.buttons & 2) != 0 : (*game.held & 0x1000) != 0)) << 4
            | uint32_t(key(VK_ESCAPE) || (focused && (pad.xinput ? (pad.buttons & 4) != 0 : (*game.held & 0x2000) != 0))) << 5;
        return Handle(input, start, key(VK_ESCAPE));
    }

    int __fastcall WindowHook(void* application, void*, UINT message, WPARAM wparam, LPARAM lparam)
    {
        window = Read<HWND>(application, 40);
        if (message == WM_KILLFOCUS)
        {
            keyboardHeld = keyboardPressed = 0;
        }
        if (message == WM_KEYDOWN || message == WM_KEYUP)
        {
            uint32_t input = 0;
            switch (wparam)
            {
                case VK_UP: input = 1; break;
                case VK_DOWN: input = 2; break;
                case VK_LEFT: input = 4; break;
                case VK_RIGHT: input = 8; break;
                case VK_RETURN: input = 16; break;
            }
            if (message == WM_KEYUP) keyboardHeld.fetch_and(~input);
            else if (Menu::opened && input && !(lparam & (1L << 30)))
            {
                keyboardHeld.fetch_or(input);
                keyboardPressed.fetch_or(input);
            }
            if (Menu::opened && input) return 0;
        }
        if (message == WM_COMMAND && LOWORD(wparam) == 40001)
        {
            if (!Menu::rendererReady)
                return shWindow.unsafe_thiscall<int>(application, message, wparam, lparam);
            // ESC is polled once per frame; accelerator auto-repeat cannot
            // immediately close the menu. The window's Quit command also opens it.
            if (!(GetAsyncKeyState(VK_ESCAPE) & 0x8000)) request = true;
            return 0;
        }
        return shWindow.unsafe_thiscall<int>(application, message, wparam, lparam);
    }

    int __cdecl SchedulerHook()
    {
        // Freeze all native task coroutines, including scripts and AI. The
        // renderer replays the last complete room frame while drawing this UI.
        if (Update()) return 0;
        return shScheduler.unsafe_ccall<int>();
    }

    int __cdecl InterfaceHook()
    {
        // Keep fades, dialogue and cinematic bars at their paused frame.
        if (Menu::opened) return 0;
        return shInterface.unsafe_ccall<int>();
    }

    int __fastcall ResetGraphicsHook(void* graphics, void*)
    {
        {
            const std::lock_guard lock(menuMutex);
            // Release our references before the game releases its D3D device,
            // including mode resets and exit through the window close button.
            Menu::opened = confirmation = false;
            pauseClock = false;
            textures.clear();
            textureDevice = nullptr;
            if (pausedMovie)
            {
                const auto* table = *static_cast<uintptr_t**>(pausedMovie);
                reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD)>(table[7])(pausedMovie, movieState);
                reinterpret_cast<ULONG(__stdcall*)(void*)>(table[2])(pausedMovie);
                pausedMovie = nullptr;
            }
        }
        return shResetGraphics.unsafe_thiscall<int>(graphics);
    }

    void Init()
    {
        if (!Game::Resolve(game)) return;
        auto windowPattern = hook::pattern("53 55 8B 6C 24 0C 56 8B F1 57 8B 7C 24 18 83 FD 20 8B 5E 28");
        auto scheduler = hook::pattern("56 57 BE ? ? ? ? 8D 7E F0 33 C0 89 3D ? ? ? ? 66 8B 07 48 74 ? 48 74 ? 83 E8 02");
        auto interfacePattern = hook::pattern("E8 ? ? ? ? E8 ? ? ? ? E9 ? ? ? ? 90 A0 ? ? ? ? 56 8B 35 ? ? ? ? A8 08 A0");
        auto texture = hook::pattern("56 8B F1 E8 ? ? ? ? 8B 4C 24 10 8B 44 24 0C 8B 54 24 14 89 4E 0C 8B 4C 24 08 89 46 08");
        auto clock = hook::pattern("6A FF E8 ? ? ? ? 8B 15 ? ? ? ? 8B C8 2B CA 8B 15 ? ? ? ? 83 C4 04 03 D1 89 15 ? ? ? ? A3");
        auto resetGraphics = hook::pattern("56 8B F1 57 33 FF 8B 86 E4 00 00 00 3B C7 74 10 8B 4E 04 3B CF 74 09");
        if (windowPattern.size() != 1 || scheduler.size() != 1 || interfacePattern.size() != 1 || texture.size() != 1
            || resetGraphics.size() != 1)
            return;
        shResetGraphics = safetyhook::create_inline(resetGraphics.get_first(), ResetGraphicsHook);
        if (!shResetGraphics) return;
        Localization::Init();
        if (clock.size() == 1)
        {
            playTimeAnchor = *clock.get_first<uint32_t*>(9);
            frameCounter = reinterpret_cast<decltype(frameCounter)>(injector::GetBranchDestination(clock.get_first(2)).as_int());
        }
        createTexture = reinterpret_cast<CreateTexture>(texture.get_first());
        shWindow = safetyhook::create_inline(windowPattern.get_first(), WindowHook);
        shScheduler = safetyhook::create_inline(scheduler.get_first(), SchedulerHook);
        shInterface = safetyhook::create_inline(interfacePattern.get_first(), InterfaceHook);
    }
}

void Menu::UpdateMovie(void* renderer)
{
    using namespace GameMenu;
    if (opened && !pausedMovie && (*game.flags & 0x10000))
    {
        auto* stream = Read<void*>(renderer, 51976);
        if (!stream) return;
        const auto* table = *static_cast<uintptr_t**>(stream);
        DWORD state = 0;
        if (SUCCEEDED(reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD*)>(table[6])(stream, &state))
            && SUCCEEDED(reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD)>(table[7])(stream, 0)))
        {
            reinterpret_cast<ULONG(__stdcall*)(void*)>(table[1])(stream);
            pausedMovie = stream;
            movieState = state;
        }
    }
    else if (!opened && pausedMovie)
    {
        const auto* table = *static_cast<uintptr_t**>(pausedMovie);
        reinterpret_cast<HRESULT(__stdcall*)(void*, DWORD)>(table[7])(pausedMovie, movieState);
        reinterpret_cast<ULONG(__stdcall*)(void*)>(table[2])(pausedMovie);
        pausedMovie = nullptr;
    }
}

void Menu::Draw(void* renderer, void* device, void* flat, void* textured)
{
    if (!opened || !GameMenu::createTexture) return;
    using namespace GameMenu;
    const std::lock_guard lock(menuMutex);
    if (!opened) return;
    const float width = float(Read<int>(renderer, 33996)), height = float(Read<int>(renderer, 34000));
    const auto viewport = Presentation::Viewport::Fit(width, height, 4.0f / 3.0f);
    const float scale = viewport.width / 320.0f;
    struct Vertex { float x, y, z, rhw; uint32_t color, specular; float u, v; };
    using Flat = int(__thiscall*)(void*, void*, int, unsigned);
    using Textured = int(__thiscall*)(void*, void*, int, int, unsigned);
    const auto panel = [&](float x, float y, float w, float h, uint32_t color)
    {
        struct FlatVertex { float x, y, z, rhw; uint32_t color, specular; };
        const float l = viewport.x + x * scale, t = viewport.y + y * scale;
        FlatVertex vertices[] = { {l,t,0,1,color,0}, {l,t+h*scale,0,1,color,0}, {l+w*scale,t,0,1,color,0}, {l+w*scale,t+h*scale,0,1,color,0} };
        reinterpret_cast<Flat>(flat)(device, vertices, 4, 0);
    };
    const auto text = [&](float x, float y, const std::wstring& label, uint32_t color)
    {
        auto* texture = Texture(label, Read<void*>(device, 0));
        if (!texture) return;
        const float l = viewport.x + x * scale, t = viewport.y + y * scale;
        const float w = texture->width * scale, h = texture->height * scale;
        const float u = float(texture->width) / Read<int>(texture->native.data(), 8);
        const float v = float(texture->height) / Read<int>(texture->native.data(), 12);
        Vertex vertices[] = { {l,t,0,1,color,0,0,0}, {l,t+h,0,1,color,0,0,v}, {l+w,t,0,1,color,0,u,0}, {l+w,t+h,0,1,color,0,u,v} };
        reinterpret_cast<Textured>(textured)(device, vertices, 4, int(texture->native[1]), 0x20);
    };
    panel(-viewport.x / scale, -viewport.y / scale, width / scale, height / scale, 0x80000000);
    panel(17, 17, 286, 206, 0xFF777777);
    panel(18, 18, 284, 204, 0xFF080808);
    text(29, 25, Localization::Text("FUSION FIX OPTIONS"), 0xFFE0E0E0);
    panel(27, 43, 266, 1, 0xFF555555);
    if (confirmation)
    {
        text(33, 83, Localization::Text("Quit the game?"), 0xFFFFFFFF);
        text(65, 122, (yes ? L"> " : L"  ") + Localization::Text("Yes"), yes ? 0xFFE8BC68 : 0xFFAAAAAA);
        text(173, 122, (!yes ? L"> " : L"  ") + Localization::Text("No"), !yes ? 0xFFE8BC68 : 0xFFAAAAAA);
    }
    else
    {
        for (int row = scroll; row < std::min(scroll + 10, int(rows.size())); ++row)
        {
            std::wstring value;
            if (row == 4)
            {
                constexpr const char* modes[] = { "Hold: run", "Toggle: run", "Hold: walk", "Toggle: walk" };
                value = Localization::Text(modes[Game::GetSettings().keyboardRunMode.load()]);
            }
            else if (row == 9)
            {
                const int slot = Game::GetSettings().loadSlot.load();
                value = slot ? Localization::Text("Slot") + L" " + std::to_wstring(slot) : Localization::Text("Latest");
            }
            else if (row >= 1 && row <= 8)
                value = Localization::Text(Game::Enabled(options[row - 1 - int(row > 4)]) ? "On" : "Off");
            const uint32_t color = row == selection ? 0xFFE8BC68 : row == 10 && !Saves::CanLoad() ? 0xFF555555 : 0xFFCCCCCC;
            const float y = 52.0f + (row - scroll) * 14.5f;
            text(26, y, row == selection ? L">" : L"", color);
            text(37, y, Localization::Text(rows[row]), color);
            if (!value.empty()) text(203, y, value, color);
        }
        text(28, 205, Localization::Text("Navigate / Change / Confirm / Back"), 0xFF888888);
    }
}

class MenuHooks
{
public:
    MenuHooks() { FusionFix::onInitEvent() += []() { GameMenu::Init(); }; }
} MenuHooks;

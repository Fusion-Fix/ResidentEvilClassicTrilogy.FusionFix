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
#include "NativeMenu.hxx"
#include "NativeFont.hxx"
#include "MouseInput.hxx"

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
    SafetyHookInline shWindow, shScheduler, shInterface, shResetGraphics, shFontRead;
    Game::State game;
    std::atomic<bool> request = false;
    std::mutex menuMutex;
    HWND window = nullptr;
    bool confirmation = false, yes = false;
    bool previousStart = false;
    NativeMenu::State menu;
    std::atomic<int> inputDevice = 0;
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
    NativeFont::Font font;
    int(__cdecl* archiveOpen)(const char*, int, int) = nullptr;
    int(__cdecl* archiveSeek)(int, int, int) = nullptr;
    int(__cdecl* archiveRead)(int, void*, int) = nullptr;
    int(__cdecl* archiveClose)(int) = nullptr;
    const char* fontPath = nullptr;
    char* nativeFilePath = nullptr;

    const char* ResolveFontPath()
    {
        // hook::pattern's default range ends with executable code. Font
        // filenames are in .rdata, so search readable initialized PE sections.
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (WORD section = 0; section < nt->FileHeader.NumberOfSections; ++section)
        {
            const auto& data = sections[section];
            if (!(data.Characteristics & IMAGE_SCN_MEM_READ) || data.SizeOfRawData < 27) continue;
            const auto start = base + data.VirtualAddress;
            const auto size = std::min(data.SizeOfRawData, data.Misc.VirtualSize ? data.Misc.VirtualSize : data.SizeOfRawData);
            auto paths = hook::pattern(start, start + size,
                "62 69 6F 31 39 2F ? ? ? ? 5F ? 2F ? ? ? ? 2F ? ? ? ? 2E ? ? ? 00");
            for (size_t i = 0; i < paths.size(); ++i)
            {
                const auto* candidate = paths.get(i).get<char>();
                if (_strnicmp(candidate + 6, "data_", 5) == 0 && _strnicmp(candidate + 13, "etc2/", 5) == 0
                    && _strnicmp(candidate + 18, "tex", 3) == 0 && _stricmp(candidate + 22, ".tim") == 0)
                    return candidate;
            }
        }
        return nullptr;
    }

    int __cdecl FontFileHook(const char* filename, void* buffer)
    {
        const int size = shFontRead.unsafe_ccall<int>(filename, buffer);
        if (size >= 32 && size <= 1024 * 1024 && buffer && fontPath && _stricmp(filename, fontPath) == 0)
        {
            // Capture the decoded TIM before the native upload changes its
            // coordinates or the game's shared read buffer gets reused.
            const std::lock_guard lock(menuMutex);
            if (!font.Ready()) font.Load({static_cast<const uint8_t*>(buffer), size_t(size)}, 3,
                Localization::language == "japanese");
        }
        return size;
    }

    bool LoadFont()
    {
        if (font.Ready()) return true;
        if (!fontPath || !nativeFilePath || !archiveOpen || !archiveSeek || !archiveRead || !archiveClose) return false;
        // Match the native file loader's full filename context as well as its
        // archive-relative argument, then restore the interrupted game's path.
        struct RestorePath
        {
            char* target;
            std::string saved;
            ~RestorePath() { memcpy(target, saved.c_str(), saved.size() + 1); }
        } path{nativeFilePath, std::string(nativeFilePath)};
        if (path.saved.size() < 3) return false;
        memcpy(nativeFilePath + 3, fontPath, strlen(fontPath) + 1);
        const int handle = archiveOpen(fontPath + 5, 0, 0);
        if (handle < 0) return false;
        const int size = archiveSeek(handle, 0, 2);
        bool loaded = false;
        if (size >= 32 && size <= 1024 * 1024)
        {
            // The native loader ignores the rewind return value.
            archiveSeek(handle, 0, 0);
            std::vector<uint8_t> bytes(size);
            loaded = archiveRead(handle, bytes.data(), size) == size
                && font.Load(bytes, 3, Localization::language == "japanese");
        }
        archiveClose(handle);
        return loaded;
    }

    constexpr std::array<Game::Option, 8> options = { Game::Option::WobbleFix, Game::Option::PanAndScan, Game::Option::AlternateControls,
        Game::Option::SkipIntro, Game::Option::SkipDoor, Game::Option::FastLoad, Game::Option::AutoLoad, Game::Option::AutoPush };

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

    TextTexture* Texture(const std::wstring& text, void* device, bool green, bool compact)
    {
        if (!LoadFont()) return nullptr;
        if (textureDevice != device) { textures.clear(); textureDevice = device; }
        const auto key = std::wstring(1, wchar_t(1 + int(green) + 2 * int(compact))) + text;
        if (const auto found = textures.find(key); found != textures.end())
        {
            if (reinterpret_cast<IDirectDrawSurface4*>(found->second->native[0])->IsLost() == DD_OK) return found->second.get();
            textures.erase(found);
        }
        const auto image = font.Render(text, green);
        if (!image.width || !image.height || image.width > 2048) return nullptr;
        const int width = int(std::bit_ceil(unsigned(std::max(image.width, 8))));
        const int height = int(std::bit_ceil(unsigned(std::max(image.height, 8))));
        auto texture = std::make_unique<TextTexture>();
        if (!createTexture(texture->native.data(), device, width, height, 0, 0x2A)) return nullptr;
        auto* surface = reinterpret_cast<IDirectDrawSurface4*>(texture->native[0]);
        DDSURFACEDESC2 locked{ .dwSize = sizeof(DDSURFACEDESC2) };
        if (FAILED(surface->Lock(nullptr, &locked, DDLOCK_WAIT | DDLOCK_WRITEONLY, nullptr))) return nullptr;
        const auto& format = locked.ddpfPixelFormat;
        const int bytes = int(format.dwRGBBitCount / 8);
        const bool ready = bytes == 2 || bytes == 4;
        if (ready)
        {
            for (DWORD y = 0; y < locked.dwHeight; ++y)
                memset(static_cast<uint8_t*>(locked.lpSurface) + y * locked.lPitch, 0, locked.dwWidth * bytes);
            for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x)
            {
                const auto c = image.pixels[y * image.width + x];
                const uint32_t packed = Channel((c >> 16) & 255, format.dwRBitMask) | Channel((c >> 8) & 255, format.dwGBitMask)
                    | Channel(c & 255, format.dwBBitMask) | Channel(c >> 24, format.dwRGBAlphaBitMask);
                memcpy(static_cast<uint8_t*>(locked.lpSurface) + y * locked.lPitch + x * bytes, &packed, bytes);
            }
        }
        const auto unlocked = surface->Unlock(nullptr);
        if (!ready || FAILED(unlocked)) return nullptr;
        texture->width = image.width; texture->height = image.height;
        auto* result = texture.get(); textures.emplace(key, std::move(texture)); return result;
    }

    void Close()
    {
        if (pauseClock)
        {
            *playTimeAnchor += frameCounter(-1) - pauseTime;
            pauseClock = false;
        }
        Menu::opened = confirmation = false;
        MouseInput::Reset();
        Input::suppressUntilRelease = true;
        *game.held = *game.pressed = 0;
    }

    int SelectedRow()
    {
        switch (menu.Selected(3))
        {
        case NativeMenu::Resume: return 0;
        case NativeMenu::Wobble: return 1;
        case NativeMenu::Widescreen: return 2;
        case NativeMenu::Controls: return 3;
        case NativeMenu::DefaultPace: return 4;
        case NativeMenu::SkipIntro: return 5;
        case NativeMenu::SkipDoors: return 6;
        case NativeMenu::FastLoad: return 7;
        case NativeMenu::AutoLoad: return 8;
        case NativeMenu::LoadSlot: return 9;
        case NativeMenu::Load: return 10;
        case NativeMenu::Quit: return 11;
        case NativeMenu::AutoPush: return 12;
        case NativeMenu::Bindings: return 13;
        case NativeMenu::AspectLimit: return 14;
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
        if (selection == 15)
        {
            auto& enabled = Game::GetSettings().mouseSteering;
            enabled = !enabled.load();
            MouseInput::Reset();
        }
        else if (selection == 17)
        {
            auto& toggle = Game::GetSettings().shiftToggle;
            toggle = !toggle.load();
        }
        else if (selection == 16)
        {
            auto& sensitivity = Game::GetSettings().mouseSensitivity;
            sensitivity = std::clamp(sensitivity.load() + delta * 0.25f, 0.25f, 4.0f);
            MouseInput::Reset();
        }
        else if (selection == 13)
        {
            auto& bindings = Game::GetSettings().hdControls;
            bindings = !bindings.load();
        }
        else if (selection == 14)
        {
            auto& aspect = Game::GetSettings().maxAspectRatio;
            aspect = NativeMenu::ChangeAspect(aspect.load(), delta);
            Game::onSettingsChanged().executeAll();
        }
        else if (selection == 12) Game::Toggle(Game::Option::AutoPush);
        else if (selection == 4)
        {
            auto& pace = Game::GetSettings().defaultRun;
            pace = !pace.load();
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
        if (!confirmation && menu.Enter(menu.Selected(3))) return;
        const int selection = SelectedRow();
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
        menu.controller = inputDevice.load() != 0;
        menu.xinput = inputDevice.load() == 2;
        const auto pressed = (input & ~previousInput) | keyboardPressed.exchange(0);
        const bool toggle = request.exchange(false) || (start && !previousStart)
            || ((pressed & 32) && escape && (Menu::opened || !Input::InNativeMenu()));
        const bool wasOpen = Menu::opened;
        if (toggle)
        {
            if (Menu::opened)
            {
                if (start && !previousStart) Close(); else Back();
            }
            else if (Menu::rendererReady)
            {
                Menu::opened = true;
                MouseInput::Reset();
                Input::suppressUntilRelease = true;
                pauseClock = playTimeAnchor && frameCounter && (*game.flags & 0x08000000);
                if (pauseClock) pauseTime = frameCounter(-1);
                confirmation = false;
                menu.Reset();
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
                if (navigation & 1) menu.Move(-1, 3);
                if (navigation & 2) menu.Move(1, 3);
                if (navigation & 4) Change(-1);
                if (navigation & 8) Change(1);
            }
            if (pressed & 16) Activate();
            if (pressed & 32)
            {
                Back();
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
        if (Menu::opened || !game.Controllable() || (*game.flags & 0x10000) || Input::suppressUntilRelease) MouseInput::Reset();
        if (focused && !window) window = GetForegroundWindow();
        const auto key = [&](int code) { return focused && (GetAsyncKeyState(code) & 0x8000) != 0; };
        const auto pad = Input::GetPad();
        const bool start = focused && (pad.buttons & 1);
        if (focused && (pad.buttons || pad.dpad || std::abs(pad.left.x) > 0.5f || std::abs(pad.left.y) > 0.5f)) inputDevice = pad.xinput ? 2 : 1;
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
        MouseInput::WheelMessage(message, wparam, Game::Enabled(Game::Option::PanAndScan)
            && !Menu::opened && game.Controllable() && !(*game.flags & 0x10000));
        MouseInput::Message(window, message, lparam, Game::GetSettings().mouseSteering, Menu::opened);
        if (message == WM_KEYDOWN) inputDevice = 0;
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
        fontPath = ResolveFontPath();
        // These short archive wrappers occur in several unrelated interfaces.
        // Resolve the actual calls made by the game's TIM file loader instead
        // of treating non-unique wrapper signatures as missing functions.
        auto fileLoader = hook::pattern("53 55 56 8B 74 24 10 57 56 68 ? ? ? ? C6 05 ? ? ? ? 00 33 DB E8 ? ? ? ? 8D 7E 05 53 53 57 E8");
        for (size_t i = 0; i < fileLoader.size(); ++i)
        {
            auto* loader = fileLoader.get(i).get<uint8_t>();
            // The partial-file reader shares the prologue, but does not seek
            // to SEEK_END to obtain the complete file's size at this point.
            if (loader[0x57] == 0x6A && loader[0x58] == SEEK_END && loader[0x22] == 0xE8
                && loader[0x5C] == 0xE8 && loader[0x76] == 0xE8 && loader[0x98] == 0xE8)
            {
                nativeFilePath = *reinterpret_cast<char**>(loader + 10);
                archiveOpen = reinterpret_cast<decltype(archiveOpen)>(injector::GetBranchDestination(loader + 0x22).as_int());
                archiveSeek = reinterpret_cast<decltype(archiveSeek)>(injector::GetBranchDestination(loader + 0x5C).as_int());
                archiveRead = reinterpret_cast<decltype(archiveRead)>(injector::GetBranchDestination(loader + 0x76).as_int());
                archiveClose = reinterpret_cast<decltype(archiveClose)>(injector::GetBranchDestination(loader + 0x98).as_int());
                shFontRead = safetyhook::create_inline(loader, FontFileHook);
                break;
            }
        }

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
        reinterpret_cast<Flat>(flat)(device, vertices, 4, 1);
    };
    const auto text = [&](float x, float y, const std::wstring& label, uint32_t color, float available, bool compact)
    {
        auto* texture = Texture(label, Read<void*>(device, 0), color == 0xFF00FF00, compact);
        if (!texture) return;
        const float l = viewport.x + x * scale, t = viewport.y + y * scale;
        const float factor = std::min(compact ? 0.65f : 1.0f, available / texture->width);
        const float w = texture->width * scale * factor, h = texture->height * scale * factor;
        if (color == 0xFF00FF00) color = 0xFFFFFFFF;
        const float u = float(texture->width) / Read<int>(texture->native.data(), 8);
        const float v = float(texture->height) / Read<int>(texture->native.data(), 12);
        Vertex vertices[] = { {l,t,0,1,color,0,0,0}, {l,t+h,0,1,color,0,0,v}, {l+w,t,0,1,color,0,u,0}, {l+w,t+h,0,1,color,0,u,v} };
        reinterpret_cast<Textured>(textured)(device, vertices, 4, int(texture->native[1]), 1);
    };
    panel(-viewport.x / scale, -viewport.y / scale, width / scale, height / scale, 0xB0000000);
    const auto value = [&](NativeMenu::Action action) -> std::wstring
    {
        if (action == NativeMenu::MouseSteering)
            return Localization::Text(Game::GetSettings().mouseSteering ? "On" : "Off");
        if (action == NativeMenu::MouseSensitivity)
        {
            auto number = std::to_wstring(Game::GetSettings().mouseSensitivity.load());
            number.resize(number.find(L'.') + 3); return number;
        }
        if (action == NativeMenu::Bindings)
            return Localization::Text(Game::GetSettings().hdControls ? "Remaster" : "Original");
        if (action == NativeMenu::AspectLimit)
            return NativeMenu::AspectName(Game::GetSettings().maxAspectRatio.load());
        if (action == NativeMenu::Controls)
            return Localization::Text(Game::Enabled(Game::Option::AlternateControls) ? "Alternate" : "Original");
        if (action == NativeMenu::DefaultPace)
            return Localization::Text(Game::GetSettings().defaultRun.load() ? "Run" : "Walk");
        if (action == NativeMenu::ShiftBehavior)
            return Localization::Text(Game::GetSettings().shiftToggle.load() ? "Toggle" : "Hold");
        if (action == NativeMenu::LoadSlot)
        {
            const int slot = Game::GetSettings().loadSlot.load();
            return slot ? Localization::Text("Slot") + L" " + std::to_wstring(slot) : Localization::Text("Latest");
        }
        const auto option = [&]() -> int
        {
            switch (action)
            {
            case NativeMenu::Wobble: return 0;
            case NativeMenu::Widescreen: return 1;
            case NativeMenu::SkipIntro: return 3;
            case NativeMenu::SkipDoors: return 4;
            case NativeMenu::FastLoad: return 5;
            case NativeMenu::AutoLoad: return 6;
            case NativeMenu::AutoPush: return 7;
            default: return -1;
            }
        }();
        return option >= 0 ? Localization::Text(Game::Enabled(options[option]) ? "On" : "Off") : L"";
    };
    NativeMenu::Draw(3, menu, confirmation, yes, Saves::CanLoad(), panel, text, Localization::Text, value);
}

class MenuHooks
{
public:
    MenuHooks() { FusionFix::onInitEvent() += []() { GameMenu::Init(); }; }
} MenuHooks;

module;

#include <common.hxx>
#include <safetyhook.hpp>
#include <Xinput.h>
#include <atomic>
#include "Presentation.hxx"
#include "HDControls.hxx"

export module Input;

import common;
import Geometry;
import Game;

export namespace Input
{
    struct Pad
    {
        bool connected = false;
        Presentation::Direction left, right;
        uint32_t dpad = 0;
        uint32_t pressedDpad = 0;
        uint32_t buttons = 0;
        bool xinput = false;
    };

    std::mutex padMutex;
    Pad pad;
    std::atomic<bool> shift = false;
    bool suppressUntilRelease = false;
    Game::State game;

    bool InNativeMenu()
    {
        return Game::GetSettings().hdControls && game.menu && *game.menu != 0;
    }

    bool ShiftHeld() { return shift.load(std::memory_order_relaxed); }

    Pad GetPad()
    {
        const std::lock_guard lock(padMutex);
        return pad;
    }

    HDControls::State HDState()
    {
        const auto current = GetPad();
        return HDControls::Read(current.xinput ? current.dpad : 0,
            current.xinput ? current.left.x : 0.0f, current.xinput ? current.left.y : 0.0f,
            current.xinput ? current.buttons : 0);
    }
}

namespace ControllerInput
{
    using WobbleFix::Read;
    SafetyHookInline shPoll, shKeyboard;
    SafetyHookMid shPacket;
    uint32_t* rawHeld = nullptr;
    uint16_t* profile = nullptr;
    using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    GetState getState = nullptr;
    DWORD controller = 0;
    std::array<bool, 3> keys{};
    bool movieActive = false, movieInputReady = false;

    static Input::Pad FromXInput(const XINPUT_GAMEPAD& input)
    {
        const auto axis = [](SHORT value) { return float(value) / (value < 0 ? 32768.0f : 32767.0f); };
        return { true, Presentation::Direction::Analog(axis(input.sThumbLX), axis(input.sThumbLY)),
            Presentation::Direction::Analog(axis(input.sThumbRX), axis(input.sThumbRY)),
            uint32_t(bool(input.wButtons & XINPUT_GAMEPAD_DPAD_UP))
            | uint32_t(bool(input.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)) << 1
            | uint32_t(bool(input.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)) << 2
            | uint32_t(bool(input.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)) << 3, 0,
            HDControls::Buttons(input), true };
    }

    static Input::Pad FromDirectInput(void* manager)
    {
        if (Read<int>(manager, 658804) <= 0)
            return {};
        const auto axis = [&](size_t offset) { return std::clamp(float(Read<int32_t>(manager, 260 + offset)) / 1000.0f, -1.0f, 1.0f); };
        // Use the same first joystick as the native input path. DirectInput
        // axes are configured to -1000..1000 before the game's button mapping.
        bool rx = false, ry = false;
        const auto count = std::clamp(Read<int>(manager, 344), 0, 256);
        for (int i = 0; i < count; ++i)
        {
            const auto offset = Read<int>(manager, 659168 + i * 264);
            rx |= offset == 12;
            ry |= offset == 16;
        }
        const auto pov = Read<uint32_t>(manager, 292);
        uint32_t dpad = 0;
        if ((pov & 0xFFFF) != 0xFFFF && pov < 36000)
        {
            const auto octant = ((pov + 2250) / 4500) % 8;
            constexpr uint32_t directions[] = { 1, 3, 2, 6, 4, 12, 8, 9 };
            dpad = directions[octant];
        }
        // GOG's remapped PlayStation pads expose the right stick as Z/Rz.
        return { true, Presentation::Direction::Analog(axis(0), -axis(4)),
            Presentation::Direction::Analog(axis(rx && ry ? 12 : 8), -axis(rx && ry ? 16 : 20)), dpad, 0,
            uint32_t(bool(Read<uint8_t>(manager, 308 + 9) & 0x80))
            | uint32_t(bool(Read<uint8_t>(manager, 308) & 0x80)) << 1
            | uint32_t(bool(Read<uint8_t>(manager, 309) & 0x80)) << 2 };
    }

    static void Keyboard()
    {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        const bool active = process == GetCurrentProcessId();
        Input::shift.store(active && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0, std::memory_order_relaxed);
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const bool held = (GetAsyncKeyState(VK_F2 + int(i)) & 0x8000) != 0;
            if (active && held && !keys[i])
                Game::Toggle(Game::Option(i));
            keys[i] = held;
        }
    }

    static int __fastcall KeyboardPollHook(void* manager, void*)
    {
        const auto result = shKeyboard.unsafe_thiscall<int>(manager);
        // Remove only native keyboard bindings before the engine combines
        // them with DirectInput. HDState reads physical keys independently.
        if (Game::GetSettings().hdControls && Input::game.flags)
            std::memset(static_cast<uint8_t*>(manager) + 658824, 0, 256);
        return result;
    }

    static int __fastcall PollHook(void* manager, void*)
    {
        const auto result = shPoll.unsafe_thiscall<int>(manager);
        auto next = FromDirectInput(manager);
        if (getState)
        {
            XINPUT_STATE input{};
            if (getState(controller, &input) == ERROR_SUCCESS)
                next = FromXInput(input.Gamepad);
            else
                for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
                    if (i != controller && getState(i, &input) == ERROR_SUCCESS)
                    {
                        controller = i;
                        next = FromXInput(input.Gamepad);
                        break;
                    }
        }
        {
            const std::lock_guard lock(Input::padMutex);
            next.pressedDpad = next.dpad & ~Input::pad.dpad;
            Input::pad = next;
        }
        if (Input::game.flags)
        {
            const bool movie = (*Input::game.flags & 0x10000) != 0;
            if (!movie || !movieActive) movieInputReady = false;
            const auto input = Input::HDState();
            // Observe releases on every device poll: the native movie skip
            // check does not start polling its packet until its initial delay.
            // Holding A to start New Game must not also skip its opening.
            if (movie && !input.confirm && !input.examine && !input.cancel && !input.options)
                movieInputReady = true;
            movieActive = movie;
        }
        Keyboard();
        return result;
    }

    static void Init()
    {
        auto keyboard = hook::pattern("56 8B F1 57 8B 86 80 0D 0A 00 8D BE 88 0D 0A 00 57 68 00 01 00 00 8B 08 50 FF 51 24 85 C0");
        auto packet = hook::pattern("8B 15 ? ? ? ? 33 F6 89 15 ? ? ? ? 89 35 ? ? ? ? B9 01 00 00 00 33 DB 66 8B 1F 85 D8");
        auto raw = hook::pattern("8A 0D ? ? ? ? F7 D0 25 FF FF 00 00 80 F9 07 A3");
        auto mapping = hook::pattern("A0 ? ? ? ? 8B F8 A1 ? ? ? ? C1 E7 05 81 C7 ? ? ? ? 8B E8");
        if (packet.size() == 1 && raw.size() == 1 && mapping.size() == 1 && keyboard.size() == 1 && Game::Resolve(Input::game))
        {
            rawHeld = *raw.get_first<uint32_t*>(17);
            profile = *mapping.get_first<uint16_t*>(17);
            shKeyboard = safetyhook::create_inline(keyboard.get_first(), KeyboardPollHook);
            shPacket = safetyhook::create_mid(packet.get_first(), [](SafetyHookContext& ctx)
            {
                if (*Input::game.flags & 0x10000000) return; // recorded demo input
                const auto pad = Input::GetPad();
                auto input = Input::HDState();
                if (Game::GetSettings().hdControls)
                {
                    const bool movie = (*Input::game.flags & 0x10000) != 0;
                    uint32_t legacy = 0;
                    if (pad.connected && !pad.xinput)
                    {
                        // Translate the controller's selected native profile
                        // into the default packet used by remaster bindings.
                        const auto* nativeProfile = reinterpret_cast<const uint16_t*>(ctx.edi);
                        for (int i = 0; i < 16; ++i)
                            if (ctx.eax & nativeProfile[i]) legacy |= profile[i];
                    }
                    ctx.eax = movie && !movieInputReady ? 0
                        : HDControls::Native(input, 3, Input::InNativeMenu(), movie) | legacy;
                    ctx.edi = uintptr_t(profile);
                    // Native analog steering must not remove freshly mapped
                    // left/right packet bits later in this function.
                    *reinterpret_cast<uint8_t*>(ctx.esp + 0x12) = 0;
                }
                if (Input::suppressUntilRelease)
                {
                    if (!ctx.eax && !input.Any()) Input::suppressUntilRelease = false;
                    ctx.eax = 0;
                }
                *rawHeld = ctx.eax;
            });
        }
        auto pattern = hook::pattern("51 8B 81 74 0D 0A 00 53 55 33 ED 56 57 85 C0 89 4C 24 10 7E 57 8D B1 54 01 00 00");
        if (pattern.size() != 1)
            return;
        for (const auto* name : { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" })
            if (const auto library = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
            {
                getState = reinterpret_cast<GetState>(GetProcAddress(library, "XInputGetState"));
                if (getState)
                    break;
                FreeLibrary(library);
            }
        shPoll = safetyhook::create_inline(pattern.get_first(), PollHook);
    }
}

class InputHooks
{
public:
    InputHooks()
    {
        FusionFix::onInitEvent() += []() { ControllerInput::Init(); };
    }
} InputHooks;

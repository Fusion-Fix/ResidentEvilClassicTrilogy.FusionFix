module;
#include <common.hxx>
#include <Xinput.h>
#include <mmsystem.h>
#include "ClassicPresentation.hxx"
#include "HDControls.hxx"

export module ClassicInput;
import ClassicGame;

export namespace ClassicInput
{
    struct Pad
    {
        bool connected = false, xinput = false;
        Presentation::Direction left, right;
        uint32_t dpad = 0, pressedDpad = 0, buttons = 0, pressedButtons = 0;
    };

    Pad pad;
    bool gameInputSuppressed = false;
    std::function<bool()> nativeMenu;
    // Some title screens retain Escape for Fusion Fix while still requiring
    // native menu button semantics (A/B instead of gameplay actions).
    std::function<bool()> nativeActionMenu;
    bool InNativeMenu() { return ClassicGame::GetSettings().hdControls && nativeMenu && nativeMenu(); }
    bool shift = false;
    bool escapeHeld = false, escapePressed = false;
    std::array<bool, 2> keys{};
    using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

    GetState GetController()
    {
        static const auto result = []() -> GetState
        {
            for (const auto* name : { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" })
                if (const auto library = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
                {
                    if (auto function = GetProcAddress(library, "XInputGetState"))
                        return reinterpret_cast<GetState>(function);
                    FreeLibrary(library);
                }
            return nullptr;
        }();
        return result;
    }

    Pad LegacyController()
    {
        // Both native engines use WinMM joystick data before mapping buttons.
        // Read its axes directly so non-XInput pads retain analog movement.
        static const auto library = GetModuleHandleW(L"winmm.dll");
        static const auto position = reinterpret_cast<decltype(&joyGetPosEx)>(GetProcAddress(library, "joyGetPosEx"));
        static const auto capabilities = reinterpret_cast<decltype(&joyGetDevCapsW)>(GetProcAddress(library, "joyGetDevCapsW"));
        static UINT controller = UINT_MAX;
        static JOYCAPSW caps{};
        static uint64_t nextScan = 0;
        if (!position || !capabilities) return {};
        JOYINFOEX state{ .dwSize = sizeof(JOYINFOEX), .dwFlags = JOY_RETURNALL };
        if (controller == UINT_MAX || position(controller, &state) != JOYERR_NOERROR)
        {
            controller = UINT_MAX;
            const auto now = GetTickCount64();
            if (now < nextScan) return {};
            nextScan = now + 2000;
            for (UINT i = 0; i < 32; ++i)
                if (position(i, &state) == JOYERR_NOERROR && capabilities(i, &caps, sizeof(caps)) == JOYERR_NOERROR)
                { controller = i; break; }
            if (controller == UINT_MAX) return {};
        }
        const auto axis = [](DWORD value, UINT low, UINT high)
        { return high > low ? std::clamp(2.0f * (float(value) - low) / float(high - low) - 1.0f, -1.0f, 1.0f) : 0.0f; };
        Pad result;
        result.connected = true;
        result.left = Presentation::Direction::Analog(axis(state.dwXpos, caps.wXmin, caps.wXmax), -axis(state.dwYpos, caps.wYmin, caps.wYmax));
        if ((caps.wCaps & (JOYCAPS_HASZ | JOYCAPS_HASR)) == (JOYCAPS_HASZ | JOYCAPS_HASR))
            result.right = Presentation::Direction::Analog(axis(state.dwZpos, caps.wZmin, caps.wZmax), -axis(state.dwRpos, caps.wRmin, caps.wRmax));
        if ((caps.wCaps & JOYCAPS_HASPOV) && state.dwPOV < 36000)
        {
            constexpr uint32_t directions[] = { 1, 3, 2, 6, 4, 12, 8, 9 };
            result.dpad = directions[((state.dwPOV + 2250) / 4500) % 8];
        }
        result.buttons = uint32_t(bool(state.dwButtons & (1u << 9)))
            | uint32_t(bool(state.dwButtons & 1)) << 1 | uint32_t(bool(state.dwButtons & 2)) << 2;
        return result;
    }

    void Update()
    {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        const bool active = process == GetCurrentProcessId();
        shift = active && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool escape = active && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
        escapePressed = escape && !escapeHeld;
        escapeHeld = escape;
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const bool held = (GetAsyncKeyState(VK_F3 + int(i)) & 0x8000) != 0;
            if (active && held && !keys[i]) ClassicGame::Toggle(ClassicGame::Option(i));
            keys[i] = held;
        }
        Pad next;
        if (active)
            if (const auto getState = GetController())
                for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
                {
                    XINPUT_STATE state{};
                    if (getState(i, &state) != ERROR_SUCCESS) continue;
                    const auto& input = state.Gamepad;
                    const auto axis = [](SHORT value) { return float(value) / (value < 0 ? 32768.0f : 32767.0f); };
                    next.connected = next.xinput = true;
                    next.left = Presentation::Direction::Analog(axis(input.sThumbLX), axis(input.sThumbLY));
                    next.right = Presentation::Direction::Analog(axis(input.sThumbRX), axis(input.sThumbRY));
                    next.dpad = uint32_t(bool(input.wButtons & XINPUT_GAMEPAD_DPAD_UP))
                        | uint32_t(bool(input.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)) << 1
                        | uint32_t(bool(input.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)) << 2
                        | uint32_t(bool(input.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)) << 3;
                    next.buttons = HDControls::Buttons(input);
                    break;
                }
        if (active && !next.connected) next = LegacyController();
        next.pressedDpad = next.dpad & ~pad.dpad;
        next.pressedButtons = next.buttons & ~pad.buttons;
        pad = next;
    }

    HDControls::State HDState()
    {
        // Unidentified legacy pads retain the native action bindings. Their
        // axes and D-pad still use the existing movement/pan support.
        return HDControls::Read(pad.xinput ? pad.dpad : 0,
            pad.xinput ? pad.left.x : 0.0f, pad.xinput ? pad.left.y : 0.0f,
            pad.xinput ? pad.buttons : 0);
    }

    uint16_t HDPacket(int game, uint16_t native, uint16_t legacy = 0)
    {
        if (!ClassicGame::GetSettings().hdControls) return native;
        const bool menu = nativeActionMenu ? nativeActionMenu() : InNativeMenu();
        return HDControls::Native(HDState(), game, menu) | (pad.xinput ? 0 : legacy);
    }

    bool Run(const Presentation::Direction& direction, bool stick)
    {
        static bool shiftHeld = false, toggled = false, stickRunning = false;
        static bool previousPace = false, previousBehavior = false, initialized = false;
        const bool defaultRun = ClassicGame::GetSettings().defaultRun.load(std::memory_order_relaxed);
        const bool shiftToggle = ClassicGame::GetSettings().shiftToggle.load(std::memory_order_relaxed);
        if (!initialized || defaultRun != previousPace || shiftToggle != previousBehavior)
        {
            toggled = false; shiftHeld = shift;
            previousPace = defaultRun; previousBehavior = shiftToggle;
            initialized = true;
        }
        if (!stick && shiftToggle && shift && !shiftHeld) toggled = !toggled;
        shiftHeld = shift;
        if (stick)
        {
            stickRunning = std::hypot(direction.x, direction.y) >= (stickRunning ? 0.65f : 0.70f);
            return stickRunning || (ClassicGame::GetSettings().hdControls && (pad.buttons & HDControls::X));
        }
        stickRunning = false;
        return defaultRun != (shiftToggle ? toggled : shift);
    }
}

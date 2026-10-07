#pragma once
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

namespace MouseInput
{
    // Native directions are clockwise: up, right, down, left.
    // These are the same logical bits used by Presentation::Direction::Digital.
    enum Direction : uint32_t { Up = 1, Right = 2, Down = 4, Left = 8 };
    constexpr uint32_t horizontalMask = Right | Left, verticalMask = Up | Down;
    inline std::mutex mutex;
    inline double deltaX = 0, deltaY = 0, turn = 0, pitch = 0;
    inline uint64_t received = 0;
    inline bool active = false, aiming = false;
    inline uint32_t previousDirection = 0;
    inline std::atomic<uint64_t> resetGeneration = 0;
    struct Command { uint32_t held = 0, pressed = 0; unsigned turnSteps = 0; };
    inline HWND registeredWindow = nullptr;
    inline bool clipped = false;
    inline RECT previousClip{}, currentClip{};
    inline bool wheelActive = false;
    inline double wheelDelta = 0;
    inline bool Focused();

    inline void WheelMessage(UINT message, WPARAM key, bool enabled)
    {
        const std::lock_guard lock(mutex);
        if (!enabled || !Focused()) { wheelDelta = 0; wheelActive = false; return; }
        if (message == WM_MOUSEWHEEL && wheelActive)
            wheelDelta = std::clamp(wheelDelta + GET_WHEEL_DELTA_WPARAM(key), -1200.0, 1200.0);
    }
    inline float WheelPan(bool enabled)
    {
        const std::lock_guard lock(mutex);
        wheelActive = enabled && Focused();
        const float result = wheelActive ? float(wheelDelta / WHEEL_DELTA) * 0.2f : 0.0f;
        wheelDelta = 0;
        return result;
    }

    inline bool Focused()
    {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        return process == GetCurrentProcessId();
    }
    inline void ReleaseCursor()
    {
        if (!clipped) return;
        RECT actual{};
        if (GetClipCursor(&actual) && EqualRect(&actual, &currentClip)) ClipCursor(&previousClip);
        clipped = false;
    }
    inline void CaptureCursor()
    {
        const auto window = GetForegroundWindow();
        RECT client{};
        if (!window || IsIconic(window) || !GetClientRect(window, &client)) return;
        POINT points[2] = { {client.left, client.top}, {client.right, client.bottom} };
        if (!ClientToScreen(window, &points[0]) || !ClientToScreen(window, &points[1])) return;
        const RECT next{points[0].x, points[0].y, points[1].x, points[1].y};
        if (next.right <= next.left || next.bottom <= next.top) return;
        if (!clipped && !GetClipCursor(&previousClip)) return;
        if (ClipCursor(&next)) { currentClip = next; clipped = true; }
    }
    inline void ClearLocked()
    {
        deltaX = deltaY = turn = pitch = 0;
        active = aiming = false;
        previousDirection = 0;
        ++resetGeneration;
        ReleaseCursor();
    }
    inline void Reset()
    {
        const std::lock_guard lock(mutex);
        ClearLocked();
    }
    inline void Message(HWND window, UINT message, LPARAM parameter, bool enabled, bool paused)
    {
        if (!enabled || paused || !Focused()) { Reset(); return; }
        if (message == WM_DESTROY || message == WM_KILLFOCUS || message == WM_DISPLAYCHANGE)
        { Reset(); if (message == WM_DESTROY && registeredWindow == window) registeredWindow = nullptr; return; }
        // Foreground raw input keeps working at desktop cursor edges. Do not
        // disable legacy mouse messages, so aim/fire buttons stay native.
        if (registeredWindow != window && window)
        {
            RAWINPUTDEVICE mouse{ 1, 2, 0, window };
            if (RegisterRawInputDevices(&mouse, 1, sizeof(mouse))) registeredWindow = window;
        }
        if (message != WM_INPUT) return;
        UINT size = 0;
        const auto handle = reinterpret_cast<HRAWINPUT>(parameter);
        if (GetRawInputData(handle, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0
            || size < sizeof(RAWINPUT)) return;
        std::vector<uint8_t> buffer(size);
        if (GetRawInputData(handle, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
        const auto& input = *reinterpret_cast<const RAWINPUT*>(buffer.data());
        if (input.header.dwType != RIM_TYPEMOUSE || (input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) return;
        const std::lock_guard lock(mutex);
        // Inputs received outside a permitted player update are discarded.
        if (!active) return;
        deltaX = std::clamp(deltaX + input.data.mouse.lLastX, -1000.0, 1000.0);
        deltaY = std::clamp(deltaY + input.data.mouse.lLastY, -1000.0, 1000.0);
        received = GetTickCount64();
    }

    inline Command Directions(bool allowed, bool aim, bool horizontal, bool vertical, float sensitivity)
    {
        const std::lock_guard lock(mutex);
        if (!allowed || !Focused()) { ClearLocked(); return {}; }
        active = true;
        CaptureCursor();
        if (aim != aiming) { pitch = deltaY = 0; aiming = aim; }
        const auto now = GetTickCount64();
        if (now - received > 200) deltaX = deltaY = turn = 0;
        // Convert relative motion to a bounded number of native turn increments.
        // Consume them this frame instead of queuing a slow keyboard-rate turn.
        turn = horizontal ? std::clamp(turn + deltaX * sensitivity * 0.08, -8.0, 8.0) : 0;
        if (aim) pitch = std::clamp(pitch + deltaY * sensitivity / 80.0, -1.0, 1.0);
        else pitch = 0;
        deltaX = deltaY = 0;
        uint32_t direction = 0;
        const unsigned steps = unsigned(std::floor(std::abs(turn)));
        if (steps)
        {
            direction |= turn > 0 ? Right : Left;
            turn += turn > 0 ? -double(steps) : double(steps);
        }
        if (aim && vertical && pitch < -0.3) direction |= Up;
        else if (aim && vertical && pitch > 0.3) direction |= Down;
        const Command command{ direction, direction & ~previousDirection, steps };
        previousDirection = direction;
        return command;
    }

    // Alternate movement supplies a screen-relative base heading every tick.
    // Preserve the native walk/run handler's mouse turn as an additive offset
    // until movement stops, or a menu/focus/script transition resets input.
    struct MovingHeading
    {
        int offset = 0;
        int16_t before = 0;
        uint64_t generation = 0;
        bool tracking = false;
        void Reset() { offset = 0; tracking = false; }
        void Align(int16_t base, int16_t actual)
        {
            offset = (int(actual) - int(base)) & 4095;
            generation = resetGeneration.load();
        }
        int16_t Prepare(int16_t base, bool enabled)
        {
            const auto current = resetGeneration.load();
            if (!enabled || current != generation) Reset();
            generation = current;
            tracking = enabled;
            before = int16_t((int(base) + offset) & 4095);
            return before;
        }
        void Record(int16_t after, bool mouseTurn)
        {
            if (!tracking || !mouseTurn) return;
            const int difference = ((int(after) - int(before) + 2048) & 4095) - 2048;
            // Ignore animation/camera-driven teleports; keep only native turn
            // increments generated alongside this frame's movement input.
            if (std::abs(difference) <= 1024) offset = (offset + difference) & 4095;
        }
    };

    template<class T> struct NativeInput
    {
        T* held; T* pressed;
        T originalHeld, originalPressed;
        bool turning = false;
        uint32_t horizontalHeld = 0, horizontalPressed = 0;
        unsigned turnSteps = 0;
        NativeInput(T* held, T* pressed) : held(held), pressed(pressed), originalHeld(*held), originalPressed(*pressed) {}
        ~NativeInput() { *held = originalHeld; *pressed = originalPressed; }
        int16_t Accelerate(int16_t before, int16_t after, uint32_t nativeHeld, unsigned turnRate = 0) const
        {
            if (!turnSteps || !(nativeHeld & horizontalHeld)) return after;
            const int change = ((int(after) - int(before) + 2048) & 4095) - 2048;
            // The native handler decides whether this animation can turn and
            // supplies the sign/rate. Never replay the movement or weapon tick.
            if (!change || std::abs(change) > 128) return after;
            // Optional rate normalization also replaces the first native step,
            // so walking and standing can match aiming without changing keys.
            const int step = turnRate ? (change < 0 ? -int(turnRate) : int(turnRate)) : change;
            return int16_t((int(before) + step * int(turnSteps)) & 4095);
        }

        void Apply(bool allowed, bool aim, bool horizontal, float sensitivity, bool alternateMoving = false)
        {
            const auto command = Directions(allowed, aim, horizontal && (alternateMoving || !(*held & horizontalMask)), !(*held & verticalMask), sensitivity);
            turnSteps = command.turnSteps;
            horizontalHeld = command.held & horizontalMask;
            horizontalPressed = command.pressed & horizontalMask;
            turning = horizontalHeld && !alternateMoving;
            // Keep mouse turning out of the screen-relative direction vector.
            // Merge it after that vector has been mapped to forward movement.
            const auto mask = alternateMoving ? verticalMask : 15u;
            *held |= T(command.held & mask);
            *pressed |= T(command.pressed & mask & ~originalHeld);
        }
    };
}

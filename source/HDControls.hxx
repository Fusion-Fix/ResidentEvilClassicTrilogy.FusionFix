#pragma once

#include <cstdint>
#include <Windows.h>
#include <Xinput.h>
#include "MouseInput.hxx"

namespace HDControls
{
    // The first three bits also serve the Fusion Fix menu.
    enum Button : uint32_t { Start = 1, A = 2, B = 4, X = 8, Y = 16,
        LB = 32, RB = 64, R3 = 128, LT = 256, RT = 512 };

    inline uint32_t Buttons(const XINPUT_GAMEPAD& pad)
    {
        uint32_t result = 0;
        constexpr WORD native[] = { XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B,
            XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_LEFT_SHOULDER,
            XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_RIGHT_THUMB };
        for (unsigned i = 0; i < 8; ++i) if (pad.wButtons & native[i]) result |= 1u << i;
        if (pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) result |= LT;
        if (pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) result |= RT;
        return result;
    }

    struct State
    {
        uint32_t direction = 0;
        bool aim = false, attack = false, examine = false, run = false;
        bool target = false, reload = false, map = false, status = false, turn = false;
        bool confirm = false, cancel = false, options = false;
        bool Any() const
        {
            return direction || aim || attack || examine || run || target || reload
                || map || status || turn || confirm || cancel || options;
        }
    };

    inline State Read(uint32_t dpad, float x, float y, uint32_t buttons)
    {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        if (process != GetCurrentProcessId()) return {};
        const auto key = [](int code) { return (GetAsyncKeyState(code) & 0x8000) != 0; };
        State result;
        result.direction = dpad;
        if (!dpad)
            result.direction = uint32_t(y > 0.35f) | uint32_t(x > 0.35f) << 1
                | uint32_t(y < -0.35f) << 2 | uint32_t(x < -0.35f) << 3;
        if (key('W') || key(VK_UP)) result.direction |= 1;
        if (key('D') || key(VK_RIGHT)) result.direction |= 2;
        if (key('S') || key(VK_DOWN)) result.direction |= 4;
        if (key('A') || key(VK_LEFT)) result.direction |= 8;
        const auto mouse = MouseInput::buttons.load();
        result.aim = (mouse & 2) || key(VK_RBUTTON) || (buttons & LT);
        result.attack = (mouse & 1) || key(VK_LBUTTON) || (buttons & RT);
        result.examine = key('F') || (buttons & A);
        result.run = key(VK_SHIFT) || (buttons & X);
        result.target = key('C') || (buttons & LB);
        result.reload = key('R') || (buttons & B);
        result.map = key('M') || (buttons & RB);
        result.status = key('N') || (buttons & Y);
        result.turn = key('Q') || (buttons & R3)
            || ((buttons & X) && ((dpad & 4) || (!dpad && y < -0.35f)));
        result.confirm = key(VK_RETURN);
        result.cancel = key(VK_BACK) || (buttons & B);
        result.options = key(VK_ESCAPE) || (buttons & Start);
        return result;
    }

    inline uint16_t Native(const State& input, int game, bool menu = false, bool movie = false)
    {
        // All three engines expose a PlayStation-style packet before their
        // gameplay/menu translation. These are packet bits, not engine actions.
        if (movie)
        {
            // RE3 movies accept only the raw Start bit, unlike RE1/RE2.
            // A/B skip; Start/Escape remain reserved for the Fusion Fix menu.
            return !input.options && (input.confirm || input.examine || input.cancel)
                ? (game == 3 ? 0x10 : 0x80) : 0;
        }
        uint16_t result = uint16_t(input.direction << 12);
        if (menu)
        {
            // Native status/start bits can also mean confirm in classic menus.
            // Keep gameplay actions out of that translation entirely.
            if (input.confirm || input.examine) result |= 0x80;
            if (input.cancel || input.options) result |= 0x40;
            return result;
        }
        const bool aim = input.aim;
        if (aim) result |= 8;
        if (input.confirm || (aim ? input.attack : input.examine)) result |= 0x80;
        if (!aim && (input.run || input.cancel)) result |= 0x40;
        if (aim && input.target) result |= game == 1 ? 0x40 : 4;
        if (input.status) result |= 0x800;
        if (input.map) result |= game == 1 ? 0x900 : game == 2 ? 0x100 : 0x10;
        return result;
    }

    struct Actions
    {
        bool previousTurn = false, previousReload = false;
        bool turn = false, reload = false;
        void Update(const State& state)
        {
            turn = state.turn && !previousTurn;
            reload = state.reload && !previousReload;
            previousTurn = state.turn;
            previousReload = state.reload;
        }
    };

    // The first two games have no native quick-turn animation. Pivot in place
    // over eight simulation ticks, keeping collision and the player dispatcher.
    struct QuickTurn
    {
        int remaining = 0;
        int16_t angle = 0;
        bool Step(bool start, bool allowed, int16_t& yaw)
        {
            if (!allowed) { remaining = 0; return false; }
            if (start && !remaining) { angle = yaw; remaining = 8; }
            if (!remaining) return false;
            angle = int16_t((angle + 256) & 4095);
            yaw = angle;
            --remaining;
            return true;
        }
    };

    inline unsigned Transfer(unsigned capacity, unsigned loaded, unsigned spare)
    {
        return loaded < capacity ? (spare < capacity - loaded ? spare : capacity - loaded) : 0;
    }
}

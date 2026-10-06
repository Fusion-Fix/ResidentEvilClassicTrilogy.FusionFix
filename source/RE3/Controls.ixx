module;

#include <common.hxx>
#include <safetyhook.hpp>
#include "Presentation.hxx"
#include "HDControls.hxx"

export module Controls;

import common;
import Geometry;
import Game;
import Input;

namespace AlternateControls
{
    using WobbleFix::Read;
    using WobbleFix::Write;

    SafetyHookInline shMovement;
    Game::State game;
    Presentation::Heading heading;
    bool moving = false;
    int16_t requestedYaw = 0;
    int keyboardRunMode = 0;
    bool shiftHeld = false, runToggled = false, stickRunning = false;
    HDControls::Actions actions;
    uint8_t** inventory = nullptr;
    uint8_t* capacities = nullptr;
    int8_t(__cdecl* canReload)(int) = nullptr;

    bool ReloadAvailable()
    {
        if (!inventory || !*inventory || !canReload) return false;
        const auto* items = *inventory;
        const unsigned slot = items[296];
        if (slot >= 10) return false;
        return items[4 * slot + 1] < capacities[4 * items[4 * slot]] && canReload(0) == 0;
    }

    static uintptr_t __cdecl MovementHook(void* player)
    {
        keyboardRunMode = Game::GetSettings().keyboardRunMode.load(std::memory_order_relaxed);
        const auto held = *game.held;
        const auto pressed = *game.pressed;
        const auto pad = Input::GetPad();
        const bool shift = Input::ShiftHeld();
        const bool shiftPressed = shift && !shiftHeld;
        shiftHeld = shift;
        const bool tank = pad.connected && pad.dpad != 0;
        bool stick = false;
        auto direction = Presentation::Direction::Digital(held);
        if (pad.connected && pad.left.Moving())
        {
            direction = pad.left;
            stick = true;
        }
        else if (!pad.connected && *game.padType == 7)
        {
            const float x = (float(*game.stickX) - 128.0f) / 128.0f;
            const float y = (128.0f - float(*game.stickY)) / 128.0f;
            const auto analog = Presentation::Direction::Analog(x, y);
            if (analog.Moving())
            {
                direction = analog;
                stick = true;
            }
        }
        const auto routine = Read<uint8_t>(player, 5);
        if (player == game.player) actions.Update(Input::HDState());
        if (player == game.player && game.Controllable() && Game::GetSettings().hdControls && !Input::suppressUntilRelease)
        {
            if (actions.turn && routine <= 4 && !(held & 0x500))
            {
                // Use RE3's native quick-turn animation and its timing.
                Write(player, 4, uint32_t(0x601));
                heading.Reset(); moving = false;
                return shMovement.unsafe_ccall<uintptr_t>(player);
            }
            if (actions.reload && routine == 5 && Read<uint8_t>(player, 6) == 1
                && (held & 0x500) && ReloadAvailable()) Write(player, 6, uint16_t(4));
        }
        const bool pushing = routine == 9;
        const bool stairs = routine == 10 || routine == 14 || routine == 15;
        // Menus, scripted movement, the attract demo and aiming keep their native
        // input. Aim/backstep/dodge combinations are not remapped.
        if (!Game::Enabled(Game::Option::AlternateControls) || player != game.player || !game.Controllable()
            || (routine > 4 && !stairs && !pushing) || (held & 0x500) || tank)
        {
            heading.Reset();
            moving = false;
            stickRunning = false;
            if (!Game::Enabled(Game::Option::AlternateControls) || !game.Controllable())
                runToggled = false;
            if (tank && player == game.player && game.Controllable())
            {
                // The wrapper can merge the D-pad into stick axes. Physical
                // D-pad input wins and uses the original tank animation path.
                *game.held = (held & ~15u) | pad.dpad;
                *game.pressed = (pressed & ~15u) | pad.pressedDpad;
                const auto result = shMovement.unsafe_ccall<uintptr_t>(player);
                *game.held = held;
                *game.pressed = pressed;
                return result;
            }
            return shMovement.unsafe_ccall<uintptr_t>(player);
        }

        // Shift toggles only keyboard movement, including while standing still.
        if (!stick && (keyboardRunMode & 1) && shiftPressed)
            runToggled = !runToggled;
        bool run = (keyboardRunMode >= 2) != ((keyboardRunMode & 1) ? runToggled : shift);
        if (stick)
        {
            // After the radial dead zone, 0.70 corresponds to about 75% tilt.
            // Hysteresis prevents walk/run flickering near the boundary.
            const float length = std::hypot(direction.x, direction.y);
            stickRunning = length >= (stickRunning ? 0.65f : 0.70f);
            run = stickRunning || (Game::GetSettings().hdControls && (pad.buttons & HDControls::X));
        }
        else
            stickRunning = false;

        if (!direction.Moving())
        {
            heading.Reset();
            moving = false;
            stickRunning = false;
            *game.held = held & ~0x21Fu;
            *game.pressed = pressed & ~0x21Fu;
            const auto result = shMovement.unsafe_ccall<uintptr_t>(player);
            *game.held = held;
            *game.pressed = pressed;
            return result;
        }

        const auto yaw = heading.Update(direction, float(Read<int16_t>(game.view, 0)), float(Read<int16_t>(game.view, 4)));
        // Stair handlers still need remapped forward input on every frame.
        // Let the stair animation align the character while input is held.
        // Small analog noise must not restart that alignment each frame.
        // A deliberate direction change still steers, including reversing.
        const int difference = ((int(yaw) - int(requestedYaw) + 2048) & 4095) - 2048;
        if (!pushing && (!stairs || !moving || std::abs(difference) > 64))
        {
            Write(player, 110, yaw);
            requestedYaw = yaw;
        }
        // Leaving the native backwards/turn animation needs the usual idle
        // transition. Native code then selects walk/run and retains collision.
        if (routine == 3 || routine == 4)
            Write(player, 4, uint32_t(1));
        // The push handler (routine 9) consumes bit 0x10 independently
        // of walking. Do not overwrite its native alignment with stick yaw.
        const int pushDifference = ((int(yaw) - int(Read<int16_t>(player, 110)) + 2048) & 4095) - 2048;
        const bool forward = !pushing || std::abs(pushDifference) < 1024;
        *game.held = (held & ~0x21Fu) | (forward ? 0x11u : 0u) | (!pushing && run ? 0x200u : 0u);
        *game.pressed = (pressed & ~0x21Fu) | (forward && !moving ? 0x11u : 0u);
        moving = forward;
        const auto result = shMovement.unsafe_ccall<uintptr_t>(player);
        *game.held = held;
        *game.pressed = pressed;
        return result;
    }

    static void Init()
    {
        if (!Game::Resolve(game))
            return;
        auto reload = hook::pattern("51 A1 ? ? ? ? 80 B8 28 01 00 00 FF 75 04 0C FF 59 C3 8A 80 29 01 00 00");
        auto capacity = hook::pattern("8A 04 85 ? ? ? ? 2A C1 3A C2 88 44 24 0C");
        if (reload.size() == 1 && capacity.size() == 1)
        {
            inventory = *reload.get_first<uint8_t**>(2);
            capacities = *capacity.get_first<uint8_t*>(3);
            canReload = reinterpret_cast<decltype(canReload)>(reload.get_first());
        }
        CIniReader reader("");
        keyboardRunMode = std::clamp(reader.ReadInteger("MAIN", "KeyboardRunMode", 0), 0, 3);
        shMovement = safetyhook::create_inline(game.movement, MovementHook);
    }
}

class Controls
{
public:
    Controls()
    {
        FusionFix::onInitEvent() += []() { AlternateControls::Init(); };
    }
} Controls;

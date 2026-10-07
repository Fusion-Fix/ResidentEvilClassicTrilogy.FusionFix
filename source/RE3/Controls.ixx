module;

#include <common.hxx>
#include <safetyhook.hpp>
#include "Presentation.hxx"
#include "HDControls.hxx"
#include "../MouseInput.hxx"

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
    MouseInput::MovingHeading mouseHeading;
    bool moving = false;
    bool pushContact = false;
    int16_t contactYaw = 0;
    SafetyHookMid shPushContact;
    int16_t requestedBaseYaw = 0;
    bool stairMovement = false;
    bool defaultRun = false, shiftToggle = false;
    bool shiftHeld = false, runToggled = false, stickRunning = false;
    HDControls::Actions actions;
    uint8_t** inventory = nullptr;
    uint8_t* capacities = nullptr;
    int8_t(__cdecl* canReload)(int) = nullptr;
    const uint8_t* aimTurnRates = nullptr;

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
        const bool nextDefaultRun = Game::GetSettings().defaultRun.load(std::memory_order_relaxed);
        const bool nextShiftToggle = Game::GetSettings().shiftToggle.load(std::memory_order_relaxed);
        const bool runModeChanged = nextDefaultRun != defaultRun || nextShiftToggle != shiftToggle;
        defaultRun = nextDefaultRun; shiftToggle = nextShiftToggle;
        if (runModeChanged) runToggled = false;
        MouseInput::NativeInput mouse(game.held, game.pressed);
        const auto nativeMovement = [&]() -> uintptr_t
        {
            const auto beforeMouseTurn = Read<int16_t>(player, 110);
            const auto nativeHeld = *game.held;
            const auto beforeRoutine = Read<uint8_t>(player, 5);
            const bool regularTurn = beforeRoutine <= 4 && !(nativeHeld & 0x500);
            const auto result = shMovement.unsafe_ccall<uintptr_t>(player);
            const auto afterRoutine = Read<uint8_t>(player, 5);
            // RE3 normally turns faster while standing/walking than while
            // aiming. Use the equipped weapon's native aiming rate for mouse
            // steering in both states; leave physical controls untouched.
            const auto weapon = Read<uint8_t>(player, 70);
            const unsigned turnRate = regularTurn && Read<uint8_t>(player, 5) <= 4
                ? (aimTurnRates && weapon < 20 ? aimTurnRates[weapon] & 0x7Fu : 48u) : 0u;
            // A native stair/script transition can rotate the player in this
            // same tick. It must never be treated as a mouse turning increment.
            const bool mouseTurn = (beforeRoutine <= 4 && afterRoutine <= 4)
                || (beforeRoutine == 5 && afterRoutine == 5);
            if (mouse.horizontalHeld && mouseTurn && Read<uint8_t>(player, 4) == 1) Write(player, 110,
                mouse.Accelerate(beforeMouseTurn, Read<int16_t>(player, 110), nativeHeld, turnRate));
            return result;
        };
        auto held = *game.held;
        const auto pressed = *game.pressed;
        const bool contact = player == game.player && pushContact;
        if (player == game.player) pushContact = false;
        const auto pad = Input::GetPad();
        const bool shift = Input::ShiftHeld();
        const bool shiftPressed = shift && !shiftHeld && !runModeChanged;
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
        if (player == game.player)
        {
            const bool aim = (held & 0x500) && routine == 5;
            const bool allowed = Game::GetSettings().mouseSteering && game.Controllable()
                && !Input::suppressUntilRelease && !(*game.flags & 0x10000) && (routine <= 4 || aim);
            const bool alternateMoving = Game::Enabled(Game::Option::AlternateControls)
                && !aim && !tank && direction.Moving();
            mouse.Apply(allowed, aim, true, Game::GetSettings().mouseSensitivity, alternateMoving);
            held = *game.held;
        }
        if (player == game.player) actions.Update(Input::HDState());
        if (player == game.player && game.Controllable() && Game::GetSettings().hdControls && !Input::suppressUntilRelease)
        {
            if (actions.turn && routine <= 4 && !(held & 0x500))
            {
                // Use RE3's native quick-turn animation and its timing.
                Write(player, 4, uint32_t(0x601));
                heading.Reset(); mouseHeading.Reset(); moving = false;
                stairMovement = false;
                return nativeMovement();
            }
            if (actions.reload && routine == 5 && Read<uint8_t>(player, 6) == 1
                && (held & 0x500) && ReloadAvailable()) Write(player, 6, uint16_t(4));
        }
        const bool pushing = routine == 9;
        const bool stairs = routine == 10 || routine == 14 || routine == 15;
        // Menus, scripted movement, the attract demo and aiming keep their native
        // input. Aim/backstep/dodge combinations are not remapped.
        if (mouse.turning || !Game::Enabled(Game::Option::AlternateControls) || player != game.player || !game.Controllable()
            || (routine > 4 && !stairs && !pushing) || (held & 0x500) || tank)
        {
            heading.Reset(); mouseHeading.Reset();
            moving = false;
            stairMovement = false;
            stickRunning = false;
            if (!Game::Enabled(Game::Option::AlternateControls) || !game.Controllable())
                runToggled = false;
            if (tank && player == game.player && game.Controllable())
            {
                // The wrapper can merge the D-pad into stick axes. Physical
                // D-pad input wins and uses the original tank animation path.
                *game.held = (held & ~15u) | pad.dpad | mouse.horizontalHeld;
                *game.pressed = (pressed & ~15u) | pad.pressedDpad | mouse.horizontalPressed;
                const auto result = nativeMovement();
                *game.held = held;
                *game.pressed = pressed;
                return result;
            }
            return nativeMovement();
        }

        // Shift toggles only keyboard movement, including while standing still.
        if (!stick && shiftToggle && shiftPressed)
            runToggled = !runToggled;
        bool run = defaultRun != (shiftToggle ? runToggled : shift);
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
            heading.Reset(); mouseHeading.Reset();
            moving = false;
            stairMovement = false;
            stickRunning = false;
            *game.held = held & ~0x21Fu;
            *game.pressed = pressed & ~0x21Fu;
            const auto result = nativeMovement();
            *game.held = held;
            *game.pressed = pressed;
            return result;
        }

        const auto baseYaw = heading.Update(direction, float(Read<int16_t>(game.view, 0)), float(Read<int16_t>(game.view, 4)));
        if (stairMovement && !stairs && Game::GetSettings().mouseSteering)
            mouseHeading.Align(baseYaw, Read<int16_t>(player, 110));
        const auto yaw = mouseHeading.Prepare(baseYaw,
            Game::GetSettings().mouseSteering && !pushing && !stairs);
        // Only native pushable contact suppresses running. The original walk
        // handler then enters the push animation at the normal object speed.
        const int contactDifference = ((int(yaw) - int(contactYaw) + 2048) & 4095) - 2048;
        if (Game::Enabled(Game::Option::AutoPush) && contact && !stairs && std::abs(contactDifference) < 1024) run = false;
        // Stair handlers still need remapped forward input on every frame.
        // Let the stair animation align the character while input is held.
        // Small analog noise must not restart that alignment each frame.
        // A deliberate direction change still steers, including reversing.
        // Compare physical movement requests, not the disappearing mouse
        // offset, to distinguish a deliberate change from stair alignment.
        const int difference = ((int(baseYaw) - int(requestedBaseYaw) + 2048) & 4095) - 2048;
        if (!pushing && (!stairs || !moving || std::abs(difference) > 64))
        {
            Write(player, 110, yaw);
            requestedBaseYaw = baseYaw;
        }
        // Leaving the native backwards/turn animation needs the usual idle
        // transition. Native code then selects walk/run and retains collision.
        if (routine == 3 || routine == 4)
            Write(player, 4, uint32_t(1));
        // The push handler (routine 9) consumes bit 0x10 independently
        // of walking. Do not overwrite its native alignment with stick yaw.
        const int pushDifference = ((int(yaw) - int(Read<int16_t>(player, 110)) + 2048) & 4095) - 2048;
        const bool forward = !pushing || std::abs(pushDifference) < 1024;
        *game.held = (held & ~0x21Fu) | (forward ? 0x11u : 0u) | (!pushing && run ? 0x200u : 0u)
            | (!pushing && !stairs ? mouse.horizontalHeld : 0u);
        *game.pressed = (pressed & ~0x21Fu) | (forward && !moving ? 0x11u : 0u)
            | (!pushing && !stairs ? mouse.horizontalPressed : 0u);
        moving = forward;
        const auto result = nativeMovement();
        const auto nextRoutine = Read<uint8_t>(player, 5);
        stairMovement = stairs || nextRoutine == 10 || nextRoutine == 14 || nextRoutine == 15;
        mouseHeading.Record(Read<int16_t>(player, 110), mouse.horizontalHeld != 0 && nextRoutine <= 4);
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
        auto contact = hook::pattern("8A 91 C0 00 00 00 8D 81 C0 00 00 00 66 8B 71 3C FE C2 88 10 8A 18 66 8B 51 34");
        if (contact.size() == 1)
            shPushContact = safetyhook::create_mid(contact.get_first(), [](SafetyHookContext&)
            {
                // Native collision has already selected a single pushable
                // object on the player's floor and rejected disabled objects.
                if (!Game::Enabled(Game::Option::AutoPush) || !moving || Input::suppressUntilRelease || !game.Controllable()
                    || !Game::Enabled(Game::Option::AlternateControls)
                    || Read<uint8_t>(game.player, 5) > 2) return;
                pushContact = true;
                contactYaw = Read<int16_t>(game.player, 110);
            });
        defaultRun = Game::GetSettings().defaultRun;
        shiftToggle = Game::GetSettings().shiftToggle;
        auto aimRates = hook::pattern("8A 88 ? ? ? ? 80 E1 7F 8A D1 66");
        if (aimRates.size()) aimTurnRates = *aimRates.get_first<const uint8_t*>(2);
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

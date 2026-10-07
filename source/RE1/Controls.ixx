module;
#include <common.hxx>
#include <safetyhook.hpp>
#include "ClassicPresentation.hxx"
#include "HDControls.hxx"
#include "../MouseInput.hxx"

export module RE1Controls;
import common;
import ClassicGame;
import ClassicInput;
import ClassicMemory;

namespace RE1Controls
{
    using ClassicMemory::Read;
    using ClassicMemory::Write;
    SafetyHookInline shMovement, shPush;
    uint16_t* held = nullptr;
    uint16_t* pressed = nullptr;
    uint16_t* previousHeld = nullptr;
    uint16_t mappedHeld = 0;
    bool remapped = false;
    int16_t* yaw = nullptr;
    uint8_t* player = nullptr;
    uint32_t* control = nullptr;
    uint16_t* taskFlags = nullptr;
    void* view = nullptr;
    Presentation::Heading heading;
    MouseInput::MovingHeading mouseHeading;
    bool moving = false;
    HDControls::Actions actions;
    HDControls::QuickTurn quickTurn;
    SafetyHookInline shReload;
    SafetyHookMid shProfile;
    uint8_t** inventory = nullptr;
    uint8_t* equipped = nullptr;
    uint8_t* capacities = nullptr;
    void(__cdecl* compactInventory)() = nullptr;

    int AmmoSlot()
    {
        if (!inventory || !*inventory || !equipped || !*equipped || *equipped > 8
            || player[2] < 2 || player[2] > 5) return -1;
        const int slots = (player[1] & 3) == 1 ? 8 : 6;
        int best = -1;
        for (int i = 0; i < slots; ++i)
            if ((*inventory)[i * 2] == player[2] + 9 && (*inventory)[i * 2 + 1]
                && (best < 0 || (*inventory)[i * 2 + 1] > (*inventory)[best * 2 + 1])) best = i;
        return best;
    }

    int __cdecl Reload()
    {
        if (!ClassicGame::GetSettings().hdControls || player[2] < 2 || player[2] > 5)
            return shReload.unsafe_ccall<int>();
        const int slot = AmmoSlot();
        if (slot < 0) return 0;
        auto* items = *inventory;
        auto& loaded = items[2 * *equipped - 1];
        auto& spare = items[2 * slot + 1];
        const auto amount = HDControls::Transfer(capacities[4 * player[2]], loaded & 0x7F, spare);
        loaded = uint8_t((loaded & 0x7F) + amount);
        spare -= uint8_t(amount);
        if (!spare) { items[2 * slot] = 0; compactInventory(); }
        return 0;
    }

    void __cdecl Movement()
    {
        MouseInput::NativeInput mouse(held, pressed);
        auto originalHeld = *held; const auto originalPressed = *pressed;
        const auto originalPrevious = *previousHeld;
        bool mapInput = false, mouseMovement = false;
        const auto& pad = ClassicInput::pad;
        // The native dispatcher separates normal motion, scripted interactions,
        // weapons and damage into distinct branches.
        const bool normal = player[133] == 0 && player[134] <= 15
            && player[134] != 10 && player[134] != 11 && player[134] != 12;
        const bool running = player[133] == 1 && player[134] >= 13 && player[134] <= 15;
        // The room loop masks native buttons while scripts own the player.
        // Live controller input must obey that same permission before it can
        // replace the masked packet or change the player's heading/state.
        const bool controllable = (*taskFlags & 0x100) && !(player[3] & 0x20)
            && player[132] == 1 && !ClassicInput::gameInputSuppressed;
        const bool scripted = !controllable || (*control & 0x10000000) || player[133] >= 2;
        const bool aim = (originalHeld & 0x100) && player[133] == 3;
        const bool mouseAllowed = ClassicGame::GetSettings().mouseSteering && controllable
            && !(*control & 0x10000000) && (normal || running || aim)
            && !(ClassicInput::nativeMenu && ClassicInput::nativeMenu());
        const bool alternateMoving = ClassicGame::Enabled(ClassicGame::Option::AlternateControls)
            && !aim && !pad.dpad && ((originalHeld & 15) || pad.left.Moving());
        mouse.Apply(mouseAllowed, aim, true, ClassicGame::GetSettings().mouseSensitivity, alternateMoving);
        originalHeld = *held;
        actions.Update(ClassicInput::HDState());
        const bool hd = ClassicGame::GetSettings().hdControls && !ClassicInput::gameInputSuppressed;
        if (hd && controllable && actions.reload && player[133] == 3 && player[134] == 19
            && (originalHeld & 0x100) && !(*control & 0x10000000) && AmmoSlot() >= 0
            && ((*inventory)[2 * *equipped - 1] & 0x7F) < capacities[4 * player[2]])
            Write(player, 134, uint16_t(24));
        const bool turning = quickTurn.Step(hd && actions.turn,
            hd && player[132] == 1 && !scripted && (normal || running) && !(originalHeld & 0x100), *yaw);
        if (turning)
        {
            Write(player, 133, uint8_t(0));
            Write(player, 134, uint16_t(0));
            *held &= ~0x20Fu; *pressed &= ~0x20Fu;
            heading.Reset(); mouseHeading.Reset(); moving = false; mapInput = true;
        }
        else if (!scripted && pad.dpad)
        {
            *held = (originalHeld & ~15u) | pad.dpad | mouse.horizontalHeld;
            *pressed = (originalPressed & ~15u) | pad.pressedDpad | mouse.horizontalPressed;
            mapInput = true;
            heading.Reset(); mouseHeading.Reset(); moving = false;
        }
        else if (!mouse.turning && !scripted && ClassicGame::Enabled(ClassicGame::Option::AlternateControls)
            && (normal || running) && !(originalHeld & 0x100))
        {
            const bool stick = pad.connected && pad.left.Moving();
            const auto direction = stick ? pad.left : Presentation::Direction::Digital(originalHeld);
            const bool run = ClassicInput::Run(direction, stick);
            mapInput = true;
            *held = originalHeld & ~0x20Fu;
            *pressed = originalPressed & ~0x20Fu;
            if (direction.Moving())
            {
                const auto nextYaw = mouseHeading.Prepare(heading.Update(direction, float(Read<int16_t>(view)), float(Read<int16_t>(view, 4))),
                    ClassicGame::GetSettings().mouseSteering);
                mouseMovement = true;
                *yaw = nextYaw;
                *held |= 1u | (run ? 0x200u : 0u) | mouse.horizontalHeld;
                *pressed |= mouse.horizontalPressed;
                if (!moving) *pressed |= 1;
                moving = true;
            }
            else { heading.Reset(); mouseHeading.Reset(); moving = false; }
        }
        else { heading.Reset(); mouseHeading.Reset(); moving = false; }
        // The walk dispatcher compares against the previous logical direction
        // before selecting its animation. Raw left/right input would otherwise
        // restart screen-relative forward motion on every frame.
        if (mapInput && remapped) *previousHeld = (originalPrevious & ~0x20Fu) | (mappedHeld & 0x20Fu);
        mappedHeld = *held;
        remapped = mapInput;
        const auto beforeMouseTurn = *yaw;
        const auto nativeHeld = *held;
        shMovement.unsafe_ccall<void>();
        *yaw = mouse.Accelerate(beforeMouseTurn, *yaw, nativeHeld);
        if (mouseMovement) mouseHeading.Record(*yaw, mouse.horizontalHeld != 0);
        *held = originalHeld;
        *pressed = originalPressed;
        *previousHeld = originalPrevious;
    }

    uintptr_t __cdecl Push()
    {
        const auto original = *held;
        // Object collision runs after Movement has restored the physical input.
        // Feed it the same logical direction used to move the player this frame.
        if (remapped && ClassicGame::Enabled(ClassicGame::Option::AlternateControls)
            && !ClassicInput::gameInputSuppressed && !(*control & 0x10000000)
            && (*taskFlags & 0x100) && !(player[3] & 0x20) && player[132] == 1 && player[133] < 2)
            *held = (original & ~15u) | (mappedHeld & 15u);
        const auto result = shPush.unsafe_ccall<uintptr_t>();
        *held = original;
        return result;
    }

    void Init()
    {
        auto movement = hook::pattern("66 83 3D ? ? ? ? 00 7D ? 66 81 3D ? ? ? ? FF 7F 75 ? 66 81 05 ? ? ? ? 00 08 C7 05");
        auto buttons = hook::pattern("33 C0 66 A1 ? ? ? ? 25 C0 00 00 00 3D 80 00 00 00 74 ? 3D C0 00 00 00");
        auto actor = hook::pattern("C7 05 ? ? ? ? ? ? ? ? F6 05 ? ? ? ? 01 74 ? 33 C0 A0 ? ? ? ? FF 14 85");
        auto matrix = hook::pattern("68 ? ? ? ? 03 C2 68 ? ? ? ? C1 F8 02 8D 4C 24 ? 66 89 44 24 ? 51 E8");
        auto scripted = hook::pattern("F6 05 ? ? ? ? 10 66 8B 0D ? ? ? ? 89 15 ? ? ? ? 66 A3 ? ? ? ? 66 89 0D");
        auto permission = hook::pattern("F6 05 ? ? ? ? 20 75 ? F6 05 ? ? ? ? 01 75 ? B8 00 C0 00 00 66 21 05 ? ? ? ? 66 21 05");
        if (movement.size() != 1 || buttons.size() != 1 || actor.size() != 1 || matrix.size() != 1 || scripted.size() != 1 || permission.size() != 1) return;
        held = *buttons.get_first<uint16_t*>(4);
        pressed = held + 1;
        yaw = *movement.get_first<int16_t*>(24);
        player = *actor.get_first<uint8_t*>(6);
        view = *matrix.get_first<void*>(1);
        control = reinterpret_cast<uint32_t*>(*scripted.get_first<uint8_t*>(2) - 3);
        taskFlags = reinterpret_cast<uint16_t*>(*permission.get_first<uint8_t*>(11) - 1);
        previousHeld = *scripted.get_first<uint16_t*>(22);
        auto reload = hook::pattern("83 EC 04 A0 ? ? ? ? 04 09 33 C9 88 44 24 00 53 33 C0 88 4C 24 07 A0 ? ? ? ? 8A 14 85");
        if (reload.size() == 1)
        {
            auto* code = reload.get_first<uint8_t>();
            capacities = Read<uint8_t*>(code, 0x1F);
            inventory = Read<uint8_t**>(code, 0x45);
            equipped = Read<uint8_t*>(code, 0x79);
            compactInventory = reinterpret_cast<decltype(compactInventory)>(code + 0xC8 + Read<int32_t>(code, 0xC4));
            shReload = safetyhook::create_inline(code, Reload);
        }
        auto mapping = hook::pattern("8B 34 8D ? ? ? ? B1 10 FE C9 33 C0 8A C1 66 8B 04 46");
        if (mapping.size() == 1)
        {
            // Select the profile before loading its table. The following
            // instructions include the native loop's backward branch target.
            shProfile = safetyhook::create_mid(mapping.get_first(), [](SafetyHookContext& ctx)
            {
                if (ClassicGame::GetSettings().hdControls && !(*control & 0x10000000)) ctx.ecx = 0;
            });
        }
        shMovement = safetyhook::create_inline(movement.get_first(), Movement);
        auto push = hook::pattern("83 EC 14 A1 ? ? ? ? C7 44 24 04 00 00 00 00 C7 44 24 00 00 00 00 00 80 78 02 00 53 56 57 55");
        if (push.size() == 1) shPush = safetyhook::create_inline(push.get_first(), Push);
    }
}

class Controls
{
public:
    Controls() { FusionFix::onInitEvent() += []() { RE1Controls::Init(); }; }
} Controls;

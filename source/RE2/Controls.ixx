module;
#include <common.hxx>
#include <safetyhook.hpp>
#include "ClassicPresentation.hxx"
#include "HDControls.hxx"

export module RE2Controls;
import common;
import ClassicGame;
import ClassicInput;
import ClassicMemory;
import ClassicMenu;

namespace RE2Controls
{
    using ClassicMemory::Read;
    using ClassicMemory::Write;
    SafetyHookInline shMovement, shInput, shLegacyMap;
    bool waitForInputRelease = false;
    uint32_t* legacyInput = nullptr;
    uint32_t legacyPacket = 0;

    int __cdecl LegacyMap(int input, int device)
    {
        const auto result = shLegacyMap.unsafe_ccall<int>(input, device);
        if (device == 1) legacyPacket |= uint32_t(result);
        return result;
    }

    int __cdecl Input()
    {
        legacyPacket = 0;
        const auto native = shInput.unsafe_ccall<int>();
        const auto input = ClassicInput::HDPacket(2, uint16_t(native), legacyInput ? uint16_t(*legacyInput) : uint16_t(legacyPacket));
        ClassicInput::gameInputSuppressed = ClassicMenu::opened || waitForInputRelease;
        if (ClassicMenu::opened) return 0;
        if (waitForInputRelease)
        {
            if (!input && !ClassicInput::HDState().Any()) waitForInputRelease = false;
            return 0;
        }
        return input;
    }
    uint32_t* held = nullptr;
    uint32_t* pressed = nullptr;
    void** game = nullptr;
    uint32_t* control = nullptr;
    void* view = nullptr;
    Presentation::Heading heading;
    bool moving = false;
    HDControls::Actions actions;
    HDControls::QuickTurn quickTurn;
    SafetyHookInline shReload;
    SafetyHookMid shProfile;
    uint8_t* inventory = nullptr;
    uint8_t* equipped = nullptr;
    uint8_t* capacities = nullptr;
    uint8_t** ammoTypes = nullptr;
    int(__cdecl* findItem)(int) = nullptr;
    void(__cdecl* compactInventory)() = nullptr;
    int(__cdecl* setCount)(int, int) = nullptr;

    int AmmoSlot()
    {
        if (!inventory || !equipped || *equipped >= 10) return -1;
        const auto weapon = inventory[4 * *equipped];
        // Match the native reload exclusions: knife, fuel/electric weapons,
        // and the four-shot rocket launcher do not use this animation.
        if (weapon < 2 || weapon > 19 || (weapon >= 9 && weapon <= 11) || weapon == 17) return -1;
        if (!ammoTypes[2 * weapon]) return -1;
        return findItem(*ammoTypes[2 * weapon]);
    }

    int __cdecl Reload()
    {
        if (!ClassicGame::GetSettings().hdControls) return shReload.unsafe_ccall<int>();
        const int slot = AmmoSlot();
        if (slot < 0 || slot >= 10) return 0;
        const auto weapon = inventory[4 * *equipped];
        auto& spare = inventory[4 * slot + 1];
        const auto amount = HDControls::Transfer(capacities[8 * weapon], inventory[4 * *equipped + 1], spare);
        spare -= uint8_t(amount);
        if (!spare) { inventory[4 * slot] = 0; compactInventory(); }
        return setCount(*equipped, inventory[4 * *equipped + 1] + amount);
    }

    uintptr_t __cdecl Movement(void* player)
    {
        const auto originalHeld = *held, originalPressed = *pressed;
        const auto& pad = ClassicInput::pad;
        const bool ownPlayer = *game && player == static_cast<uint8_t*>(*game) + 14864 && !(*control & 0x2000);
        const auto routine = Read<uint8_t>(player, 5);
        const bool recovering = routine == 9 && Read<uint8_t>(player, 7) > 4;
        if (ownPlayer) actions.Update(ClassicInput::HDState());
        const bool hd = ClassicGame::GetSettings().hdControls && !ClassicInput::gameInputSuppressed;
        if (hd && ownPlayer && actions.reload && Read<uint8_t>(player, 4) == 1
            && routine == 5 && Read<uint8_t>(player, 6) == 1 && (originalHeld & 0x100)
            && AmmoSlot() >= 0 && inventory[4 * *equipped + 1] < capacities[8 * inventory[4 * *equipped]])
            Write(player, 6, uint16_t(4));
        auto yaw = Read<int16_t>(player, 118);
        const bool turning = ownPlayer && quickTurn.Step(hd && actions.turn,
            hd && Read<uint8_t>(player, 4) == 1 && routine <= 4 && !(originalHeld & 0x100), yaw);
        if (turning)
        {
            Write(player, 118, yaw); Write(player, 4, uint32_t(1));
            *held &= ~0x20Fu; *pressed &= ~0x20Fu;
            heading.Reset(); moving = false;
        }
        else if (ownPlayer && pad.dpad)
        {
            *held = (originalHeld & ~15u) | pad.dpad;
            *pressed = (originalPressed & ~15u) | pad.pressedDpad;
            heading.Reset(); moving = false;
        }
        else if (ownPlayer && ClassicGame::Enabled(ClassicGame::Option::AlternateControls)
            && Read<uint8_t>(player, 4) == 1 && (routine <= 4 || recovering) && !(originalHeld & 0x100))
        {
            const bool stick = pad.connected && pad.left.Moving();
            const auto direction = stick ? pad.left : Presentation::Direction::Digital(originalHeld);
            const bool run = ClassicInput::Run(direction, stick);
            *held = originalHeld & ~0x20Fu;
            *pressed = originalPressed & ~0x20Fu;
            if (direction.Moving())
            {
                const auto yaw = heading.Update(direction, float(Read<int16_t>(view)), float(Read<int16_t>(view, 4)));
                Write(player, 118, yaw);
                if (routine == 3 || routine == 4) Write(player, 4, uint32_t(1));
                *held |= 1u | (run ? 0x200u : 0u);
                if (!moving) *pressed |= 1;
                moving = true;
            }
            else { heading.Reset(); moving = false; }
        }
        else { heading.Reset(); moving = false; }
        const auto result = shMovement.unsafe_ccall<uintptr_t>(player);
        *held = originalHeld;
        *pressed = originalPressed;
        return result;
    }

    void Init()
    {
        auto input = hook::pattern("B9 ? ? ? ? E8 ? ? ? ? A1 ? ? ? ? C7 05 ? ? ? ? 00 00 00 00 85 C0 74 21 A1 ? ? ? ? 6A 00 50 E8");
        const bool recordedLegacy = input.size() == 1;
        if (!recordedLegacy)
            input = hook::pattern("B9 ? ? ? ? E8 ? ? ? ? A1 ? ? ? ? 85 C0 74 15 A1 ? ? ? ? 6A 00 50 E8 ? ? ? ? 83 C4 08 A3 ? ? ? ? 83 3D ? ? ? ? 02 7C 27");
        if (input.size() == 1)
        {
            if (recordedLegacy) legacyInput = *input.get_first<uint32_t*>(0x45);
            else
            {
                // French/Japanese do not retain the mapped legacy-pad packet.
                // Capture device 1 after its native mapping, excluding keyboard input.
                auto* code = input.get_first<uint8_t>();
                shLegacyMap = safetyhook::create_inline(code + 0x20 + Read<int32_t>(code, 0x1C), LegacyMap);
            }
            shInput = safetyhook::create_inline(input.get_first(), Input);
            ClassicMenu::pause.emplace_back([](bool) { waitForInputRelease = true; });
        }
        auto movement = hook::pattern("8B 0D ? ? ? ? 8B 15 ? ? ? ? 56 8B 74 24 08 33 C0 51 8A 46 05 52 56 FF 14 85");
        auto matrix = hook::pattern("BF ? ? ? ? F3 A5 8B 4C 24 4C 8B 70 04 8B 18 8B 69 04");
        auto player = hook::pattern("A1 ? ? ? ? 83 C7 04 3B B8 4C 21 00 00 75 ? 8D B0 10 3A 00 00");
        auto gameplay = hook::pattern("A0 ? ? ? ? 53 55 8B 2D ? ? ? ? 56 57 A8 02 74 ? 8B C5 24 01 F6 D8 1B C0");
        if (movement.size() != 1 || matrix.size() != 1 || player.size() != 1 || gameplay.size() != 1) return;
        pressed = *movement.get_first<uint32_t*>(2);
        held = *movement.get_first<uint32_t*>(8);
        view = *matrix.get_first<void*>(1);
        game = *player.get_first<void**>(1);
        control = *gameplay.get_first<uint32_t*>(9);
        auto reload = hook::pattern("A1 ? ? ? ? 33 C9 25 FF 00 00 00 53 33 DB 8A 0C 85 ? ? ? ? 8B C1 C1 E0 03 8A 98 ? ? ? ? 8B 90");
        if (reload.size() == 1)
        {
            auto* code = reload.get_first<uint8_t>();
            equipped = Read<uint8_t*>(code, 1);
            inventory = Read<uint8_t*>(code, 0x12);
            capacities = Read<uint8_t*>(code, 0x1D);
            ammoTypes = Read<uint8_t**>(code, 0x23);
            findItem = reinterpret_cast<decltype(findItem)>(code + 0x31 + Read<int32_t>(code, 0x2D));
            compactInventory = reinterpret_cast<decltype(compactInventory)>(code + 0x61 + Read<int32_t>(code, 0x5D));
            setCount = reinterpret_cast<decltype(setCount)>(code + 0x7D + Read<int32_t>(code, 0x79));
            shReload = safetyhook::create_inline(code, Reload);
        }
        auto mapping = hook::pattern("81 C2 ? ? ? ? 89 4C 24 10 83 EA 02 33 DB 49 66 8B 1A 85 C3");
        if (mapping.size() == 1)
        {
            // Hook the profile-offset addition, before the mapping loop.
            // The following instructions contain the loop's backward target;
            // detouring those would make later iterations enter our jump.
            shProfile = safetyhook::create_mid(mapping.get_first(), [](SafetyHookContext& ctx)
            {
                if (ClassicGame::GetSettings().hdControls && !(*control & 0x2000)) ctx.edx = 0;
            });
        }
        auto menu = hook::pattern("8B 0D ? ? ? ? 89 35 ? ? ? ? F7 C1 00 00 00 01 74 14 81 E6 00 3C 00 00");
        auto tasks = hook::pattern("8D 04 D2 33 C9 C1 E0 02 66 8B 88 ? ? ? ? 8D B0 ? ? ? ? 49");
        auto titleTask = hook::pattern("A0 ? ? ? ? C6 05 ? ? ? ? 01 A8 40 74 12 A8 08 75 12 24 BF");
        auto saveTask = hook::pattern("8B 0D ? ? ? ? 33 C0 8A 41 08 83 E8 00 74 04 48 74 1A C3 6A 00 E8");
        if (menu.size() == 1)
        {
            auto* flags = *menu.get_first<uint32_t*>(2) - 1;
            ClassicInput::nativeMenu = [flags]() { return (*flags & 0x8000) != 0; };
            if (tasks.size() == 1 && titleTask.size() == 1 && saveTask.size() == 1)
            {
                auto* table = *tasks.get_first<uint8_t*>(11);
                const auto title = uintptr_t(titleTask.get_first());
                const auto save = uintptr_t(saveTask.get_first());
                ClassicInput::nativeActionMenu = [flags, table, title, save]()
                {
                    if (*flags & 0x8000) return true;
                    // Read active scheduled tasks, not the title's last state:
                    // that state remains allocated after entering gameplay.
                    for (int slot = 0; slot < 8; ++slot)
                    {
                        const auto* task = table + 36 * slot;
                        const auto callback = Read<uintptr_t>(task, 4);
                        if (Read<uint16_t>(task) && (callback == title || callback == save)) return true;
                    }
                    return false;
                };
            }
        }
        shMovement = safetyhook::create_inline(movement.get_first(), Movement);
    }
}

class Controls
{
public:
    Controls() { FusionFix::onInitEvent() += []() { RE2Controls::Init(); }; }
} Controls;

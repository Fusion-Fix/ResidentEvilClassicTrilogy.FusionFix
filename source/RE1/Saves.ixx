module;
#include <common.hxx>
#include <safetyhook.hpp>

export module RE1Saves;
import common;
import ClassicGame;
import ClassicMemory;
import ClassicMenu;

export namespace RE1Saves
{
    using ClassicMemory::Read;
    using ClassicMemory::Write;
    SafetyHookMid shSlots;
    SafetyHookInline shCaptions;
    uint8_t* title = nullptr;
    uint32_t* pressed = nullptr;
    uint32_t* flags = nullptr;
    uint32_t* control = nullptr;
    int(__cdecl* taskExit)() = nullptr;
    int requested = -1, pending = -1;
    bool autoAttempted = false;
    std::filesystem::path directory;

    bool CanLoad() { return title && title[0] == 1 && title[1] == 2; }

    std::filesystem::path File(int slot) { return directory / (L"savedat" + std::to_wstring(slot + 1) + L".dat"); }

    bool HasSave()
    {
        for (int slot = 0; slot < 8; ++slot)
            if (GetFileAttributesW(File(slot).c_str()) != INVALID_FILE_ATTRIBUTES) return true;
        return false;
    }

    void Title()
    {
        if (!CanLoad()) return;
        if (!autoAttempted)
        {
            autoAttempted = true;
            if (ClassicGame::Enabled(ClassicGame::Option::AutoLoad) && HasSave())
                requested = ClassicGame::GetSettings().loadSlot;
        }
        if (requested < 0) return;
        pending = requested; requested = -1;
        title[-1] = 2;
        *pressed |= 1; // Confirm the native title menu's Load Game action.
    }

    void Slots(SafetyHookContext& registers)
    {
        const auto* stack = reinterpret_cast<uint8_t*>(registers.esp);
        if (pending < 0 || registers.ebp != 1 || Read<int>(stack, 0x17E8) != 1) return;
        int selected = -1;
        uint64_t latest = 0;
        for (int slot = 0; slot < 8; ++slot)
        {
            if (!Read<int>(stack, 0x144 + slot * 20)) continue;
            if (pending > 0) { if (slot == pending - 1) selected = slot; continue; }
            WIN32_FILE_ATTRIBUTE_DATA attributes{};
            if (!GetFileAttributesExW(File(slot).c_str(), GetFileExInfoStandard, &attributes)) continue;
            const auto time = uint64_t(attributes.ftLastWriteTime.dwHighDateTime) << 32 | attributes.ftLastWriteTime.dwLowDateTime;
            if (selected < 0 || time > latest) { selected = slot; latest = time; }
        }
        pending = -1;
        if (selected < 0) return; // Keep the native picker for missing or unreadable saves.
        Write(reinterpret_cast<void*>(registers.esp), 0x14, selected);
        registers.ebp = 4; // Native validated load, restoration and error handling.
    }

    int __cdecl Captions()
    {
        if (ClassicGame::Enabled(ClassicGame::Option::FastLoad) && (*flags & 0x10000000) && !(*control & 0x10000000))
            return taskExit();
        return shCaptions.unsafe_ccall<int>();
    }

    void Init()
    {
        auto state = hook::pattern("A0 ? ? ? ? 85 C0 74 ? 83 F8 01 0F 84 ? ? ? ? 5E 83 C4 04 C3 33 C0 A0");
        auto input = hook::pattern("F7 05 ? ? ? ? FF 0E 00 00 75 ? 85 F6 75 ? F6 05 ? ? ? ? 51 74 ? F6 05");
        auto slots = hook::pattern("83 FD 09 0F 87 ? ? ? ? FF 24 AD ? ? ? ? BD 01 00 00 00 33 F6 8D BC 24 44 01 00 00");
        auto captions = hook::pattern("F6 05 ? ? ? ? 10 74 11 6A 00 6A 5D E8 ? ? ? ? 83 C4 08 E9");
        auto loaded = hook::pattern("F6 05 ? ? ? ? 10 6A 00 74 0F 6A 5C E8");
        if (state.size() != 1 || input.size() != 1 || slots.size() != 1 || captions.size() != 1 || loaded.size() != 1) return;
        title = *state.get_first<uint8_t*>(1);
        pressed = *input.get_first<uint32_t*>(2);
        control = reinterpret_cast<uint32_t*>(*captions.get_first<uint8_t*>(2) - 3);
        flags = reinterpret_cast<uint32_t*>(*loaded.get_first<uint8_t*>(2) - 3);
        taskExit = reinterpret_cast<decltype(taskExit)>(injector::GetBranchDestination(captions.get_first(21)).as_int());
        wchar_t executable[MAX_PATH]{};
        GetModuleFileNameW(nullptr, executable, MAX_PATH);
        directory = std::filesystem::path(executable).parent_path() / L"SAVE";
        ClassicMenu::maxSlot = 8;
        ClassicMenu::canLoad = CanLoad;
        ClassicMenu::load = [](int slot) { if (CanLoad()) requested = slot; };
        shSlots = safetyhook::create_mid(slots.get_first(), Slots);
        shCaptions = safetyhook::create_inline(captions.get_first(), Captions);
    }
}

class Saves
{
public:
    Saves() { FusionFix::onInitEvent() += []() { RE1Saves::Init(); }; }
} Saves;

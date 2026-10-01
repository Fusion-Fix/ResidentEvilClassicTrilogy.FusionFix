module;
#include <common.hxx>
#include <safetyhook.hpp>

export module RE2Saves;
import common;
import ClassicGame;
import ClassicMemory;
import ClassicMenu;
import PortableSettings;

namespace RE2Saves
{
    using ClassicMemory::Read;
    SafetyHookInline shTitle, shSlots, shCaptions;
    uint8_t** task = nullptr;
    uint16_t* title = nullptr;
    uint32_t* flags = nullptr;
    uint8_t* mode = nullptr;
    uint8_t* loading = nullptr;
    uint8_t** entries = nullptr;
    int* count = nullptr;
    int* chosen = nullptr;
    char** filename = nullptr;
    int(__cdecl* taskExit)() = nullptr;
    int requested = -1, pending = -1;
    bool autoAttempted = false;
    uint8_t incompatibleCampaign = 1;
    std::filesystem::path directory;

    int AvailableSlots()
    {
        WIN32_FIND_DATAW file{};
        const auto search = FindFirstFileW((directory / L"*.RESIDENT2").c_str(), &file);
        if (search == INVALID_HANDLE_VALUE) return 0;
        int result = 0;
        do { if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) ++result; }
        while (result < 65535 && FindNextFileW(search, &file));
        FindClose(search);
        return result;
    }

    bool CanLoad() { return title && *title == 0x0102; }

    int __cdecl Title()
    {
        if (CanLoad() && !autoAttempted)
        {
            autoAttempted = true;
            if (ClassicGame::Enabled(ClassicGame::Option::AutoLoad) && AvailableSlots()) requested = ClassicGame::GetSettings().loadSlot;
        }
        if (CanLoad() && requested >= 0)
        {
            pending = requested; requested = -1;
            *title = 0x0202;
            (*task)[9] = 0;
        }
        return shTitle.unsafe_ccall<int>();
    }

    int __cdecl Slots()
    {
        const auto result = shSlots.unsafe_ccall<int>();
        if (pending < 0 || *loading != 1 || *mode != 2) return result;
        if (!*entries) { pending = -1; return result; }
        int selected = -1;
        for (int slot = 0; slot < *count; ++slot)
        {
            const auto* entry = *entries + 276 * slot;
            // The native loader rejects the other character's campaign saves.
            if (!entry[273] && (entry[260] & 1) == incompatibleCampaign) continue;
            if (!pending || slot == pending - 1) { selected = slot; break; }
        }
        pending = -1;
        if (selected < 0) return result;
        *chosen = selected;
        *filename = reinterpret_cast<char*>(*entries + 276 * selected);
        *mode = 10; // Use the game's validated load and restore path.
        return result;
    }

    int __cdecl Captions()
    {
        if (ClassicGame::Enabled(ClassicGame::Option::FastLoad) && !(*flags & 0x01022008))
            return taskExit();
        return shCaptions.unsafe_ccall<int>();
    }

    void Init()
    {
        auto menu = hook::pattern("33 C0 A0 ? ? ? ? FF 24 85 ? ? ? ? 90 90 6A 12 E8");
        auto slots = hook::pattern("8B 0D ? ? ? ? 81 EC 08 01 00 00 33 C0 8A 41 09 53 55 33 ED 56 83 F8 04 57 77 3A FF 24 85");
        auto enumeration = hook::pattern("A1 ? ? ? ? 68 ? ? ? ? 52 68 ? ? ? ? 50 68 ? ? ? ? E8 ? ? ? ? A0");
        auto selection = hook::pattern("8B 0D ? ? ? ? 8D 04 49 C1 E0 03 2B C1 8B 0D ? ? ? ? 8D 04 40 8D 14 81 8A 84 81 11 01 00 00");
        auto loadMode = hook::pattern("80 E1 01 80 E4 FB 88 0D ? ? ? ? A3 ? ? ? ? E8");
        auto name = hook::pattern("C7 05 ? ? ? ? ? ? ? ? E9 ? ? ? ? 8A 0D ? ? ? ? 6A 00 FE C9 68 00 00 06 04 F6 D9");
        auto captions = hook::pattern("8B 0D ? ? ? ? 33 C0 8A 41 08 83 E8 00 74 04 48 74 1A C3 E8");
        auto exit = hook::pattern("A1 ? ? ? ? 8D 04 C0 C1 E0 02 66 C7 80 ? ? ? ? 00 00 C7 80 ? ? ? ? ? ? ? ? C6 80 ? ? ? ? 01 C3");
        auto campaign = hook::pattern("8A 82 04 01 00 00 24 01 3C ? 75 12 C6 05");
        auto claire = hook::pattern("F6 82 04 01 00 00 01 75 16 C6 05 ? ? ? ? 62");
        if (menu.size() != 1 || slots.size() != 1 || enumeration.size() != 1 || selection.size() != 1
            || loadMode.size() != 1 || name.size() != 1 || captions.size() != 1 || exit.size() != 1) return;
        title = reinterpret_cast<uint16_t*>(*menu.get_first<uint8_t*>(3) - 1);
        task = *slots.get_first<uint8_t**>(2);
        mode = *slots.get_first<uint8_t*>(43);
        count = *enumeration.get_first<int*>(1);
        entries = *enumeration.get_first<uint8_t**>(12);
        chosen = *selection.get_first<int*>(2);
        loading = *loadMode.get_first<uint8_t*>(8);
        flags = *loadMode.get_first<uint32_t*>(13);
        filename = *name.get_first<char**>(2);
        // Read the same campaign test as the native loader (Leon/Claire).
        if (campaign.size() == 1) incompatibleCampaign = *campaign.get_first<uint8_t>(9);
        else if (claire.size() == 1) incompatibleCampaign = 0;
        else return;
        directory = PortableSettings::SaveDirectory();
        taskExit = reinterpret_cast<decltype(taskExit)>(exit.get_first());
        ClassicMenu::canLoad = CanLoad;
        ClassicMenu::availableSlots = AvailableSlots;
        ClassicMenu::load = [](int slot) { if (CanLoad()) requested = slot; };
        shTitle = safetyhook::create_inline(menu.get_first(), Title);
        shSlots = safetyhook::create_inline(slots.get_first(), Slots);
        shCaptions = safetyhook::create_inline(captions.get_first(), Captions);
    }
}

class Saves
{
public:
    Saves() { FusionFix::onInitEvent() += []() { RE2Saves::Init(); }; }
} Saves;

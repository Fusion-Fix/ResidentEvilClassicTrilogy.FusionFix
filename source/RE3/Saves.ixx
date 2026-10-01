module;
#include <common.hxx>
#include <safetyhook.hpp>
#include <fstream>
#include <vector>
#include <atomic>

export module Saves;
import common;
import Game;
import Geometry;

export namespace Saves
{
    std::atomic<bool> atTitle = false;
    std::atomic<int> requested = -1;
    bool CanLoad();
    void Request(int slot) { if (CanLoad()) requested = std::clamp(slot, 0, 30); }
}

namespace SaveHooks
{
    using WobbleFix::Read;
    SafetyHookInline shTitle, shCard, shSlots, shSave, shCaptions;
    Game::State game;
    void(__cdecl* taskExit)() = nullptr;
    uint8_t* captionBusy = nullptr;
    uint8_t* captionKind = nullptr;
    bool autoAttempted = false, scanning = false;
    int pending = -1, scannedCard = -1;

    struct Slot { int number; uint32_t hash; uint64_t time; };
    std::vector<Slot> slots;

    uint32_t Hash(const uint8_t* bytes)
    {
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < 128; ++i)
            hash = (hash ^ bytes[i]) * 16777619u;
        return hash;
    }

    std::string SlotKey(int slot) { return "Slot" + std::to_string(slot); }

    uint64_t SaveTime(int slot, uint32_t hash)
    {
        CIniReader reader("");
        const auto value = reader.ReadString("SAVES", SlotKey(slot), "");
        uint64_t time = 0;
        uint32_t recorded = 0;
        if (sscanf(value.c_str(), "%llu:%u", &time, &recorded) == 2 && recorded == hash)
            return time;
        return 0;
    }

    void Record(void* context)
    {
        const int slot = Read<uint8_t>(context, 14) * 15 + Read<uint8_t>(context, 4) + 1;
        const auto hash = Hash(static_cast<uint8_t*>(context) + 2880 + Read<uint8_t>(context, 4) * 128);
        FILETIME time{};
        GetSystemTimeAsFileTime(&time);
        CIniReader reader("");
        reader.WriteString("SAVES", SlotKey(slot), std::to_string((uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime)
            + ":" + std::to_string(hash), true);
    }

    void ArchiveTimes()
    {
        // The Steam/GOG wrapper also keeps dated 2312-byte save snapshots.
        // Match their preview data, rather than trusting a filename's slot.
        std::error_code error;
        for (const auto& directory : { std::filesystem::path("savedata"), std::filesystem::path("../savedata") })
            for (const auto& entry : std::filesystem::directory_iterator(directory, error))
            {
                if (entry.path().extension() != ".bio3" || entry.file_size(error) != 2312)
                    continue;
                std::array<uint8_t, 2312> data{};
                std::ifstream input(entry.path(), std::ios::binary);
                if (!input.read(reinterpret_cast<char*>(data.data()), data.size()))
                    continue;
                const auto hash = Hash(data.data() + 512);
                WIN32_FILE_ATTRIBUTE_DATA attributes{};
                if (!GetFileAttributesExW(entry.path().c_str(), GetFileExInfoStandard, &attributes))
                    continue;
                const auto time = (uint64_t(attributes.ftLastWriteTime.dwHighDateTime) << 32) | attributes.ftLastWriteTime.dwLowDateTime;
                for (auto& slot : slots)
                    if (slot.hash == hash)
                        slot.time = std::max(slot.time, time);
            }
    }

    int __cdecl TitleHook(uint8_t* title)
    {
        Saves::atTitle = title[1] == 0 && !(*game.flags & 0x80);
        if (Saves::atTitle)
        {
            pending = -1;
            scanning = false;
        }
        if (Saves::atTitle && !autoAttempted)
        {
            autoAttempted = true;
            if (Game::Enabled(Game::Option::AutoLoad))
                Saves::requested = Game::GetSettings().loadSlot.load();
        }
        if (Saves::atTitle && Saves::requested >= 0)
        {
            pending = Saves::requested.load();
            Saves::requested = -1;
            scanning = pending == 0;
            scannedCard = -1;
            slots.clear();
            title[1] = 2; // Native title-menu Load Game action.
            Saves::atTitle = false;
        }
        const auto result = shTitle.unsafe_ccall<int>(title);
        Saves::atTitle = title[1] == 0 && !(*game.flags & 0x80);
        return result;
    }

    void __cdecl CardHook(uint8_t* context)
    {
        if (pending >= 0 && context[2] == 1)
        {
            context[14] = pending > 0 ? uint8_t((pending - 1) / 15) : 0;
            context[19] = 1;
            context[0] = 4;
            return;
        }
        shCard.unsafe_ccall<void>(context);
    }

    int __cdecl SlotsHook(uint8_t* context)
    {
        const auto result = shSlots.unsafe_ccall<int>(context);
        if (pending < 0 || context[2] != 1)
            return result;
        if (context[0] != 6)
        {
            // Empty/unreadable cards still use the native error and cancel UI.
            if (!scanning)
            {
                pending = -1;
                scanning = false;
                return result;
            }
        }
        if (scanning && scannedCard != context[14])
        {
            scannedCard = context[14];
            if (context[0] == 6)
                for (int i = 0; i < 15; ++i)
                    if (!(context[32 + i] & 0xC0))
                    {
                        const int number = context[14] * 15 + i + 1;
                        const auto hash = Hash(context + 2880 + i * 128);
                        slots.push_back({ number, hash, SaveTime(number, hash) });
                    }
            if (context[14] == 0)
            {
                context[14] = 1;
                context[0] = 4;
                return result;
            }
            ArchiveTimes();
            const auto latest = std::max_element(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) { return a.time < b.time; });
            // Old PSX memory cards contain no real-world timestamps. If there
            // is no dated history and several saves, show the native picker.
            if (latest == slots.end() || (slots.size() > 1 && std::any_of(slots.begin(), slots.end(), [](const Slot& slot) { return !slot.time; })))
            {
                pending = -1;
                scanning = false;
                context[14] = 0;
                context[0] = 4;
                return result;
            }
            pending = latest->number;
            scanning = false;
            if ((pending - 1) / 15 != context[14])
            {
                context[14] = uint8_t((pending - 1) / 15);
                context[0] = 4;
                return result;
            }
        }
        if (pending > 0 && context[0] == 6)
        {
            const int index = (pending - 1) % 15;
            context[4] = context[5] = uint8_t(index);
            context[16] = uint8_t(std::max(0, index - 4));
            context[12] = 0;
            if (!(context[32 + index] & 0xC0))
            {
                context[4] = context[10] = uint8_t(index);
                context[0] = 11; // Native validated read, restore and error handling.
            }
            pending = -1;
        }
        return result;
    }

    int __cdecl SaveHook(uint8_t* context)
    {
        const auto result = shSave.unsafe_ccall<int>(context);
        if (context[0] == 10 && context[15] == 0)
            Record(context);
        return result;
    }

    int __fastcall CaptionsHook(int argument)
    {
        if (Game::Enabled(Game::Option::FastLoad) && *captionKind == 1)
        {
            *captionBusy = 0;
            taskExit();
            return 0;
        }
        return shCaptions.unsafe_fastcall<int>(argument);
    }

    void Init()
    {
        if (!Game::Resolve(game)) return;
        auto title = hook::pattern("8B 4C 24 04 51 8A 41 0F 04 04 88 41 0F 25 FF 00 00 00 0F BE 90 ? ? ? ? B8 56 55 55 55");
        auto card = hook::pattern("56 8B 74 24 08 C6 46 07 02 A1 ? ? ? ? F6 C4 10 75 ? 8B 0D ? ? ? ? F6 C5 08");
        auto load = hook::pattern("53 56 8B 74 24 0C 33 C0 32 DB 8A 46 0E 88 5E 07 50 88 5E 13 E8");
        auto save = hook::pattern("53 56 8B 74 24 0C 6A 28 8A 46 0A 88 46 04 8B 0D ? ? ? ? 51 68 ? ? ? ? E8");
        auto captions = hook::pattern("51 33 C0 C6 05 ? ? ? ? 01 A0 ? ? ? ? 83 F8 03 77 ? FF 24 85");
        auto exit = hook::pattern("56 8B 35 ? ? ? ? 66 C7 06 00 00 E8 ? ? ? ? 8B 46 08 50 E8 ? ? ? ? E8 ? ? ? ? 68 00 00 00 FF");
        if (title.size() != 1 || card.size() != 1 || load.size() != 1 || save.size() != 1 || captions.size() != 1 || exit.size() != 1)
            return;
        captionBusy = *captions.get_first<uint8_t*>(5);
        captionKind = *captions.get_first<uint8_t*>(11);
        taskExit = reinterpret_cast<decltype(taskExit)>(exit.get_first());
        shTitle = safetyhook::create_inline(title.get_first(), TitleHook);
        shCard = safetyhook::create_inline(card.get_first(), CardHook);
        shSlots = safetyhook::create_inline(load.get_first(), SlotsHook);
        shSave = safetyhook::create_inline(save.get_first(), SaveHook);
        shCaptions = safetyhook::create_inline(captions.get_first(), CaptionsHook);
    }
}

class SaveFeatures
{
public:
    SaveFeatures() { FusionFix::onInitEvent() += []() { SaveHooks::Init(); }; }
} SaveFeatures;

bool Saves::CanLoad()
{
    return atTitle && SaveHooks::game.flags && !(*SaveHooks::game.flags & 0x08010000)
        && *SaveHooks::game.backgroundMode == 1;
}

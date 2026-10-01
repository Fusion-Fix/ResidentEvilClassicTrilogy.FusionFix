module;

#include <common.hxx>
#include <atomic>
#include "Presentation.hxx"

export module Game;

import Geometry;
import common;

export namespace Game
{
    using WobbleFix::Read;

    enum class Option { WobbleFix, PanAndScan, AlternateControls, SkipIntro, SkipDoor, FastLoad, AutoLoad };
    constexpr std::array<const char*, 7> optionNames = { "WobbleFix", "PanAndScan", "AlternateControls", "SkipIntro", "SkipDoor", "FastLoad", "AutoLoad" };

    struct Settings
    {
        std::array<std::atomic<bool>, 7> enabled;
        std::atomic<int> keyboardRunMode = 0, loadSlot = 0;
        bool hdControls = true;
        float maxAspectRatio = 16.0f / 9.0f;

        Settings()
        {
            CIniReader reader("");
            hdControls = reader.ReadInteger("MAIN", "HDControls", 1) != 0;
            maxAspectRatio = Presentation::ParseAspectRatio(reader.ReadString("MAIN", "MaxAspectRatio", "16:9"));
            enabled[0] = reader.ReadInteger("MAIN", "WobbleFix", 1) != 0;
            enabled[1] = reader.ReadInteger("MAIN", "PanAndScan", 0) != 0;
            enabled[2] = reader.ReadInteger("MAIN", "AlternateControls", 0) != 0;
            enabled[3] = reader.ReadInteger("MAIN", "SkipIntro", 1) != 0;
            enabled[4] = reader.ReadInteger("MAIN", "SkipDoor", 1) != 0;
            enabled[5] = reader.ReadInteger("MAIN", "FastLoad", 0) != 0;
            enabled[6] = reader.ReadInteger("MAIN", "AutoLoad", 0) != 0;
            keyboardRunMode = std::clamp(reader.ReadInteger("MAIN", "KeyboardRunMode", 0), 0, 3);
            loadSlot = std::clamp(reader.ReadInteger("MAIN", "LoadSlot", 0), 0, 30);
        }
    };

    Settings& GetSettings()
    {
        static Settings settings;
        return settings;
    }

    bool Enabled(Option option) { return GetSettings().enabled[size_t(option)].load(std::memory_order_relaxed); }
    auto& onSettingsChanged()
    {
        static FusionFix::Event<> event;
        return event;
    }

    void SaveSettings()
    {
        CIniReader reader("");
        for (size_t i = 0; i < optionNames.size(); ++i)
            reader.WriteInteger("MAIN", optionNames[i], int(GetSettings().enabled[i].load()), true);
        reader.WriteInteger("MAIN", "KeyboardRunMode", GetSettings().keyboardRunMode.load(), true);
        reader.WriteInteger("MAIN", "LoadSlot", GetSettings().loadSlot.load(), true);
    }

    void Toggle(Option option)
    {
        auto& setting = GetSettings().enabled[size_t(option)];
        bool current = setting.load(std::memory_order_relaxed);
        while (!setting.compare_exchange_weak(current, !current, std::memory_order_relaxed)) {}
        onSettingsChanged().executeAll();
    }

    struct State
    {
        void* player = nullptr;
        void* view = nullptr;
        void* camera = nullptr;
        uint32_t* flags = nullptr;
        uint32_t* control = nullptr;
        uint32_t* room = nullptr;
        uint32_t* held = nullptr;
        uint32_t* pressed = nullptr;
        uint8_t* padType = nullptr;
        uint8_t* stickX = nullptr;
        uint8_t* stickY = nullptr;
        uintptr_t* interfaceTable = nullptr;
        uintptr_t* backgroundTable = nullptr;
        uint8_t* menu = nullptr;
        uint8_t* backgroundMode = nullptr;
        void* movement = nullptr;

        bool Gameplay() const { return (*flags & 0x08000000) && *menu == 0; }
        bool Controllable() const
        {
            return Gameplay() && !(*flags & 0x10000000)
                && !(*control & 0x81000000) && Read<uint8_t>(player, 4) == 1;
        }

        float PlayerY() const
        {
            double point[3] = { double(Read<int32_t>(player, 52)),
                double(Read<int32_t>(player, 56)) - 800.0, double(Read<int32_t>(player, 60)) };
            double projected[3]{};
            for (size_t row = 0; row < 3; ++row)
            {
                projected[row] = Read<int32_t>(view, 20 + row * 4);
                for (size_t column = 0; column < 3; ++column)
                    projected[row] += Read<int16_t>(view, (row * 3 + column) * 2) * point[column] / 4096.0;
            }
            if (projected[2] <= 1.0)
                return 120.0f;
            const auto focal = Read<uint16_t>(camera, 2) >> 7;
            return float(120.0 + projected[1] * focal / projected[2]);
        }

        bool Resolve()
        {
            auto playerPattern = hook::pattern("68 ? ? ? ? C7 05 ? ? ? ? ? ? ? ? E8 ? ? ? ? 68 80 00 00 00 68 80 00 00 00");
            auto viewPattern = hook::pattern("BF ? ? ? ? F3 A5 8B 4C 24 4C 8B 70 04 8B 18 8B 69 04");
            auto cameraPattern = hook::pattern("BF ? ? ? ? F6 C4 10 F3 A5 74 05 E8 ? ? ? ? E8");
            auto flagsPattern = hook::pattern("A1 ? ? ? ? A9 00 00 20 00 74 0A E8 ? ? ? ? E9 ? ? ? ? A9 00 00 00 08");
            auto controlPattern = hook::pattern("F7 05 ? ? ? ? 00 00 00 01 75 ? A9 40 42 59 10");
            auto menuPattern = hook::pattern("A0 ? ? ? ? 48 74 ? 48 0F 84 ? ? ? ? 48 0F 85 ? ? ? ? 57 E8");
            auto roomPattern = hook::pattern("0F BF 0D ? ? ? ? 8B 15 ? ? ? ? 33 C0 66 8B 04 4A 0F BF 15");
            auto interfacePattern = hook::pattern("8B 0D ? ? ? ? 83 C1 1C 51 E8 ? ? ? ? A0 ? ? ? ? 83 C4 08 34 01");
            auto backgroundPattern = hook::pattern("8B 0D ? ? ? ? 83 C1 1C 51 E8 ? ? ? ? 8B 35 ? ? ? ? 83 C4 04 3B F3");
            auto modePattern = hook::pattern("F7 05 ? ? ? ? 00 00 01 00 74 05 E8 ? ? ? ? A0 ? ? ? ? 55 56 3C 02 57");
            auto movementPattern = hook::pattern("8B 0D ? ? ? ? 8B 15 ? ? ? ? 56 8B 74 24 08 33 C0 51 8A 46 05 52 56 FF 14 85");
            auto padPattern = hook::pattern("8A 0D ? ? ? ? F7 D0 25 FF FF 00 00 80 F9 07 A3");
            auto stickPattern = hook::pattern("A0 ? ? ? ? 8A 0D ? ? ? ? 88 44 24 11 88 4C 24 13 A0");
            if (playerPattern.size() != 1 || viewPattern.size() != 1 || cameraPattern.size() != 1
                || flagsPattern.size() != 1 || controlPattern.size() != 1 || menuPattern.size() != 1
                || roomPattern.size() != 1 || interfacePattern.size() != 1 || backgroundPattern.size() != 1
                || modePattern.size() != 1 || movementPattern.size() != 1 || padPattern.size() != 1 || stickPattern.size() != 1)
                return false;

            player = *playerPattern.get_first<void*>(1);
            view = *viewPattern.get_first<void*>(1);
            camera = *cameraPattern.get_first<void*>(1);
            flags = *flagsPattern.get_first<uint32_t*>(1);
            control = *controlPattern.get_first<uint32_t*>(2);
            menu = *menuPattern.get_first<uint8_t*>(1);
            room = *roomPattern.get_first<uint32_t*>(3);
            interfaceTable = *interfacePattern.get_first<uintptr_t*>(2);
            backgroundTable = *backgroundPattern.get_first<uintptr_t*>(2);
            backgroundMode = *modePattern.get_first<uint8_t*>(18);
            pressed = *movementPattern.get_first<uint32_t*>(2);
            held = *movementPattern.get_first<uint32_t*>(8);
            movement = movementPattern.get_first();
            padType = *padPattern.get_first<uint8_t*>(2);
            stickX = *stickPattern.get_first<uint8_t*>(1);
            stickY = *stickPattern.get_first<uint8_t*>(7);
            return true;
        }
    };

    bool Resolve(State& output)
    {
        // Both features use the same bindings. Resolve once, before either
        // feature replaces the movement entry point used as a signature.
        static State bindings;
        static const bool found = bindings.Resolve();
        if (found)
            output = bindings;
        return found;
    }
}

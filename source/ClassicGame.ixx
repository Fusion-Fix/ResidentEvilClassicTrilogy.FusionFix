module;
#include <common.hxx>
#include <atomic>
#include "ClassicPresentation.hxx"

export module ClassicGame;
import common;

export namespace ClassicGame
{
    enum class Option { PanAndScan, AlternateControls, SkipIntro, SkipDoor, FastLoad, AutoLoad, AutoPush };
    constexpr std::array<const char*, 7> optionNames = { "PanAndScan", "AlternateControls", "SkipIntro", "SkipDoor", "FastLoad", "AutoLoad", "AutoPush" };

    struct Settings
    {
        std::array<std::atomic<bool>, 7> enabled;
        std::atomic<int> keyboardRunMode = 0, loadSlot = 0;
        std::atomic<bool> hdControls = true;
        bool portableMode = true;
        std::atomic<float> maxAspectRatio = 16.0f / 9.0f;

        Settings()
        {
            CIniReader reader("");
            portableMode = reader.ReadInteger("MAIN", "PortableMode", 1) != 0;
            hdControls = reader.ReadInteger("MAIN", "HDControls", 1) != 0;
            maxAspectRatio = Presentation::ParseAspectRatio(reader.ReadString("MAIN", "MaxAspectRatio", "16:9"));
            for (size_t i = 0; i < optionNames.size(); ++i)
                enabled[i] = reader.ReadInteger("MAIN", optionNames[i], i == 2 || i == 3 || i == 6) != 0;
            keyboardRunMode = std::clamp(reader.ReadInteger("MAIN", "KeyboardRunMode", 0), 0, 3);
            loadSlot = std::clamp(reader.ReadInteger("MAIN", "LoadSlot", 0), 0, 65535);
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

    void Toggle(Option option)
    {
        auto& setting = GetSettings().enabled[size_t(option)];
        setting = !setting.load(std::memory_order_relaxed);
        onSettingsChanged().executeAll();
    }

    void SaveSettings()
    {
        CIniReader reader("");
        for (size_t i = 0; i < optionNames.size(); ++i)
            reader.WriteInteger("MAIN", optionNames[i], int(GetSettings().enabled[i].load()), true);
        reader.WriteInteger("MAIN", "HDControls", int(GetSettings().hdControls.load()), true);
        reader.WriteString("MAIN", "MaxAspectRatio", std::to_string(GetSettings().maxAspectRatio.load()), true);
        reader.WriteInteger("MAIN", "PortableMode", int(GetSettings().portableMode), true);
        reader.WriteInteger("MAIN", "KeyboardRunMode", GetSettings().keyboardRunMode.load(), true);
        reader.WriteInteger("MAIN", "LoadSlot", GetSettings().loadSlot.load(), true);
    }
}

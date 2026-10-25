module;
#include <common.hxx>
#include <atomic>
#include "ClassicPresentation.hxx"

export module ClassicGame;
import common;
import FixSettings;

export namespace ClassicGame
{
    enum class Option { PanAndScan, AlternateControls, SkipIntro, SkipDoor, FastLoad, AutoLoad, AutoPush };
    constexpr std::array<const char*, 7> optionNames = { "PanAndScan", "AlternateControls", "SkipIntro", "SkipDoor", "FastLoad", "AutoLoad", "AutoPush" };

    struct Settings
    {
        std::array<std::atomic<bool>, 7> enabled;
        std::atomic<int> loadSlot = 0;
        std::atomic<bool> defaultRun = false, shiftToggle = false;
        std::atomic<bool> hdControls = true, mouseSteering = true;
        std::atomic<float> mouseSensitivity = 1.0f;
        bool portableMode = true;
        std::atomic<float> maxAspectRatio = 16.0f / 9.0f;

        Settings()
        {
            for (size_t i = 0; i < optionNames.size(); ++i)
            {
                enabled[i] = i != 4 && i != 5;
                FixSettings::Bind(std::string("MAIN.") + optionNames[i], enabled[i]);
            }
            FixSettings::Bind("MAIN.MouseSteering", mouseSteering);
            FixSettings::Bind("MAIN.MouseSensitivity", mouseSensitivity, 0.25f, 4.0f);
            FixSettings::Bind("MAIN.HDControls", hdControls);
            FixSettings::BindAspect("MAIN.MaxAspectRatio", maxAspectRatio);
            FixSettings::Bind("MAIN.DefaultPace", defaultRun);
            FixSettings::Bind("MAIN.ShiftBehavior", shiftToggle);
            FixSettings::Bind("MAIN.LoadSlot", loadSlot, 0, 65535);
            // Applies after a restart: the native settings are redirected at startup
            portableMode = !FixSettings::Has("MAIN.PortableMode") || FixSettings::Plugin().Bool("MAIN.PortableMode");
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

    // Stores the values the native menu changed
    void SaveSettings()
    {
        auto& settings = GetSettings();
        for (size_t i = 0; i < optionNames.size(); ++i)
            FixSettings::Store(std::string("MAIN.") + optionNames[i], settings.enabled[i].load());
        FixSettings::Store("MAIN.MouseSteering", settings.mouseSteering.load());
        FixSettings::Store("MAIN.MouseSensitivity", settings.mouseSensitivity.load());
        FixSettings::Store("MAIN.HDControls", settings.hdControls.load());
        FixSettings::StoreAspect("MAIN.MaxAspectRatio", settings.maxAspectRatio.load());
        FixSettings::Store("MAIN.PortableMode", settings.portableMode);
        FixSettings::Store("MAIN.DefaultPace", settings.defaultRun.load());
        FixSettings::Store("MAIN.ShiftBehavior", settings.shiftToggle.load());
        FixSettings::Store("MAIN.LoadSlot", settings.loadSlot.load());
    }
}

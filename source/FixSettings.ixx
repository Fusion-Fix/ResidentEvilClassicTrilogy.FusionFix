module;

#include <common.hxx>
#include <atomic>
#include <cmath>
#include <charconv>
#include <asi_settings/asi_settings.hpp>
#include "ClassicPresentation.hxx"

export module FixSettings;

// The settings are described in settings/<plugin>.json, embedded with their translations by
// source/resources/Settings.rc. asi_settings::Plugin takes the defaults from it, reads and writes
// <plugin>.cfg next to the plugin, and answers menu hosts, the overlay and FusionFixHub.
export namespace FixSettings
{
    asi_settings::Plugin& Plugin()
    {
        static asi_settings::Plugin plugin;
        static std::once_flag once;
        std::call_once(once, []
        {
            if (!plugin.Load())
                OutputDebugStringA(("Fusion Fix: settings unavailable, " + plugin.Error() + ".\n").c_str());
            // FusionFixHub and the overlay change the values while the game runs
            asi_settings::LiveLink::Start();
        });
        return plugin;
    }

    bool Has(std::string_view id)
    {
        return Plugin().GetDescription().Find(id) != nullptr;
    }

    // Set by changes from any thread (FusionFixHub, the overlay, presets); the game thread applies them
    std::atomic<bool> changed = false;

    bool TakeChanged()
    {
        return changed.exchange(false);
    }

    std::string AspectText(float aspect)
    {
        constexpr std::pair<float, const char*> names[] = { { 4.0f / 3.0f, "4:3" }, { 16.0f / 10.0f, "16:10" },
            { 16.0f / 9.0f, "16:9" }, { 21.0f / 9.0f, "21:9" }, { 32.0f / 9.0f, "32:9" } };
        for (const auto& [value, name] : names)
        {
            if (std::abs(aspect - value) < 0.001f)
                return name;
        }
        char text[32]{};
        std::to_chars(std::begin(text), std::end(text), aspect, std::chars_format::fixed, 6);
        return text;
    }

    float Aspect(std::string_view id)
    {
        return Presentation::ParseAspectRatio(Plugin().String(id));
    }

    // Keeps a value of the code in step with the setting: read now, updated on every change.
    // Settings the game doesn't have keep the code's own value.
    void Bind(std::string_view id, std::atomic<bool>& target)
    {
        if (!Has(id))
            return;
        target = Plugin().Bool(id);
        Plugin().OnChange(id, [&target, id = std::string(id)] { target = Plugin().Bool(id); changed = true; });
    }

    void Bind(std::string_view id, std::atomic<int>& target, int min, int max)
    {
        if (!Has(id))
            return;
        target = std::clamp(Plugin().Int(id), min, max);
        Plugin().OnChange(id, [&target, id = std::string(id), min, max] { target = std::clamp(Plugin().Int(id), min, max); changed = true; });
    }

    void Bind(std::string_view id, std::atomic<float>& target, float min, float max)
    {
        if (!Has(id))
            return;
        const auto read = [id = std::string(id), min, max]
        {
            const float value = Plugin().Float(id);
            return std::isfinite(value) ? std::clamp(value, min, max) : 1.0f;
        };
        target = read();
        Plugin().OnChange(id, [&target, read] { target = read(); changed = true; });
    }

    void BindAspect(std::string_view id, std::atomic<float>& target)
    {
        if (!Has(id))
            return;
        target = Aspect(id);
        Plugin().OnChange(id, [&target, id = std::string(id)] { target = Aspect(id); changed = true; });
    }

    // Stores a value the native menu changed; unchanged values are left alone
    void Store(std::string_view id, bool value)
    {
        if (Has(id) && Plugin().Bool(id) != value)
            Plugin().Set(id, value ? "1" : "0");
    }

    void Store(std::string_view id, int value)
    {
        if (Has(id) && Plugin().Int(id) != value)
            Plugin().Set(id, std::to_string(value));
    }

    void Store(std::string_view id, float value)
    {
        if (!Has(id) || std::abs(Plugin().Float(id) - value) < 0.001f)
            return;
        char text[32]{};
        std::to_chars(std::begin(text), std::end(text), value, std::chars_format::fixed, 2);
        Plugin().Set(id, text);
    }

    void StoreAspect(std::string_view id, float value)
    {
        if (Has(id) && std::abs(Aspect(id) - value) >= 0.001f)
            Plugin().Set(id, AspectText(value));
    }

    // The text language for the fix's own texts: "auto" or a language tag
    std::string TextLanguage()
    {
        return Has("MAIN.TextLanguage") ? Plugin().String("MAIN.TextLanguage") : std::string("auto");
    }

    // Presets of the native menu
    enum class Preset { Vanilla, Recommended, Enhanced, Custom };

    Preset CurrentPreset()
    {
        const auto preset = Plugin().CurrentPreset();
        return preset == asi_settings::Preset::Custom ? Preset::Custom : Preset(int(preset));
    }

    void ApplyPreset(Preset preset)
    {
        if (preset != Preset::Custom)
            Plugin().ApplyPreset(asi_settings::Preset(int(preset)));
    }

    const char* PresetName(Preset preset)
    {
        constexpr const char* names[] = { "Vanilla", "Recommended", "Enhanced", "Custom" };
        return names[int(preset)];
    }

    // Left/Right on the menu's preset row; custom values go to Recommended first
    void StepPreset(int delta)
    {
        const auto current = CurrentPreset();
        ApplyPreset(current == Preset::Custom ? Preset::Recommended : Preset((int(current) + delta + 3) % 3));
    }
}

ASI_SETTINGS_EXPORT(FixSettings::Plugin())

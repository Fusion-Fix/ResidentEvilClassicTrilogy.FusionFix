module;

#include <common.hxx>
#include <atomic>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <asi_settings/asi_settings.hpp>
#include "NativeMenu.hxx"

export module PluginPages;
import FixSettings;
import Localization;

// The pages of the other loaded plugins with ASI Settings: the pause menu's Plugins row opens a list of them, each
// opens that plugin's page. Values cross through each plugin's AsiSettingsApi.
namespace PluginPages
{
    // Text of a plugin in the game's language and in English, for letters the game's font doesn't have
    struct Text
    {
        std::wstring local, english;
    };

    struct Choice
    {
        std::string value;
        Text label;
        float number = 0;
    };

    struct Entry
    {
        enum class Kind { Preset, Setting, Action, Restore } kind = Kind::Setting;
        const asi_settings::Setting* setting = nullptr;
        Text label, help, other, unit;
        std::vector<Choice> choices;
        bool numeric = true, slider = false, restart = false;
        int decimals = 0;
    };

    struct Plugin
    {
        asi_settings::LoadedPlugin loaded;
        asi_settings::Texts english;
        Text name;
        std::vector<const asi_settings::Setting*> settings; // in page order, compatibility pages last
    };

    std::vector<std::unique_ptr<Plugin>> plugins;
    std::map<HMODULE, int32_t> subscriptions;
    // Changes from any thread: the open page is built again
    std::atomic<bool> changed = true;
    std::vector<Entry> entries;
    std::vector<std::optional<NativeMenu::ItemView>> views;
    int built = -1;
    uint64_t builtAt = 0;

    void __cdecl OnChanged(void*, const char*)
    {
        changed = true;
    }

    Text Host(std::string_view key)
    {
        const auto text = Localization::Text(key);
        return { text, text };
    }

    Text Resolve(const Plugin& plugin, std::string_view key)
    {
        return { Localization::Decode(plugin.loaded.file.texts.Resolve(key)), Localization::Decode(plugin.english.Resolve(key)) };
    }

    bool Has(const AsiSettingsApi* api, size_t offset)
    {
        return api && api->size >= offset + sizeof(void*);
    }

    std::string GetValue(const AsiSettingsApi* api, const std::string& id)
    {
        char buffer[256];
        int32_t length = api->Get(id.c_str(), buffer, sizeof(buffer));
        if (length < 0)
            return {};
        if (length < int32_t(sizeof(buffer)))
            return std::string(buffer, length);
        std::string value(size_t(length) + 1, '\0');
        length = api->Get(id.c_str(), value.data(), uint32_t(value.size()));
        value.resize(length >= 0 ? std::min<size_t>(length, value.size() - 1) : 0);
        return value;
    }

    // The values usable now (ASI Settings, Availability), or nothing when every value is
    std::optional<std::vector<asi_settings::Labelled>> UsableValues(const AsiSettingsApi* api, const std::string& id)
    {
        if (!Has(api, offsetof(AsiSettingsApi, GetChoices)) || !api->GetChoices)
            return std::nullopt;
        char buffer[1024];
        int32_t length = api->GetChoices(id.c_str(), buffer, sizeof(buffer));
        if (length < 0)
            return std::nullopt;
        std::string json(buffer, std::min<size_t>(length, sizeof(buffer) - 1));
        if (length >= int32_t(sizeof(buffer)))
        {
            json.assign(size_t(length) + 1, '\0');
            length = api->GetChoices(id.c_str(), json.data(), uint32_t(json.size()));
            json.resize(length >= 0 ? std::min<size_t>(length, json.size() - 1) : 0);
        }
        asi_settings::json::Value root;
        if (!asi_settings::json::Parse(json, root) || !root.IsArray())
            return std::nullopt;
        std::vector<asi_settings::Labelled> values;
        for (auto& item : root.items)
        {
            if (auto value = item.Find("value"))
                values.push_back({ *value, std::string(item.Str("label")) });
        }
        return values;
    }

    std::vector<asi_settings::Slot> ReadSlots(const Plugin& plugin)
    {
        std::vector<asi_settings::Slot> slots;
        for (auto& setting : plugin.loaded.file.description.settings)
        {
            if (!setting.HasValue())
                continue;
            asi_settings::Slot slot;
            slot.setting = &setting;
            asi_settings::SetDefault(slot);
            asi_settings::ParseValue(slot, GetValue(plugin.loaded.api, setting.id), false);
            slots.push_back(std::move(slot));
        }
        return slots;
    }

    bool Evaluate(const asi_settings::Condition& condition, const std::vector<asi_settings::Slot>& slots)
    {
        return asi_settings::Evaluate(condition, [&](std::string_view id) -> const asi_settings::Slot*
        {
            for (auto& slot : slots)
            {
                if (asi_settings::text::IEquals(slot.setting->id, id))
                    return &slot;
            }
            return nullptr;
        });
    }

    bool HasPresets(const Plugin& plugin)
    {
        auto& settings = plugin.loaded.file.description.settings;
        return std::any_of(settings.begin(), settings.end(), [](auto& setting) { return setting.InPresets(); });
    }

    void ApplyPreset(const Plugin& plugin, asi_settings::Preset preset)
    {
        for (auto& slot : ReadSlots(plugin))
        {
            if (!slot.setting->InPresets())
                continue;
            const auto value = asi_settings::detail::JsonToText((*slot.setting->presets)[int(preset)]);
            if (!asi_settings::detail::JsonEquals((*slot.setting->presets)[int(preset)], asi_settings::FormatValue(slot), !slot.setting->StringValued()))
                plugin.loaded.api->Set(slot.setting->id.c_str(), value.c_str());
        }
    }

    std::string FormatNumber(double value, int places)
    {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, places);
        if (result.ec != std::errc{})
            return "?";
        std::string text(buffer, result.ptr);
        return text == "-0" ? "0" : text;
    }

    int DecimalPlaces(double value)
    {
        for (int places = 0; places < 6; ++places, value *= 10)
        {
            if (std::abs(value - std::round(value)) < 0.00001)
                return places;
        }
        return 6;
    }

    float ToNumber(const std::string& text)
    {
        char* end = nullptr;
        const float value = std::strtof(text.c_str(), &end);
        return end != text.c_str() && std::isfinite(value) ? value : 0.0f;
    }

    // A row for a setting, built from its description: bool and choice settings, and numbers and text with special
    // values or suggestions, step through a list; numbers with a range step through it. Others stay in editors.
    bool BuildSetting(const Plugin& plugin, const asi_settings::Setting& setting, const std::vector<asi_settings::Slot>& slots, Entry& entry)
    {
        using asi_settings::Type;
        const auto api = plugin.loaded.api;
        if (!setting.InMenu() || !setting.HasValue())
            return false;
        if (Has(api, offsetof(AsiSettingsApi, GetState)) && api->GetState
            && (api->GetState(setting.id.c_str(), nullptr, 0) & ASI_SETTINGS_STATE_HIDDEN))
            return false;
        if (setting.visibleIf && !Evaluate(*setting.visibleIf, slots))
            return false;

        entry.setting = &setting;
        entry.label = Resolve(plugin, setting.label);
        entry.help = Resolve(plugin, setting.description);
        entry.numeric = !setting.StringValued();
        entry.restart = setting.apply == "restart";
        const auto usable = UsableValues(api, setting.id);
        const auto add = [&](const asi_settings::json::Value& value, Text label)
        {
            const auto text = asi_settings::detail::JsonToText(value);
            if (usable && std::none_of(usable->begin(), usable->end(), [&](auto& item)
                { return asi_settings::detail::JsonEquals(item.value, text, entry.numeric); }))
                return;
            if (label.local.empty())
                label = { Localization::Decode(text), Localization::Decode(text) };
            entry.choices.push_back({ text, std::move(label), ToNumber(text) });
        };
        const auto addLabelled = [&](const std::vector<asi_settings::Labelled>& list)
        {
            for (auto& item : list)
                add(item.value, item.label.empty() ? Text{} : Resolve(plugin, item.label));
        };
        const auto prefers = [&](std::initializer_list<std::string_view> controls)
        {
            for (auto& control : setting.controls)
            {
                if (std::find(controls.begin(), controls.end(), control) != controls.end())
                    return true;
                if (control == "cycler" || control == "dropdown" || control == "segmented" || control == "switch" || control == "checkbox")
                    return false;
            }
            return false;
        };

        switch (setting.type)
        {
            case Type::Bool:
            {
                asi_settings::json::Value value;
                value.kind = asi_settings::json::Value::Kind::Number;
                add(value, Host("Off"));
                value.number = 1;
                add(value, Host("On"));
                break;
            }
            case Type::Choice:
            {
                if (setting.dynamic && usable)
                {
                    for (auto& item : *usable)
                    {
                        auto label = item.label.empty() ? Text{} : Resolve(plugin, item.label);
                        for (auto& known : setting.choices)
                        {
                            if (label.local.empty() && !known.label.empty()
                                && asi_settings::detail::JsonEquals(known.value, asi_settings::detail::JsonToText(item.value), entry.numeric))
                                label = Resolve(plugin, known.label);
                        }
                        add(item.value, label);
                    }
                }
                else
                    addLabelled(setting.choices);
                break;
            }
            case Type::Int:
            case Type::Float:
            case Type::Unknown:
            {
                const bool list = !setting.special.empty() || !setting.suggestions.empty();
                const bool range = setting.min && setting.max && *setting.min < *setting.max && entry.numeric;
                if (range && (!list || prefers({ "slider", "stepper" })))
                {
                    const double minimum = *setting.min, maximum = *setting.max;
                    const double step = setting.step && *setting.step > 0 ? *setting.step : setting.type == Type::Int ? 1.0 : (maximum - minimum) / 20;
                    const auto steps = std::ceil((maximum - minimum) / step - 0.000001);
                    if (steps < 1 || steps > 1000)
                        return false;
                    entry.slider = true;
                    entry.decimals = setting.decimals >= 0 ? setting.decimals : setting.type == Type::Int ? 0
                        : std::max({ DecimalPlaces(minimum), DecimalPlaces(maximum), DecimalPlaces(step) });
                    for (int i = 0; i <= int(steps); ++i)
                    {
                        const double value = i == int(steps) ? maximum : minimum + i * step;
                        const auto text = FormatNumber(value, entry.decimals);
                        entry.choices.push_back({ text, { Localization::Decode(text), Localization::Decode(text) }, float(value) });
                    }
                    if (!setting.unit.empty())
                        entry.unit = { Localization::Decode(setting.unit), Localization::Decode(setting.unit) };
                }
                else if (list)
                {
                    addLabelled(setting.special);
                    addLabelled(setting.suggestions);
                }
                break;
            }
            default:
                // Text, paths, keys and colours: their special values and suggestions; the menu can't enter others
                addLabelled(setting.special);
                addLabelled(setting.suggestions);
                break;
        }
        if (entry.choices.empty())
            return false;

        // A value outside the list shows as Custom, or as the label of a special value
        entry.other = Host("Custom");
        for (auto& slot : slots)
        {
            if (slot.setting != &setting)
                continue;
            for (auto& special : setting.special)
            {
                if (asi_settings::detail::JsonEquals(special.value, asi_settings::FormatValue(slot), entry.numeric) && !special.label.empty())
                    entry.other = Resolve(plugin, special.label);
            }
        }
        return true;
    }

    std::vector<Entry> BuildEntries(const Plugin& plugin)
    {
        std::vector<Entry> rows;
        if (HasPresets(plugin))
        {
            Entry preset;
            preset.kind = Entry::Kind::Preset;
            preset.label = Host("Preset");
            rows.push_back(std::move(preset));
        }
        const auto slots = ReadSlots(plugin);
        bool any = false;
        for (auto setting : plugin.settings)
        {
            if (setting->type == asi_settings::Type::Action)
            {
                if (!setting->InMenu() || (setting->visibleIf && !Evaluate(*setting->visibleIf, slots)))
                    continue;
                Entry action;
                action.kind = Entry::Kind::Action;
                action.setting = setting;
                action.label = Resolve(plugin, setting->label);
                action.help = Resolve(plugin, setting->description);
                rows.push_back(std::move(action));
                continue;
            }
            Entry entry;
            if (BuildSetting(plugin, *setting, slots, entry))
            {
                rows.push_back(std::move(entry));
                any = true;
            }
        }
        if (any)
        {
            Entry restore;
            restore.kind = Entry::Kind::Restore;
            restore.label = Host("Restore defaults");
            restore.help = Host("Reset the settings on this page to their defaults.");
            rows.push_back(std::move(restore));
        }
        return rows;
    }

    // The settings of the plugin's pages in page order, compatibility pages last
    std::vector<const asi_settings::Setting*> PageOrder(const asi_settings::Description& description)
    {
        std::vector<const asi_settings::Page*> pages;
        for (auto& page : description.pages)
            pages.push_back(&page);
        std::stable_sort(pages.begin(), pages.end(), [](auto a, auto b)
        {
            const bool ca = asi_settings::text::IEquals(a->section, "compatibility"), cb = asi_settings::text::IEquals(b->section, "compatibility");
            return ca != cb ? cb : a->order < b->order;
        });
        std::vector<const asi_settings::Setting*> settings;
        const auto add = [&](std::string_view id)
        {
            const auto begin = settings.size();
            for (auto& setting : description.settings)
            {
                if (description.pages.empty() || asi_settings::text::IEquals(setting.page, id))
                    settings.push_back(&setting);
            }
            std::stable_sort(settings.begin() + begin, settings.end(), [](auto a, auto b) { return a->order < b->order; });
        };
        if (pages.empty())
            add({});
        for (auto page : pages)
            add(page->id);
        return settings;
    }

    // The game's language for the plugins' texts, or the text language chosen in the fix's settings
    std::string LanguageTag()
    {
        const auto chosen = FixSettings::TextLanguage();
        if (!chosen.empty() && !asi_settings::text::IEquals(chosen, "auto"))
            return chosen;
        constexpr std::pair<std::string_view, const char*> tags[] = { { "french", "fr" }, { "german", "de" },
            { "italian", "it" }, { "spanish", "es" }, { "japanese", "ja" } };
        for (const auto& [language, tag] : tags)
        {
            if (Localization::language == language)
                return tag;
        }
        return "en";
    }

    // Text that fits width atlas pixels, shortened with "..." when it doesn't
    template<class Width>
    std::wstring Fit(std::wstring text, int width, Width measure)
    {
        if (measure(text) <= width)
            return text;
        while (!text.empty() && measure(text + L"...") > width)
            text.pop_back();
        while (!text.empty() && text.back() == L' ')
            text.pop_back();
        return text + L"...";
    }

    // Lines of at most width atlas pixels, broken at spaces, or anywhere in text without them
    template<class Width>
    std::vector<std::wstring> Wrap(std::wstring_view text, int width, Width measure)
    {
        std::vector<std::wstring> lines;
        std::wstring line;
        size_t at = 0;
        while (at < text.size())
        {
            if (text[at] == L'\n')
            {
                lines.push_back(std::move(line));
                line.clear();
                ++at;
                continue;
            }
            size_t end = at;
            while (end < text.size() && text[end] != L' ' && text[end] != L'\n')
                ++end;
            const auto word = std::wstring(text.substr(at, end - at));
            const auto joined = line.empty() ? word : line + L" " + word;
            if (measure(joined) <= width)
                line = joined;
            else if (!line.empty() && measure(word) <= width)
            {
                lines.push_back(std::move(line));
                line = word;
            }
            else
            {
                // A word longer than the line, or text without spaces: break between letters
                if (!line.empty())
                    line += L' ';
                for (auto c : word)
                {
                    if (!line.empty() && measure(line + c) > width)
                    {
                        lines.push_back(std::move(line));
                        line.clear();
                    }
                    line += c;
                }
            }
            at = end;
            while (at < text.size() && text[at] == L' ')
                ++at;
        }
        if (!line.empty())
            lines.push_back(std::move(line));
        return lines;
    }
}

export namespace PluginPages
{
    // Measures and checks text in the game's font
    struct Font
    {
        std::function<int(std::wstring_view)> width;
        std::function<bool(std::wstring_view)> supports;
    };

    // Finds the other plugins with settings each time the menu opens; only one menu host of the process shows them
    void Refresh()
    {
        plugins.clear();
        entries.clear();
        views.clear();
        built = -1;
        changed = true;
        if (!asi_settings::ClaimHostRole())
            return;
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&plugins), &self);
        for (auto& loaded : asi_settings::FindLoadedPlugins(self, LanguageTag()))
        {
            if (loaded.module == self)
                continue;
            auto plugin = std::make_unique<Plugin>();
            plugin->loaded = std::move(loaded);
            plugin->english = plugin->loaded.file.texts;
            plugin->english.Select("en");
            plugin->name = Resolve(*plugin, plugin->loaded.file.description.name);
            if (plugin->name.local.empty())
                plugin->name = { Localization::Decode(plugin->loaded.file.id), Localization::Decode(plugin->loaded.file.id) };
            plugin->settings = PageOrder(plugin->loaded.file.description);
            // Only plugins with settings in the menu
            const auto rows = BuildEntries(*plugin);
            if (std::none_of(rows.begin(), rows.end(), [](const Entry& entry)
                { return entry.kind == Entry::Kind::Setting || entry.kind == Entry::Kind::Action; }))
                continue;
            const auto api = plugin->loaded.api;
            if (!subscriptions.contains(plugin->loaded.module) && Has(api, offsetof(AsiSettingsApi, Subscribe)) && api->Subscribe)
            {
                const auto token = api->Subscribe(OnChanged, nullptr);
                if (token > 0)
                    subscriptions[plugin->loaded.module] = token;
            }
            plugins.push_back(std::move(plugin));
        }
    }

    // The rows of the open plugin's page, built again after changes
    const std::vector<Entry>& Entries(int plugin)
    {
        // Plugins without Subscribe still show changes made elsewhere
        const auto now = GetTickCount64();
        if (changed.exchange(false) || built != plugin || now - builtAt > 500)
        {
            built = plugin;
            builtAt = now;
            entries = plugin >= 0 && plugin < int(plugins.size()) ? BuildEntries(*plugins[plugin]) : std::vector<Entry>{};
            views.assign(entries.size(), std::nullopt);
        }
        return entries;
    }

    // Rows of the Plugins pages; on the pause page, whether there is any plugin to show
    int Count(NativeMenu::Page page, int plugin)
    {
        if (page == NativeMenu::Page::Pause || page == NativeMenu::Page::Plugins)
            return int(plugins.size());
        if (page == NativeMenu::Page::Plugin)
            return int(Entries(plugin).size());
        return 0;
    }

    std::string Value(const Plugin& plugin, const Entry& entry)
    {
        return GetValue(plugin.loaded.api, entry.setting->id);
    }

    // The position of the value in the entry's list, or -1
    int Position(const Entry& entry, const std::string& value)
    {
        if (entry.slider)
        {
            const float number = ToNumber(value);
            int nearest = -1;
            for (int i = 0; i < int(entry.choices.size()); ++i)
            {
                if (nearest < 0 || std::abs(entry.choices[i].number - number) < std::abs(entry.choices[nearest].number - number))
                    nearest = i;
            }
            return nearest;
        }
        for (int i = 0; i < int(entry.choices.size()); ++i)
        {
            const auto& choice = entry.choices[i];
            if (entry.numeric ? !value.empty() && std::abs(ToNumber(value) - choice.number) <= 0.00001f
                : asi_settings::text::IEquals(choice.value, value))
                return i;
        }
        return -1;
    }

    bool Enabled(const Plugin& plugin, const Entry& entry)
    {
        return !entry.setting || !entry.setting->enabledIf || Evaluate(*entry.setting->enabledIf, ReadSlots(plugin));
    }

    // Left/Right on a row of a plugin's page
    void Change(const NativeMenu::State& state, int delta)
    {
        if (state.page != NativeMenu::Page::Plugin || state.plugin < 0 || state.plugin >= int(plugins.size()))
            return;
        const auto& plugin = *plugins[state.plugin];
        const auto rows = Entries(state.plugin);
        if (state.cursor < 0 || state.cursor >= int(rows.size()))
            return;
        const auto& entry = rows[state.cursor];
        if (entry.kind == Entry::Kind::Preset)
        {
            // Custom values go to Recommended first, as on the fix's own preset row
            const auto current = asi_settings::CurrentPreset(ReadSlots(plugin));
            const auto next = current == asi_settings::Preset::Custom ? asi_settings::Preset::Recommended
                : asi_settings::Preset((int(current) + delta + 3) % 3);
            ApplyPreset(plugin, next);
        }
        else if (entry.kind == Entry::Kind::Setting && Enabled(plugin, entry))
        {
            const int count = int(entry.choices.size());
            const int position = Position(entry, Value(plugin, entry));
            const int next = position < 0 ? (delta > 0 ? 0 : count - 1)
                : entry.slider ? std::clamp(position + delta, 0, count - 1) : (position + delta + count) % count;
            if (next != position)
                plugin.loaded.api->Set(entry.setting->id.c_str(), entry.choices[next].value.c_str());
        }
        else
            return;
        changed = true;
    }

    // Enter on a row of a plugin's page
    void Activate(const NativeMenu::State& state)
    {
        if (state.page != NativeMenu::Page::Plugin || state.plugin < 0 || state.plugin >= int(plugins.size()))
            return;
        const auto& plugin = *plugins[state.plugin];
        const auto rows = Entries(state.plugin);
        if (state.cursor < 0 || state.cursor >= int(rows.size()))
            return;
        const auto& entry = rows[state.cursor];
        const auto api = plugin.loaded.api;
        if (entry.kind == Entry::Kind::Restore)
        {
            // Only the settings shown on this page
            for (auto& row : rows)
            {
                if (row.kind != Entry::Kind::Setting)
                    continue;
                const auto value = asi_settings::detail::JsonToText(row.setting->defaultValue);
                if (!value.empty() && !asi_settings::detail::JsonEquals(row.setting->defaultValue, Value(plugin, row), row.numeric))
                    api->Set(row.setting->id.c_str(), value.c_str());
            }
            changed = true;
        }
        else if (entry.kind == Entry::Kind::Action)
        {
            if (Has(api, offsetof(AsiSettingsApi, Invoke)) && api->Invoke && Enabled(plugin, entry))
                api->Invoke(entry.setting->id.c_str());
            changed = true;
        }
        else
            Change(state, 1);
    }

    std::wstring Title(const NativeMenu::State& state, const Font& font)
    {
        if (state.plugin < 0 || state.plugin >= int(plugins.size()))
            return {};
        const auto& name = plugins[state.plugin]->name;
        return Fit(font.supports(name.local) ? name.local : name.english, 220, font.width);
    }

    // A row of the Plugins pages, its text fitted to the menu's columns and help lines
    NativeMenu::ItemView Item(const NativeMenu::State& state, int index, const Font& font)
    {
        const auto pick = [&](const Text& text) { return font.supports(text.local) ? text.local : text.english; };
        // Atlas pixels: labels and values may shrink to 65%, the size help lines are drawn at
        constexpr int labelWidth = 226, wideLabelWidth = 376, valueWidth = 143, helpWidth = 430;
        const auto helpLines = [&](const std::wstring& text, const std::wstring& note)
        {
            std::array<std::wstring, 2> help;
            const auto lines = Wrap(text, helpWidth, font.width);
            if (!note.empty())
            {
                help[0] = lines.empty() ? std::wstring() : lines.size() == 1 ? lines[0] : Fit(lines[0] + L" " + lines[1], helpWidth, font.width);
                help[1] = Fit(note, helpWidth, font.width);
                return help;
            }
            if (!lines.empty())
                help[0] = lines[0];
            if (lines.size() == 2)
                help[1] = lines[1];
            else if (lines.size() > 2)
            {
                std::wstring rest = lines[1];
                for (size_t i = 2; i < lines.size(); ++i)
                    rest += L" " + lines[i];
                help[1] = Fit(rest, helpWidth, font.width);
            }
            return help;
        };

        NativeMenu::ItemView view;
        if (state.page == NativeMenu::Page::Plugins)
        {
            if (index < 0 || index >= int(plugins.size()))
                return view;
            view.label = Fit(pick(plugins[index]->name), wideLabelWidth, font.width);
            view.help = helpLines(Localization::Text("Change the settings of this plugin."), {});
            return view;
        }
        if (state.page != NativeMenu::Page::Plugin || state.plugin < 0 || state.plugin >= int(plugins.size()))
            return view;
        const auto& plugin = *plugins[state.plugin];
        const auto& rows = Entries(state.plugin);
        if (index < 0 || index >= int(rows.size()))
            return view;
        const auto& entry = rows[index];
        std::wstring value;
        if (entry.kind == Entry::Kind::Preset)
        {
            constexpr const char* names[] = { "Vanilla", "Recommended", "Enhanced" };
            const auto current = asi_settings::CurrentPreset(ReadSlots(plugin));
            value = Localization::Text(current == asi_settings::Preset::Custom ? "Custom" : names[int(current)]);
        }
        else if (entry.kind == Entry::Kind::Setting)
        {
            const auto current = Value(plugin, entry);
            const int position = Position(entry, current);
            if (entry.slider)
            {
                const auto unit = pick(entry.unit);
                value = Localization::Decode(FormatNumber(ToNumber(current), entry.decimals));
                if (!unit.empty() && font.supports(unit))
                    value += (unit.size() > 1 ? L" " : L"") + unit;
            }
            else
                value = pick(position >= 0 ? entry.choices[position].label : entry.other);
        }
        view.enabled = (entry.kind != Entry::Kind::Setting && entry.kind != Entry::Kind::Action) || Enabled(plugin, entry);
        // The rest of the row depends only on the page's description and texts
        if (index < int(views.size()) && views[index])
        {
            auto cached = *views[index];
            cached.value = Fit(value, valueWidth, font.width);
            cached.enabled = view.enabled;
            return cached;
        }
        view.value = Fit(value, valueWidth, font.width);
        view.label = Fit(pick(entry.label), value.empty() ? wideLabelWidth : labelWidth, font.width);
        if (entry.kind == Entry::Kind::Preset)
            view.help = { Localization::Text("Choose a preset with Left/Right; it applies at once."),
                Localization::Text("Vanilla: fixes only. Enhanced: everything on.") };
        else
            view.help = helpLines(pick(entry.help), entry.restart ? Localization::Text("Restart the game to apply this change.") : std::wstring());
        if (index < int(views.size()))
            views[index] = view;
        return view;
    }
}

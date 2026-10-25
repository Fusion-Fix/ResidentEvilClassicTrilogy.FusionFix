#pragma once

// Shared navigation and layout; each engine retains its own rendering and pause hooks.
namespace NativeMenu
{
    enum Action { Resume, Widescreen, Controls, DefaultPace, SkipIntro, SkipDoors,
        FastLoad, AutoLoad, LoadSlot, Load, Quit, Wobble, DisplayPage, ControlsPage, GamePage, Back, AutoPush, Bindings, AspectLimit, Portable, SystemPage, MouseSteering, MouseSensitivity, ShiftBehavior, Preset,
        PluginsPage, Item };
    enum class Page { Pause, Display, Controls, Game, System, Plugins, Plugin };
    // A row of the Plugins pages, filled by the menu host
    struct ItemView
    {
        std::wstring label, value;
        std::array<std::wstring, 2> help;
        bool enabled = true;
    };
    struct State
    {
        Page page = Page::Pause;
        int cursor = 0;
        bool controller = false, xinput = true;
        int plugin = 0;         // the plugin whose page is open
        mutable int top = 0;    // the first row shown on pages that scroll
        // Rows of the Plugins pages without Return; on the pause page, whether the Plugins row is shown
        int (*items)(Page page, int plugin) = nullptr;
        int Items(Page target) const { return items ? items(target, plugin) : 0; }
        void Reset() { page = Page::Pause; cursor = top = 0; }
        std::vector<Action> Rows(int game) const
        {
            switch (page)
            {
            case Page::Plugins:
            case Page::Plugin:
            {
                std::vector<Action> rows(std::max(0, Items(page)), Item);
                rows.push_back(Back);
                return rows;
            }
            case Page::Display: return game == 3 ? std::vector<Action>{ Widescreen, AspectLimit, Wobble, Back } : std::vector<Action>{ Widescreen, AspectLimit, Back };
            case Page::Controls: return { Controls, Bindings, DefaultPace, ShiftBehavior, MouseSteering, MouseSensitivity, Back };
            case Page::Game: return game == 1
                ? std::vector<Action>{ SkipIntro, SkipDoors, FastLoad, AutoLoad, LoadSlot, Back }
                : std::vector<Action>{ SkipIntro, SkipDoors, FastLoad, AutoLoad, LoadSlot, AutoPush, Back };
            case Page::System: return { Portable, Back };
            default:
            {
                auto rows = game == 3 ? std::vector<Action>{ Resume, Load, DisplayPage, ControlsPage, GamePage, Preset, Quit }
                    : std::vector<Action>{ Resume, Load, DisplayPage, ControlsPage, GamePage, Preset, SystemPage, Quit };
                if (Items(Page::Pause) > 0) rows.insert(rows.end() - 1, PluginsPage);
                return rows;
            }
            }
        }
        Action Selected(int game) const { const auto rows = Rows(game); return rows[std::clamp(cursor, 0, int(rows.size()) - 1)]; }
        void Move(int direction, int game) { const int count = int(Rows(game).size()); cursor = (cursor + direction + count) % count; }
        bool Return(int game)
        {
            if (page == Page::Pause) return false;
            top = 0;
            if (page == Page::Plugin)
            {
                page = Page::Plugins;
                cursor = plugin;
                return true;
            }
            const bool plugins = page == Page::Plugins;
            cursor = page == Page::Display ? 2 : page == Page::Controls ? 3 : page == Page::System ? 6 : 4;
            page = Page::Pause;
            if (plugins)
            {
                const auto rows = Rows(game);
                cursor = int(std::find(rows.begin(), rows.end(), PluginsPage) - rows.begin());
            }
            return true;
        }
        bool Enter(Action action, int game)
        {
            if (action == Back) return Return(game);
            if (action == PluginsPage || (action == Item && page == Page::Plugins))
            {
                if (action == Item) plugin = cursor;
                page = action == Item ? Page::Plugin : Page::Plugins;
                cursor = top = 0;
                return true;
            }
            if (action != DisplayPage && action != ControlsPage && action != GamePage && action != SystemPage) return false;
            page = action == DisplayPage ? Page::Display : action == ControlsPage ? Page::Controls : action == SystemPage ? Page::System : Page::Game;
            cursor = 0;
            return true;
        }
    };
    constexpr float aspects[] = { 4.0f / 3.0f, 16.0f / 10.0f, 16.0f / 9.0f, 21.0f / 9.0f, 32.0f / 9.0f };
    constexpr const wchar_t* aspectNames[] = { L"4:3", L"16:10", L"16:9", L"21:9", L"32:9" };
    inline float ChangeAspect(float current, int delta)
    {
        if (delta > 0) { for (float aspect : aspects) if (aspect > current + 0.001f) return aspect; return aspects[0]; }
        for (int i = 4; i >= 0; --i) if (aspects[i] < current - 0.001f) return aspects[i];
        return aspects[4];
    }
    inline std::wstring AspectName(float current)
    {
        for (int i = 0; i < 5; ++i) if (std::abs(current - aspects[i]) < 0.001f) return aspectNames[i];
        auto value = std::to_wstring(current); value.resize(value.find(L'.') + 3); return value + L":1";
    }
    inline const char* Label(Action action)
    {
        constexpr const char* names[] = { "Continue", "Widescreen", "Control type", "Default pace", "Skip intro", "Skip doors",
            "Fast load", "Auto load", "Load slot", "Load game", "Exit game", "Wobble fix", "Display", "Controls", "Game options", "Return", "Auto push", "Bindings", "Aspect limit", "Portable settings", "System", "Mouse steering", "Mouse sensitivity", "Shift behavior", "Preset",
            "Plugins", "" };
        return names[action];
    }
    inline std::array<const char*, 2> Help(Action action, bool canLoad)
    {
        switch (action)
        {
        case Resume: return { "Return to the game.", "" };
        case Load: return canLoad ? std::array<const char*, 2>{ "Load the save selected in Game options.", "" }
            : std::array<const char*, 2>{ "Loading is available from the title screen.", "Return there to load a saved game." };
        case Widescreen: return { "Fill the screen with a moving camera view.", "Right stick / mouse wheel: look up/down." };
        case Controls: return { "Original: turn and move relative to the player.", "Alternate: move in the direction you press." };
        case Bindings: return { "Remaster: WASD and HD Remaster buttons.", "Original: use the native custom bindings." };
        case AspectLimit: return { "Set the widest aspect used by widescreen.", "16:9 is the default; narrower screens adapt." };
        case Portable: return { "Save native settings beside the game.", "Restart the game to apply this change." };
        case SystemPage: return { "Choose where native settings are stored.", "" };
        case MouseSteering: return { "Turn while standing, walking or running.", "While aiming, also aim up/down with the mouse." };
        case MouseSensitivity: return { "Adjust mouse turning and vertical aim response.", "Only affects mouse steering." };
        case DefaultPace: return { "Choose whether you normally walk or run.", "Applies to alternate keyboard controls." };
        case ShiftBehavior: return { "Hold Shift, or press it to switch pace.", "Applies to alternate keyboard controls." };
        case SkipIntro: return { "Go straight to the title menu on startup.", "Story movies are still played." };
        case SkipDoors: return { "Skip door animations between rooms.", "The next room still loads normally." };
        case FastLoad: return { "Skip the recap text after loading a save.", "Story dialogue is unaffected." };
        case AutoPush: return { "Push objects without releasing the run button.", "Applies to alternate movement controls." };
        case AutoLoad: return { "Load the selected save when the game starts.", "If unavailable, show the save selection." };
        case LoadSlot: return { "Choose the save used by Load game and Auto load.", "Latest selects the most recently saved file." };
        case Quit: return { "Exit the game.", "Unsaved progress will be lost." };
        case Preset: return { "Choose a preset with Left/Right; it applies at once.", "Vanilla: fixes only. Enhanced: everything on." };
        case Wobble: return { "Stabilize character and object geometry.", "Reduces the original polygon wobble." };
        case DisplayPage: return { "Adjust the picture and camera view.", "" };
        case ControlsPage: return { "Choose movement and keyboard run controls.", "" };
        case GamePage: return { "Adjust startup, room transitions and loading.", "Changes are saved automatically." };
        case PluginsPage: return { "Change the settings of other loaded plugins.", "" };
        default: return { "Return to the pause menu.", "" };
        }
    }

    // Rows of a page shown at once, and where they are drawn
    struct Layout { int visible; float top, step; };
    inline Layout RowLayout(size_t rows)
    {
        // Eight rows fit above the help text when slightly closer together; longer pages scroll
        if (rows > 8) return { 9, 46.0f, 15.0f };
        if (rows > 7) return { 8, 50.0f, 16.0f };
        return { 7, 57.0f, 19.0f };
    }

    // item: the ItemView of a row of the Plugins pages; title: the heading of a plugin's page
    template<class Panel, class Text, class Translate, class Value, class ItemOf>
    void Draw(int game, const State& state, bool confirmation, bool yes, bool canLoad,
        Panel panel, Text text, Translate translate, Value value, ItemOf item, const std::wstring& title)
    {
        constexpr uint32_t selected = 0xFF00FF00, white = 0xFFFFFFFF;
        const bool plugins = !confirmation && (state.page == Page::Plugins || state.page == Page::Plugin);
        // The engine draws a translucent fullscreen layer before these glyphs.
        const char* heading = confirmation ? "Exit game" : state.page == Page::Pause ? "PAUSE"
            : state.page == Page::Display ? "Display" : state.page == Page::Controls ? "Controls" : state.page == Page::System ? "System"
            : state.page == Page::Plugins ? "Plugins" : "Game options";
        const auto rows = state.Rows(game);
        const auto layout = RowLayout(rows.size());
        const bool scrolls = !confirmation && int(rows.size()) > layout.visible;
        if (scrolls)
        {
            const auto position = std::to_wstring(state.cursor + 1) + L"/" + std::to_wstring(rows.size());
            text(250, 22, position, white, 44, true);
        }
        text(26, 18, state.page == Page::Plugin && !confirmation ? title : translate(heading), white, scrolls ? 220.0f : 264.0f, false);
        if (confirmation)
        {
            text(30, 70, translate("Quit the game?"), white, 258, false);
            text(30, 90, translate("Unsaved progress will be lost."), white, 258, true);
            text(77, 132, (yes ? L"> " : L"  ") + translate("Yes"), yes ? selected : white, 80, false);
            text(182, 132, (!yes ? L"> " : L"  ") + translate("No"), !yes ? selected : white, 80, false);
        }
        else
        {
            // Keep the selected row in view
            state.top = std::clamp(state.top, std::max(0, state.cursor - layout.visible + 1), std::max(0, state.cursor));
            state.top = std::clamp(state.top, 0, std::max(0, int(rows.size()) - layout.visible));
            for (int i = state.top; i < int(rows.size()) && i < state.top + layout.visible; ++i)
            {
                const auto action = rows[i];
                const bool active = i == state.cursor;
                const auto view = action == Item ? item(i) : ItemView{};
                const bool enabled = action == Item ? view.enabled : action != Load || canLoad;
                const float y = layout.top + float(i - state.top) * layout.step;
                if (active) text(26, y, L"\u25b6", selected, 10, false);
                const auto color = !enabled ? 0xFF747B83 : active ? selected : white;
                const auto setting = action == Item ? view.value : value(action);
                text(39, y, action == Item ? view.label : translate(Label(action)), color, setting.empty() ? 245.0f : 147.0f, false);
                if (!setting.empty()) text(194, y, setting, color, 93, false);
            }
        }
        const auto action = state.Selected(game);
        const auto view = !confirmation && action == Item ? item(state.cursor) : ItemView{};
        if (!confirmation && action == Item)
        {
            text(20, 188, view.help[0], white, 280, true);
            text(20, 201, view.help[1], white, 280, true);
        }
        else
        {
            const auto help = confirmation ? std::array<const char*, 2>{ "Choose Yes to exit, or No to keep playing.", "" }
                : plugins ? std::array<const char*, 2>{ "Return to the previous page.", "" } : Help(action, canLoad);
            text(20, 188, translate(help[0]), white, 280, true);
            text(20, 201, translate(help[1]), white, 280, true);
        }
        const bool adjustable = !confirmation && !(action == Item ? view.value : value(action)).empty();
        const auto navigation = confirmation ? (state.controller ? "D-pad / Stick: select" : "Left/Right: select") : state.controller
            ? (adjustable ? "D-pad: select / change" : "D-pad / Stick: select")
            : (adjustable ? "Up/Down: select   Left/Right: change" : "Up/Down: select");
        const auto buttons = state.controller ? (state.xinput ? "A: confirm   B: back" : "Button 1: confirm   Button 2: back")
            : "Enter: confirm   Esc: back";
        text(20, 216, translate(navigation), white, 280, true);
        text(20, 228, translate(buttons), white, 280, true);
    }
}

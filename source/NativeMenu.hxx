#pragma once

// Shared navigation and layout; each engine retains its own rendering and pause hooks.
namespace NativeMenu
{
    enum Action { Resume, Widescreen, Controls, DefaultPace, SkipIntro, SkipDoors,
        FastLoad, AutoLoad, LoadSlot, Load, Quit, Wobble, DisplayPage, ControlsPage, GamePage, Back, AutoPush, Bindings, AspectLimit, Portable, SystemPage, MouseSteering, MouseSensitivity, ShiftBehavior };
    enum class Page { Pause, Display, Controls, Game, System };
    struct State
    {
        Page page = Page::Pause;
        int cursor = 0;
        bool controller = false, xinput = true;
        void Reset() { page = Page::Pause; cursor = 0; }
        std::vector<Action> Rows(int game) const
        {
            switch (page)
            {
            case Page::Display: return game == 3 ? std::vector<Action>{ Widescreen, AspectLimit, Wobble, Back } : std::vector<Action>{ Widescreen, AspectLimit, Back };
            case Page::Controls: return { Controls, Bindings, DefaultPace, ShiftBehavior, MouseSteering, MouseSensitivity, Back };
            case Page::Game: return game == 1
                ? std::vector<Action>{ SkipIntro, SkipDoors, FastLoad, AutoLoad, LoadSlot, Back }
                : std::vector<Action>{ SkipIntro, SkipDoors, FastLoad, AutoLoad, LoadSlot, AutoPush, Back };
            case Page::System: return { Portable, Back };
            default: return game == 3 ? std::vector<Action>{ Resume, Load, DisplayPage, ControlsPage, GamePage, Quit }
                : std::vector<Action>{ Resume, Load, DisplayPage, ControlsPage, GamePage, SystemPage, Quit };
            }
        }
        Action Selected(int game) const { const auto rows = Rows(game); return rows[std::clamp(cursor, 0, int(rows.size()) - 1)]; }
        void Move(int direction, int game) { const int count = int(Rows(game).size()); cursor = (cursor + direction + count) % count; }
        bool Return()
        {
            if (page == Page::Pause) return false;
            cursor = page == Page::Display ? 2 : page == Page::Controls ? 3 : page == Page::System ? 5 : 4;
            page = Page::Pause;
            return true;
        }
        bool Enter(Action action)
        {
            if (action == Back) return Return();
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
            "Fast load", "Auto load", "Load slot", "Load game", "Exit game", "Wobble fix", "Display", "Controls", "Game options", "Return", "Auto push", "Bindings", "Aspect limit", "Portable settings", "System", "Mouse steering", "Mouse sensitivity", "Shift behavior" };
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
        case Wobble: return { "Stabilize character and object geometry.", "Reduces the original polygon wobble." };
        case DisplayPage: return { "Adjust the picture and camera view.", "" };
        case ControlsPage: return { "Choose movement and keyboard run controls.", "" };
        case GamePage: return { "Adjust startup, room transitions and loading.", "Changes are saved automatically." };
        default: return { "Return to the pause menu.", "" };
        }
    }

    template<class Panel, class Text, class Translate, class Value>
    void Draw(int game, const State& state, bool confirmation, bool yes, bool canLoad,
        Panel panel, Text text, Translate translate, Value value)
    {
        constexpr uint32_t selected = 0xFF00FF00, white = 0xFFFFFFFF;
        // The engine draws a translucent fullscreen layer before these glyphs.
        const char* heading = confirmation ? "Exit game" : state.page == Page::Pause ? "PAUSE"
            : state.page == Page::Display ? "Display" : state.page == Page::Controls ? "Controls" : state.page == Page::System ? "System" : "Game options";
        text(26, 18, translate(heading), white, 264.0f, false);
        if (confirmation)
        {
            text(30, 70, translate("Quit the game?"), white, 258, false);
            text(30, 90, translate("Unsaved progress will be lost."), white, 258, true);
            text(77, 132, (yes ? L"> " : L"  ") + translate("Yes"), yes ? selected : white, 80, false);
            text(182, 132, (!yes ? L"> " : L"  ") + translate("No"), !yes ? selected : white, 80, false);
        }
        else
        {
            const auto rows = state.Rows(game);
            for (int i = 0; i < int(rows.size()); ++i)
            {
                const auto action = rows[i];
                const bool active = i == state.cursor;
                const bool enabled = action != Load || canLoad;
                const float y = 57.0f + float(i) * 19.0f;
                if (active) text(26, y, L"\u25b6", selected, 10, false);
                const auto color = !enabled ? 0xFF747B83 : active ? selected : white;
                const auto setting = value(action);
                text(39, y, translate(Label(action)), color, setting.empty() ? 245.0f : 147.0f, false);
                if (!setting.empty()) text(194, y, setting, color, 93, false);
            }
        }
        const auto help = confirmation ? std::array<const char*, 2>{ "Choose Yes to exit, or No to keep playing.", "" }
            : Help(state.Selected(game), canLoad);
        text(20, 188, translate(help[0]), white, 280, true);
        text(20, 201, translate(help[1]), white, 280, true);
        const auto action = state.Selected(game);
        const bool adjustable = !confirmation && !value(action).empty();
        const auto navigation = confirmation ? (state.controller ? "D-pad / Stick: select" : "Left/Right: select") : state.controller
            ? (adjustable ? "D-pad: select / change" : "D-pad / Stick: select")
            : (adjustable ? "Up/Down: select   Left/Right: change" : "Up/Down: select");
        const auto buttons = state.controller ? (state.xinput ? "A: confirm   B: back" : "Button 1: confirm   Button 2: back")
            : "Enter: confirm   Esc: back";
        text(20, 216, translate(navigation), white, 280, true);
        text(20, 228, translate(buttons), white, 280, true);
    }
}

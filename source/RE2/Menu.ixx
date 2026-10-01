module;
#include <common.hxx>
#include <safetyhook.hpp>

export module RE2Menu;
import common;
import ClassicMenu;

namespace RE2Menu
{
    SafetyHookInline shMain, shMovie, shWindow;

    int __cdecl Main()
    {
        if (ClassicMenu::Update()) return 0;
        return shMain.unsafe_ccall<int>();
    }

    int __cdecl Movie()
    {
        // WinMain bypasses Main while an FMV is active. Poll the controller
        // here too, before the movie checks the native pressed-button packet.
        // Keep playback pending while the Fusion Fix menu pauses the movie.
        if (ClassicMenu::Update()) return 1;
        return shMovie.unsafe_ccall<int>();
    }

    LRESULT WINAPI Window(HWND window, UINT message, WPARAM key, LPARAM flags)
    {
        if (ClassicMenu::Message(window, message, key, flags)) return 0;
        return shWindow.unsafe_stdcall<LRESULT>(window, message, key, flags);
    }

    void Init()
    {
        auto frame = hook::pattern("A1 ? ? ? ? 85 C0 75 0F E8 ? ? ? ? C7 05 ? ? ? ? 01 00 00 00 A1 ? ? ? ? C6 05 ? ? ? ? 00 8B C8 8B D0");
        auto window = hook::pattern("8B 0D ? ? ? ? 55 8B 6C 24 14 56 8B 74 24 10 57 8B 7C 24 18 85 C9 74 1D");
        if (frame.size() != 1 || window.size() != 1) return;
        shWindow = safetyhook::create_inline(window.get_first(), Window);
        shMain = safetyhook::create_inline(frame.get_first(), Main);
        auto movie = hook::pattern("E8 ? ? ? ? 0F BE 05 ? ? ? ? 48 83 F8 04 0F 87 ? ? ? ? FF 24 85");
        if (movie.size() == 1) shMovie = safetyhook::create_inline(movie.get_first(), Movie);
        auto clock = hook::pattern("FF 15 ? ? ? ? 8D 0C 80 A3 ? ? ? ? D1 E1 89 0D ? ? ? ? A3 ? ? ? ? C3");
        if (clock.size() == 1)
        {
            const auto reset = reinterpret_cast<void(__cdecl*)()>(clock.get_first());
            ClassicMenu::pause.emplace_back([reset](bool active) { if (!active) reset(); });
        }
    }
}

class Menu
{
public:
    Menu() { FusionFix::onInitEvent() += []() { RE2Menu::Init(); }; }
} Menu;

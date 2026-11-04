module;
#include <common.hxx>
#include <safetyhook.hpp>

export module RE1Menu;
import common;
import ClassicMenu;
import ClassicInput;

namespace RE1Menu
{
    SafetyHookInline shMain, shWindow, shInput;
    int(__cdecl* present)() = nullptr;
    bool waitForInputRelease = false;
    uint32_t* joy = nullptr;
    int(__cdecl* convert)(int, int) = nullptr;

    int __cdecl Input(int port)
    {
        const auto native = uint16_t(shInput.unsafe_ccall<int>(port));
        const auto legacy = ClassicInput::pad.connected && !ClassicInput::pad.xinput && joy && convert
            ? uint16_t(convert(*joy, 1)) : 0;
        const auto input = ClassicInput::HDPacket(1, native, legacy);
        // Native menus poll the devices independently of WndProc. Consume the
        // closing press before the game publishes its held/pressed button state.
        ClassicInput::gameInputSuppressed = ClassicMenu::opened || waitForInputRelease;
        if (ClassicMenu::opened) return 0;
        if (waitForInputRelease)
        {
            if (!input && !ClassicInput::HDState().Any()) waitForInputRelease = false;
            return 0;
        }
        return input;
    }

    int __cdecl Main()
    {
        if (ClassicMenu::Update()) { present(); return 1; }
        return shMain.unsafe_ccall<int>();
    }

    LRESULT __fastcall Window(int first, int second, HWND window, UINT message, WPARAM key, LPARAM flags)
    {
        if (ClassicMenu::Message(window, message, key, flags)) return 0;
        if (ClassicMenu::ready && key == VK_ESCAPE && (message == WM_KEYDOWN || message == WM_KEYUP)) return 0;
        return shWindow.unsafe_fastcall<LRESULT>(first, second, window, message, key, flags);
    }

    void Init()
    {
        ClassicMenu::style = 1;
        auto frame = hook::pattern("? ? ? ? ? ? ? 53 56 57 55 75 04 6A 00 EB 02 6A 01 E8");
        // Japanese loads the wrapper into ECX; Western versions test its
        // global directly. Existing wrapper detours preserve this body.
        auto window = hook::pattern("85 C9 53 56 57 55 74 ? 8B 44 24 60 8B 5C 24 5C 8B 7C 24 58");
        const auto windowEntry = window.empty()
            ? hook::pattern("53 56 57 55 74 ? 8B 44 24 60 8B 5C 24 5C 8B 7C 24 58").get_first<uint8_t>(-10)
            : window.get_first<uint8_t>(-9);
        auto flip = hook::pattern("56 FF 05 ? ? ? ? E8 ? ? ? ? FF 15 ? ? ? ? 83 3D ? ? ? ? 00 8B F0 E9");
        auto input = hook::pattern("? ? ? ? ? ? ? 74 15 6A 00 A1 ? ? ? ? 50 E8 ? ? ? ? 83 C4 08 A3 ? ? ? ? 83 3D ? ? ? ? 02");
        present = reinterpret_cast<decltype(present)>(flip.get_first());
        joy = *input.get_first<uint32_t*>(0x33);
        const auto call = input.get_first<uint8_t>(0x11);
        convert = reinterpret_cast<decltype(convert)>(call + 5 + *reinterpret_cast<int32_t*>(call + 1));
        shInput = safetyhook::create_inline(input.get_first(), Input);
        ClassicMenu::pause.emplace_back([](bool) { waitForInputRelease = true; });
        shWindow = safetyhook::create_inline(windowEntry, Window);
        shMain = safetyhook::create_inline(frame.get_first(), Main);
        const auto pause = hook::pattern("83 3D ? ? ? ? 00 53 56 74 28 A1 ? ? ? ? 50 E8 ? ? ? ? 83 C4 04 83 F8 01 75 15").get_first<int __cdecl()>();
        const auto resume = hook::pattern("83 3D ? ? ? ? 00 56 74 20 80 3D ? ? ? ? 01 75 17 6A 00 A1").get_first<int __cdecl()>();
        ClassicMenu::pause.emplace_back([pause, resume](bool active) { if (active) pause(); else resume(); });
    }
}

class Menu
{
public:
    Menu() { FusionFix::onInitEvent() += []() { RE1Menu::Init(); }; }
} Menu;

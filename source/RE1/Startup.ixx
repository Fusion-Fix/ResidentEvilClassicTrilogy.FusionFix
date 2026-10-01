module;
#include <common.hxx>
#include <safetyhook.hpp>

export module RE1Startup;
import common;
import ClassicGame;
import RE1Saves;

namespace RE1Intro
{
    SafetyHookInline shStartup, shTitle;
    void(__cdecl* replace)(void*) = nullptr;
    void* titleTask = nullptr;
    uint8_t* title = nullptr;
    bool firstTitleFrame = true;

    int __cdecl Startup()
    {
        firstTitleFrame = true;
        if (!ClassicGame::Enabled(ClassicGame::Option::SkipIntro)) return shStartup.unsafe_ccall<int>();
        replace(titleTask);
        return 0;
    }

    int __cdecl Title()
    {
        if (firstTitleFrame && ClassicGame::Enabled(ClassicGame::Option::SkipIntro))
        {
            // The native title task already loaded the background and fonts.
            // Enter its menu instead of waiting on the PRESS ANY BUTTON screen.
            title[0] = 1;
            title[1] = 0;
        }
        firstTitleFrame = false;
        RE1Saves::Title();
        return shTitle.unsafe_ccall<int>();
    }

    void Init()
    {
        auto startup = hook::pattern("83 EC 04 53 56 57 33 DB 55 BE 01 00 00 00 89 1D ? ? ? ? 89 1D ? ? ? ? 89 35");
        auto next = hook::pattern("68 ? ? ? ? E8 ? ? ? ? 83 C4 04 5D 5F 5E 5B 83 C4 04 C3");
        auto menu = hook::pattern("83 EC 04 56 33 F6 80 3D ? ? ? ? 00 74 ? E8 ? ? ? ? 8B F0 8B 0D ? ? ? ? F7 D1 81 E6 00 00 01 00");
        auto state = hook::pattern("A0 ? ? ? ? 85 C0 74 ? 83 F8 01 0F 84 ? ? ? ? 5E 83 C4 04 C3 33 C0 A0");
        if (startup.size() != 1 || next.size() != 1 || menu.size() != 1 || state.size() != 1) return;
        titleTask = *next.get_first<void*>(1);
        replace = reinterpret_cast<decltype(replace)>(injector::GetBranchDestination(next.get_first(5)).as_int());
        title = *state.get_first<uint8_t*>(1);
        shStartup = safetyhook::create_inline(startup.get_first(), Startup);
        shTitle = safetyhook::create_inline(menu.get_first(), Title);
    }
}

class Startup
{
public:
    Startup() { FusionFix::onInitEvent() += []() { RE1Intro::Init(); }; }
} Startup;

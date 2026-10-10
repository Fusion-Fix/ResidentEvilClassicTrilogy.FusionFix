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
    SafetyHookMid shOpeningMovie, shWarningSkip, shLogoSkip;
    uintptr_t openingMovieResume = 0;
    int32_t* openingMovieTimer = nullptr;
    uintptr_t warningResume = 0, logosResume = 0;
    uint8_t* title = nullptr;
    bool firstTitleFrame = true;

    int __cdecl Startup()
    {
        firstTitleFrame = true;
        // Startup also resets engine state and registers resource cleanup.
        // Keep that initialization; skip only its warning and logo sections.
        return shStartup.unsafe_ccall<int>();
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
        auto startup = hook::pattern("83 EC 04 53 56 57 33 DB 55 ? 01 00 00 00 89 1D ? ? ? ? 89 1D ? ? ? ? 89 ?");
        auto next = hook::pattern("68 ? ? ? ? E8 ? ? ? ? 83 C4 04 5D 5F 5E 5B 83 C4 04 C3");
        auto menu = hook::pattern("83 EC 04 56 33 F6 80 3D ? ? ? ? 00 74 ? E8 ? ? ? ? 8B F0 8B 0D ? ? ? ? F7 D1 81 E6 00 00 01 00");
        auto state = hook::pattern("A0 ? ? ? ? 85 C0 74 ? 83 F8 01 0F 84 ? ? ? ? 5E 83 C4 04 C3 33 C0 A0");
        bool japanese = false;
        if (startup.size() == 0)
        {
            startup = hook::pattern("C7 05 ? ? ? ? 01 00 00 00 53 56 33 DB 89 1D ? ? ? ? 89 1D ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? 6A 01");
            next = hook::pattern("6A 01 E8 ? ? ? ? 83 C4 04 68 ? ? ? ? E8 ? ? ? ? 83 C4 04 5E 5B C3");
            japanese = true;
        }
        if (startup.size() != 1 || next.size() != 1 || menu.size() != 1 || state.size() != 1) return;
        title = *state.get_first<uint8_t*>(1);
        logosResume = uintptr_t(next.get_first(japanese ? 10 : 0));
        if (japanese)
        {
            shLogoSkip = safetyhook::create_mid(next.get_first(), [](SafetyHookContext& context)
            {
                if (ClassicGame::Enabled(ClassicGame::Option::SkipIntro)) context.eip = logosResume;
            });
        }
        else
        {
            auto warning = hook::pattern("68 E0 01 00 00 68 80 02 00 00 E8 ? ? ? ? 83 C4 08 A1");
            auto finishWarning = hook::pattern("C7 05 ? ? ? ? 00 00 00 00 68 F0 00 00 00 68 40 01 00 00 E8 ? ? ? ? 83 C4 08 68 80 00 00 00 68 80 00 00 00 68 80 00 00 00 E8 ? ? ? ? 83 C4 0C");
            if (warning.size() == 1 && finishWarning.size() == 1)
            {
                warningResume = uintptr_t(finishWarning.get_first());
                shWarningSkip = safetyhook::create_mid(warning.get_first(18), [](SafetyHookContext& context)
                {
                    if (ClassicGame::Enabled(ClassicGame::Option::SkipIntro)) context.eip = warningResume;
                });
                shLogoSkip = safetyhook::create_mid(finishWarning.get_first(51), [](SafetyHookContext& context)
                {
                    if (ClassicGame::Enabled(ClassicGame::Option::SkipIntro)) context.eip = logosResume;
                });
            }
        }
        // The title task queues OU.avi before its first menu frame. Bypass
        // that request too, without changing any later story movie requests.
        auto openingMovie = hook::pattern("39 1D ? ? ? ? 7F ? A1 ? ? ? ? 6A 01 88 1D ? ? ? ? A3 ? ? ? ? C7 05 ? ? ? ? 10 00 00 00 81 0D ? ? ? ? 00 00 04 00 E8 ? ? ? ? 83 C4 04");
        if (openingMovie.size() == 1)
        {
            auto* code = openingMovie.get_first<uint8_t>();
            openingMovieTimer = *reinterpret_cast<int32_t**>(code + 2);
            openingMovieResume = uintptr_t(code + 8 + *reinterpret_cast<int8_t*>(code + 7));
            shOpeningMovie = safetyhook::create_mid(code, [](SafetyHookContext& context)
            {
                if (ClassicGame::Enabled(ClassicGame::Option::SkipIntro))
                {
                    // Preserve the timer initialized by the skipped block.
                    *openingMovieTimer = 16;
                    context.eip = openingMovieResume;
                }
            });
        }
        shStartup = safetyhook::create_inline(startup.get_first(), Startup);
        shTitle = safetyhook::create_inline(menu.get_first(), Title);
    }
}

class Startup
{
public:
    Startup() { FusionFix::onInitEvent() += []() { RE1Intro::Init(); }; }
} Startup;

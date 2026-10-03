module;
#include <common.hxx>
#include <safetyhook.hpp>

export module RE2Startup;
import common;
import ClassicGame;

namespace RE2Intro
{
    SafetyHookInline shWarning, shTitle;
    uint8_t** task = nullptr;
    uint8_t* busy = nullptr;
    uint8_t* movie = nullptr;
    void(__cdecl* taskExit)() = nullptr;
    void(__cdecl* yield)(int16_t) = nullptr;

    int __cdecl Warning()
    {
        if (!ClassicGame::Enabled(ClassicGame::Option::SkipIntro)) return shWarning.unsafe_ccall<int>();
        // RE2 tasks return each frame. Retain the bootstrap handshake without
        // running the warning, fade or startup movie task.
        if ((*task)[8] != 5)
        {
            busy[0] = busy[2] = 1;
            (*task)[8] = 5;
        }
        if (busy[2] != 2) { yield(1); return 0; }
        busy[0] = busy[2] = 0;
        taskExit();
        return 0;
    }

    int __cdecl Title()
    {
        if (ClassicGame::Enabled(ClassicGame::Option::SkipIntro)) *movie = 1;
        const auto result = shTitle.unsafe_ccall<int>();
        if (ClassicGame::Enabled(ClassicGame::Option::SkipIntro) && (*task)[9] == 5) yield(1);
        return result;
    }

    void Init()
    {
        auto warning = hook::pattern("8B 0D ? ? ? ? 33 C0 8A 41 08 83 F8 06 77 ? FF 24 85");
        if (warning.size() != 1)
            warning = hook::pattern("8B 0D ? ? ? ? 33 C0 8A 41 08 83 F8 07 77 ? FF 24 85");
        auto title = hook::pattern("8B 0D ? ? ? ? 33 C0 8A 41 09 83 F8 09 0F 87 ? ? ? ? FF 24 85");
        auto handshake = hook::pattern("C6 05 ? ? ? ? 01 E8 ? ? ? ? 83 C4 08 6A 04 68 ? ? ? ? 68");
        auto introMovie = hook::pattern("A0 ? ? ? ? 84 C0 75 ? 6A 00 E8 ? ? ? ? 8B 0D ? ? ? ? 83 C4 04 C6 41 09 08");
        auto exit = hook::pattern("A1 ? ? ? ? 8D 04 C0 C1 E0 02 66 C7 80 ? ? ? ? 00 00 C7 80 ? ? ? ? ? ? ? ? C6 80 ? ? ? ? 01 C3");
        auto wait = hook::pattern("A1 ? ? ? ? 66 8B 4C 24 04 8D 04 C0 C1 E0 02 66 89 88 ? ? ? ? B9 01 00 00 00");
        if (warning.size() != 1 || title.size() != 1 || handshake.size() != 1
            || introMovie.size() != 1 || exit.size() != 1 || wait.size() != 1) return;
        task = *warning.get_first<uint8_t**>(2);
        busy = *handshake.get_first<uint8_t*>(2);
        movie = *introMovie.get_first<uint8_t*>(1);
        taskExit = reinterpret_cast<decltype(taskExit)>(exit.get_first());
        yield = reinterpret_cast<decltype(yield)>(wait.get_first());
        shWarning = safetyhook::create_inline(warning.get_first(), Warning);
        shTitle = safetyhook::create_inline(title.get_first(), Title);
    }
}

class Startup
{
public:
    Startup() { FusionFix::onInitEvent() += []() { RE2Intro::Init(); }; }
} Startup;

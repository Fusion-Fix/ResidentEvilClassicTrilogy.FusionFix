module;
#include <common.hxx>
#include <safetyhook.hpp>

export module Startup;
import common;
import Game;

namespace IntroSkip
{
    SafetyHookInline shWarning, shTitle, shGameStartup;
    uint8_t* busy = nullptr;
    uint8_t* fades = nullptr;
    void(__cdecl* taskExit)() = nullptr;
    void(__cdecl* yield)(int16_t) = nullptr;

    int __fastcall GameStartupHook(void* game, void*, HINSTANCE instance)
    {
        // bio3.ini and its asset paths are relative to the executable's directory.
        std::array<wchar_t, 32768> executable{};
        const auto length = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
        if (length && length < executable.size())
            SetCurrentDirectoryW(std::filesystem::path(executable.data()).parent_path().c_str());
        return shGameStartup.unsafe_thiscall<int>(game, instance);
    }

    int __cdecl WarningHook()
    {
        if (!Game::Enabled(Game::Option::SkipIntro)) return shWarning.unsafe_ccall<int>();
        // Preserve the two native tasks' handshake without displaying the warning.
        *busy = 1;
        do { yield(1); } while (*busy != 2);
        *busy = 0;
        taskExit();
        return 0;
    }

    int __cdecl TitleHook(uint8_t* title)
    {
        if (!Game::Enabled(Game::Option::SkipIntro)) return shTitle.unsafe_ccall<int>(title);
        fades[0] = fades[68] = 0;
        title[10] = 0;
        title[12] = title[9] = 1;
        *reinterpret_cast<uint32_t*>(title) = 3;
        return 0;
    }

    void Init()
    {
        auto startup = hook::pattern("64 A1 00 00 00 00 6A FF 68 ? ? ? ? 50 64 89 25 00 00 00 00 81 EC 4C 08 00 00 53 55 56 33 DB 57 8B F1");
        if (startup.size() == 1)
            shGameStartup = safetyhook::create_inline(startup.get_first(), GameStartupHook);
        auto warning = hook::pattern("68 00 40 19 80 E8 ? ? ? ? 05 00 80 00 00 50 68 ? ? ? ? E8 ? ? ? ? 6A 00 6A 02 E8");
        auto handshake = hook::pattern("C6 05 ? ? ? ? 01 6A 3C E8 ? ? ? ? A0 ? ? ? ? 83 C4 04 3C 02");
        auto title = hook::pattern("56 8B 74 24 08 8A 46 01 8B C8 81 E1 FF 00 00 00 83 F9 03 0F 87 ? ? ? ? FF 24 8D");
        auto fade = hook::pattern("8B 44 24 04 8B C8 C1 E1 04 03 C8 33 C0 8A 04 8D ? ? ? ? C3");
        auto exit = hook::pattern("56 8B 35 ? ? ? ? 66 C7 06 00 00 E8 ? ? ? ? 8B 46 08 50 E8 ? ? ? ? E8 ? ? ? ? 68 00 00 00 FF");
        if (warning.size() != 1 || handshake.size() != 1 || title.size() != 1 || fade.size() != 1 || exit.size() != 1) return;
        busy = *handshake.get_first<uint8_t*>(2);
        fades = *fade.get_first<uint8_t*>(16);
        yield = reinterpret_cast<decltype(yield)>(injector::GetBranchDestination(handshake.get_first(9)).as_int());
        taskExit = reinterpret_cast<decltype(taskExit)>(exit.get_first());
        shWarning = safetyhook::create_inline(warning.get_first(), WarningHook);
        shTitle = safetyhook::create_inline(title.get_first(), TitleHook);
    }
}

class Startup
{
public:
    Startup()
    {
        FusionFix::onInitEvent() += []()
        {
            IntroSkip::Init();
            CIniReader iniReader("");
            if (!iniReader.ReadInteger("MAIN", "SkipIntro", 1))
                return;

            auto logoPattern = hook::pattern("A9 00 00 00 20 0F 84 ? ? ? ? A0 ? ? ? ? 84 C0");
            auto sequencePattern = hook::pattern("83 FE 09 0F 87 0E 01 00 00 FF 24 B5 ? ? ? ?");
            auto moviePattern = hook::pattern("F6 05 ? ? ? ? 80 75 ? 6A 0C E8 ? ? ? ? A1");
            auto sourceNextMoviePattern = hook::pattern("A8 04 5E 75 ? A8 80 75 ? 6A 0C E8 ? ? ? ? A1");
            if (logoPattern.size() != 1 || sequencePattern.size() != 1
                || moviePattern.size() + sourceNextMoviePattern.size() != 1)
                return;

            auto* logo = logoPattern.get_first<uint8_t>();
            auto* sequence = sequencePattern.get_first<uint8_t>();
            if (sequence != logo + 49)
                return;

            // Keep the warning-screen handshake and the native title initialization.
            auto* title = logo + 11 + injector::ReadMemory<int32_t>(logo + 7, false);
            injector::MakeJMP(sequence, title, true);

            // Skip only the startup movie branch, leaving story and attract movies intact.
            auto* movie = moviePattern.size() ? moviePattern.get_first<uint8_t>(7)
                                             : sourceNextMoviePattern.get_first<uint8_t>(7);
            injector::WriteMemory<uint8_t>(movie, 0xEB, true);
        };
    }
} Startup;

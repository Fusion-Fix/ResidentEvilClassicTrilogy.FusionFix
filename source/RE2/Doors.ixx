module;
#include <common.hxx>
#include <safetyhook.hpp>

export module RE2Doors;
import common;
import ClassicGame;

namespace RE2Doors
{
    SafetyHookInline shPlayback;
    uint8_t* playing = nullptr;

    int __cdecl Playback()
    {
        // Use the same completion path as the game's existing animation skip.
        // Keep native setup, sounds, room loading and surface cleanup intact.
        if (ClassicGame::Enabled(ClassicGame::Option::SkipDoor)) *playing = 0;
        return shPlayback.unsafe_ccall<int>();
    }

    void Init()
    {
        auto playback = hook::pattern("A1 ? ? ? ? 53 33 DB 80 78 09 01 74 ? F7 05 ? ? ? ? 00 00 01 00 74 ? 6A 01 E8");
        auto active = hook::pattern("38 1D ? ? ? ? 0F 84 ? ? ? ? F7 05 ? ? ? ? 00 00 02 00 75");
        if (playback.size() != 1 || active.size() != 1) return;
        playing = *active.get_first<uint8_t*>(2);
        shPlayback = safetyhook::create_inline(playback.get_first(), Playback);
    }
}

class Doors
{
public:
    Doors() { FusionFix::onInitEvent() += []() { RE2Doors::Init(); }; }
} Doors;

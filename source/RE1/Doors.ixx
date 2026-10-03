module;
#include <common.hxx>
#include <safetyhook.hpp>

export module RE1Doors;
import common;
import ClassicGame;

namespace RE1Doors
{
    SafetyHookInline shPlayback;
    void __cdecl Playback()
    {
        // Initialization and cleanup stay in the native door task. Only its
        // animation loop is omitted; room scripts and loading still run.
        if (!ClassicGame::Enabled(ClassicGame::Option::SkipDoor))
            shPlayback.unsafe_ccall<void>();
    }

    void Init()
    {
        auto playback = hook::pattern("? ? ? ? ? ? ? ? ? ? ? ? ? 53 56 57 33 F6 55 89 35 ? ? ? ? F6 05 ? ? ? ? 01 0F 84");
        if (playback.size() == 1)
            shPlayback = safetyhook::create_inline(playback.get_first(), Playback);
    }
}

class Doors
{
public:
    Doors() { FusionFix::onInitEvent() += []() { RE1Doors::Init(); }; }
} Doors;

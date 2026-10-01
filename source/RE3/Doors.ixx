module;
#include <common.hxx>

export module Doors;
import common;
import Game;

namespace DoorSkip
{
    uint8_t* branch = nullptr;
    size_t length = 0;
    std::array<uint8_t, 6> original{}, skipped{};
    bool initialized = false, lastEnabled = false;
    void Apply()
    {
        const bool enabled = Game::Enabled(Game::Option::SkipDoor);
        if (initialized && enabled == lastEnabled) return;
        injector::WriteMemoryRaw(branch, enabled ? skipped.data() : original.data(), length, true);
        lastEnabled = enabled;
        initialized = true;
    }
}

class Doors
{
public:
    Doors()
    {
        FusionFix::onInitEvent() += []()
        {
            auto pattern = hook::pattern("A0 ? ? ? ? 33 DB 3A C3 74 ? BE 00 08 00 00 85 35");
            auto sourceNextPattern = hook::pattern("A0 ? ? ? ? 84 C0 0F 84 ? ? ? ? BE 00 08 00 00 85 35");
            if (pattern.size() + sourceNextPattern.size() != 1)
                return;

            // Bypass animation playback; retain native setup, loading and cleanup.
            if (pattern.size())
            {
                DoorSkip::branch = pattern.get_first<uint8_t>(9);
                DoorSkip::length = 1;
                DoorSkip::skipped[0] = 0xEB;
            }
            else
            {
                auto* branch = sourceNextPattern.get_first<uint8_t>(7);
                auto* end = branch + 6 + injector::ReadMemory<int32_t>(branch + 2, false);
                DoorSkip::branch = branch;
                DoorSkip::length = 6;
                DoorSkip::skipped = { 0xE9, 0, 0, 0, 0, 0x90 };
                const auto displacement = int32_t(end - branch - 5);
                memcpy(DoorSkip::skipped.data() + 1, &displacement, 4);
            }
            injector::ReadMemoryRaw(DoorSkip::branch, DoorSkip::original.data(), DoorSkip::length, false);
            Game::onSettingsChanged() += []() { DoorSkip::Apply(); };
            DoorSkip::Apply();
        };
    }
} Doors;

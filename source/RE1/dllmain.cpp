#include <common.hxx>

import common;
import PortableSettings;
import RE1Startup;
import RE1Controls;
import RE1Doors;
import RE1Widescreen;
import Localization;
import RE1Menu;
import RE1Saves;

void Init()
{
    Localization::Init();
    FusionFix::onInitEvent().executeAll();
}

extern "C" __declspec(dllexport) void InitializeASI()
{
    std::call_once(CallbackHandler::flag, []()
    {
        PortableSettings::Init(1);
        CallbackHandler::RegisterCallbackAtGetSystemTimeAsFileTime(Init,
            hook::pattern("80 3D ? ? ? ? 00 53 56 57 55 75 04 6A 00 EB 02 6A 01 E8"));
    });
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hModule);
    return TRUE;
}

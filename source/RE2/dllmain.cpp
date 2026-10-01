#include <common.hxx>

import common;
import PortableSettings;
import RE2Controls;
import RE2Doors;
import RE2Startup;
import RE2Widescreen;
import Localization;
import RE2Menu;
import RE2Saves;

void Init()
{
    Localization::Init();
    FusionFix::onInitEvent().executeAll();
}

extern "C" __declspec(dllexport) void InitializeASI()
{
    std::call_once(CallbackHandler::flag, []()
    {
        PortableSettings::Init(2);
        CallbackHandler::RegisterCallbackAtGetSystemTimeAsFileTime(Init,
            hook::pattern("A1 ? ? ? ? 85 C0 75 0F E8 ? ? ? ? C7 05 ? ? ? ? 01 00 00 00 A1 ? ? ? ? C6 05 ? ? ? ? 00 8B C8 8B D0"));
    });
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hModule);
    return TRUE;
}

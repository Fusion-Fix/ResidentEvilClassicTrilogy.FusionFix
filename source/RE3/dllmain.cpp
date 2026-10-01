#include <common.hxx>

import common;
import Rendering;
import Startup;
import Doors;
import Widescreen;
import Controls;
import Input;
import Menu;
import Saves;

void Init()
{
    FusionFix::onInitEvent().executeAll();
}

extern "C"
{
    void __declspec(dllexport) InitializeASI()
    {
        std::call_once(CallbackHandler::flag, []()
        {
            // Steam executables unpack at startup. Wait for the game code before scanning.
            CallbackHandler::RegisterCallbackAtGetSystemTimeAsFileTime(Init, hook::pattern("81 EC B4 02 00 00 8B 84 24 BC 02 00 00"));
        });
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        if (!IsUALPresent()) { InitializeASI(); }
    }
    if (reason == DLL_PROCESS_DETACH)
    {
        Menu::processTerminating = lpReserved != nullptr;
    }
    return TRUE;
}

#include <windows.h>
#include "WebPlugin.h"

extern WebPlugin* g_pWebPlugin;

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD   ul_reason_for_call,
                      LPVOID  lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        WebPlugin::g_hModule = hModule;
        break;

    case DLL_PROCESS_DETACH:
        delete g_pWebPlugin;
        g_pWebPlugin = nullptr;
        break;
    }

    return TRUE;
}

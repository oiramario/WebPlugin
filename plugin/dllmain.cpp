#include <windows.h>
#include "ID_APIWrapper.h"

extern ID_APIWrapper* g_pAPIWrapper;

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD   ul_reason_for_call,
                      LPVOID  lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        ID_APIWrapper::g_hModule = hModule;
        break;

    case DLL_PROCESS_DETACH:
        delete g_pAPIWrapper;
        g_pAPIWrapper = nullptr;
        break;
    }

    return TRUE;
}

// -- ID_WebP.cpp - ACDSee Image Decoder Plug-in exports -------------------
//
// Extremely thin shim: every IDP_* function delegates to the global
// WebPlugin singleton. The actual logic lives in WebPlugin.cpp.
//
// ACDSee calls these functions via GetProcAddress (matched by name,
// declared in ID_WebP.def).
//

#include "ID_PlugIn.h"
#include "WebPlugin.h"

// The singleton WebPlugin instance, created by IDP_Init, deleted on
// DLL_PROCESS_DETACH (see dllmain.cpp).
WebPlugin* g_pWebPlugin = nullptr;

// ===========================================================================
// ACDSee Image Decoder API exports
// ===========================================================================

extern "C" int __stdcall IDP_Init(ID_ClientInfo* pci)
{
    if (g_pWebPlugin == nullptr) {
        g_pWebPlugin = new WebPlugin();
    }
    return IDE_OK;
}

extern "C" int __stdcall IDP_GetPlugInInfo(ID_PlugInInfo** pi)
{
    if (!g_pWebPlugin) return IDE_Error;
    *pi = g_pWebPlugin->GetPlugInInfo();
    return IDE_OK;
}

extern "C" int __stdcall IDP_ShowPlugInDialog(HWND hWndParent)
{
    if (!g_pWebPlugin) return IDE_Error;
    g_pWebPlugin->ShowPlugInDialog(hWndParent);
    return IDE_OK;
}

extern "C" int __stdcall IDP_OpenImage(ID_SourceInfo* psi, ID_StateHdl* phs)
{
    if (!g_pWebPlugin) return IDE_Error;
    return g_pWebPlugin->OpenImage(psi, phs);
}

extern "C" int __stdcall IDP_CloseImage(ID_StateHdl hs)
{
    if (!g_pWebPlugin) return IDE_Error;
    return g_pWebPlugin->CloseImage(hs);
}

extern "C" int __stdcall IDP_GetImageInfo(ID_StateHdl hs, ID_ImageInfo* pii)
{
    if (!g_pWebPlugin) return IDE_Error;
    return g_pWebPlugin->GetImageInfo(hs, pii);
}

extern "C" int __stdcall IDP_GetPageInfo(ID_StateHdl hs, int iPage, ID_PageInfo* ppi)
{
    if (!g_pWebPlugin) return IDE_Error;
    return g_pWebPlugin->GetPageInfo(hs, iPage, ppi);
}

extern "C" int __stdcall IDP_PageDecode(ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio)
{
    if (!g_pWebPlugin) return IDE_Error;
    return g_pWebPlugin->PageDecode(hs, pdp, pio);
}

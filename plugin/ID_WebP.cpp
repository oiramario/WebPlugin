#include "ID_PlugIn.h"
#include "WebPlugin.h"

WebPlugin* g_pWebPlugin = nullptr;

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
    return g_pWebPlugin ? g_pWebPlugin->OpenImage(psi, phs) : IDE_Error;
}

extern "C" int __stdcall IDP_CloseImage(ID_StateHdl hs)
{
    return g_pWebPlugin ? g_pWebPlugin->CloseImage(hs) : IDE_Error;
}

extern "C" int __stdcall IDP_GetImageInfo(ID_StateHdl hs, ID_ImageInfo* pii)
{
    return g_pWebPlugin ? g_pWebPlugin->GetImageInfo(hs, pii) : IDE_Error;
}

extern "C" int __stdcall IDP_GetPageInfo(ID_StateHdl hs, int iPage, ID_PageInfo* ppi)
{
    return g_pWebPlugin ? g_pWebPlugin->GetPageInfo(hs, iPage, ppi) : IDE_Error;
}

extern "C" int __stdcall IDP_PageDecode(ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio)
{
    return g_pWebPlugin ? g_pWebPlugin->PageDecode(hs, pdp, pio) : IDE_Error;
}

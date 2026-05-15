#include "ID_PlugIn.h"
#include "ID_APIWrapper.h"

ID_APIWrapper* g_pAPIWrapper = nullptr;

extern "C" int __stdcall IDP_Init(ID_ClientInfo* pci)
{
    if (g_pAPIWrapper == nullptr) {
        g_pAPIWrapper = new ID_APIWrapper();
    }
    return IDE_OK;
}

extern "C" int __stdcall IDP_GetPlugInInfo(ID_PlugInInfo** pi)
{
    if (!g_pAPIWrapper) return IDE_Error;
    *pi = g_pAPIWrapper->GetPlugInInfo();
    return IDE_OK;
}

extern "C" int __stdcall IDP_ShowPlugInDialog(HWND hWndParent)
{
    if (!g_pAPIWrapper) return IDE_Error;
    g_pAPIWrapper->ShowPlugInDialog(hWndParent);
    return IDE_OK;
}

extern "C" int __stdcall IDP_OpenImage(ID_SourceInfo* psi, ID_StateHdl* phs)
{
    return g_pAPIWrapper ? g_pAPIWrapper->OpenImage(psi, phs) : IDE_Error;
}

extern "C" int __stdcall IDP_CloseImage(ID_StateHdl hs)
{
    return g_pAPIWrapper ? g_pAPIWrapper->CloseImage(hs) : IDE_Error;
}

extern "C" int __stdcall IDP_GetImageInfo(ID_StateHdl hs, ID_ImageInfo* pii)
{
    return g_pAPIWrapper ? g_pAPIWrapper->GetImageInfo(hs, pii) : IDE_Error;
}

extern "C" int __stdcall IDP_GetPageInfo(ID_StateHdl hs, int iPage, ID_PageInfo* ppi)
{
    return g_pAPIWrapper ? g_pAPIWrapper->GetPageInfo(hs, iPage, ppi) : IDE_Error;
}

extern "C" int __stdcall IDP_PageDecode(ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio)
{
    return g_pAPIWrapper ? g_pAPIWrapper->PageDecode(hs, pdp, pio) : IDE_Error;
}

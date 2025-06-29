#include "ID_PlugIn.h"
#include "ID_APIWrapper.h"

ID_APIWrapper* g_pAPIWrapper = nullptr;

PLUGIN_API int __stdcall IDP_Init(ID_ClientInfo* pci)
{
    if (g_pAPIWrapper == nullptr)
    {
        g_pAPIWrapper = new ID_APIWrapper(pci);
    }

    return IDE_OK;
}

PLUGIN_API int __stdcall IDP_GetPlugInInfo(ID_PlugInInfo** pi)
{
    if (g_pAPIWrapper)
    {
        *pi = const_cast<ID_PlugInInfo*>(g_pAPIWrapper->GetPlugInInfo());
        return IDE_OK;
    }

    return IDE_Error;
}

PLUGIN_API int __stdcall IDP_ShowPlugInDialog(HWND hWndParent)
{
    if (g_pAPIWrapper)
    {
        g_pAPIWrapper->ShowPlugInDialog(hWndParent);
        return IDE_OK;
    }

    return IDE_Error;
}

PLUGIN_API int __stdcall IDP_OpenImage(ID_SourceInfo* psi, ID_StateHdl* phs)
{
    int ret = IDE_OK;

    if (g_pAPIWrapper)
    {
        ret = g_pAPIWrapper->OpenImage(psi, phs);
    }

    return IDE_OK;
}

PLUGIN_API int __stdcall IDP_CloseImage(ID_StateHdl hs)
{
    int ret = IDE_OK;

    if (g_pAPIWrapper)
    {
        ret = g_pAPIWrapper->CloseImage(hs);
    }

    return ret;
}

PLUGIN_API int __stdcall IDP_GetImageInfo(ID_StateHdl hs, ID_ImageInfo* pii)
{
    int ret = IDE_OK;

    if (g_pAPIWrapper)
    {
        ret = g_pAPIWrapper->GetImageInfo(hs, pii);
    }

    return ret;
}

PLUGIN_API int __stdcall IDP_GetPageInfo(ID_StateHdl hs, int iPage, ID_PageInfo* ppi)
{
    int ret = IDE_OK;

    if (g_pAPIWrapper)
    {
        ret = g_pAPIWrapper->GetPageInfo(hs, iPage, ppi);
    }

    return ret;
}

PLUGIN_API int __stdcall IDP_PageDecode(ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio)
{
    int ret = IDE_OK;

    if (g_pAPIWrapper)
    {
        ret = g_pAPIWrapper->PageDecode(hs, pdp, pio);
    }

    return ret;
}


// PLUGIN_API int __stdcall IDP_PageDecodeStart(
//     ID_StateHdl     hs,
//     ID_DecodeParam* pdp,
//     ID_OutputParam* pop)
// {
//     if (pdp->dwFlags && PID_RECT) {
//         return IDE_InvalidParam;
//     }

//     if (!hs) {
//         return IDE_InvalidParam; // Invalid handle
//     }

//     char msg[256] = {0};
//     sprintf_s(msg, 256, "nPage=%d", pdp->nPage);
//     MessageBox(NULL, msg, "IDP_PageDecodeStart", MB_OK);

//     WebPDecoder* decoder = (WebPDecoder*)hs;
//     if (pdp->nPage > 0 && (size_t)pdp->nPage >= decoder->getFrames().size()) {
//         return IDE_InvalidParam; // Invalid page number
//     }

//     int width = decoder->getWidth();
//     int height = decoder->getHeight();

//     pop->dwFlags = 0;
//     pop->rc.bottom = height;
//     pop->rc.right = width;
//     pop->rc.left = 0;
//     pop->rc.top = 0;
//     pop->nWidth = width;
//     pop->nHeight = height;
//     pop->bpp = (decoder->hasAlpha() ? 4 : 3) * 8;

//     return IDE_OK;
// }

// PLUGIN_API int __stdcall IDP_PageDecodeStep(
//     ID_StateHdl    hs,
//     ID_StepOutput* pso)
// {
//     MessageBox(NULL, "", "IDP_PageDecodeStep", MB_OK);

//     return IDE_OK;
// }

// PLUGIN_API int __stdcall IDP_PageDecodeStop(
//     ID_StateHdl    hs)
// {
//     MessageBox(NULL, "", "IDP_PageDecodeStop", MB_OK);

//     return IDE_OK;
// }

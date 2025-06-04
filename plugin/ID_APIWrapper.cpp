#include "ID_APIWrapper.h"
#include <windows.h>
#include <string.h>
#include "resource.h"
#include "WebPDecoder.h"

HMODULE ID_APIWrapper::g_hModule = NULL;

ID_APIWrapper::ID_APIWrapper(ID_ClientInfo* pci)
:   m_WebPFormatInfo {
        .dwFlags = 0,
        .dwID = MAKE_FORMATID('W', 'E', 'B', 'P'),
        .szName = "WebP Format",
        .szNameShort = "WEBP",
        .pszExtList = (char*)"WEBP\0\0",
        .szDefExt = "WEBP",
        .color = 0,
        .iIcon = 0,
        .pszMimeType = 0
    },
    m_szFormatInfo {
        m_WebPFormatInfo
    },
    m_PlugInInfo {
        .dwFlags = 0,
        .nVersion = ID_VERSION,
        .szTitle = "WebP Image Codec",
        .iIcon = (UINT)LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_WEBP)),
        .nFormats = sizeof(m_szFormatInfo) / sizeof(m_szFormatInfo[0]),
        .pFormatInfo = m_szFormatInfo
    }
{
    m_ClientInfo.dwFlags = pci->dwFlags;
    strcpy_s(m_ClientInfo.szAppName, pci->szAppName);
    strcpy_s(m_ClientInfo.szCompany, pci->szCompany);
}

ID_APIWrapper::~ID_APIWrapper()
{
}

INT_PTR CALLBACK ID_APIWrapper::AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            char version[64] = {0};
            LoadString(ID_APIWrapper::g_hModule, IDS_VERSION, version, sizeof(version));
            SetDlgItemText(hDlg, IDC_VERSION, version);
        }
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

void ID_APIWrapper::ShowPlugInDialog(HWND hWndParent)
{
    DialogBox(ID_APIWrapper::g_hModule, MAKEINTRESOURCE(IDD_ABOUT), hWndParent, AboutDlgProc);
}

int ID_APIWrapper::OpenImage(ID_SourceInfo* psi, ID_StateHdl* phs)
{
    WebPDecoder* decoder = new WebPDecoder;
    if (!decoder->decode(psi->pBuf, psi->dwLen))
    {
        delete decoder;
        return IDE_Error;
    }
    else
    {
        *phs = decoder;
    }

    return IDE_OK;
}

int ID_APIWrapper::CloseImage(ID_StateHdl hs)
{
    if (hs)
    {
        WebPDecoder* decoder = (WebPDecoder*)hs;
        delete decoder;
    }

    return IDE_OK;
}

int ID_APIWrapper::GetImageInfo(ID_StateHdl hs, ID_ImageInfo* pii)
{
    if (hs)
    {
        WebPDecoder* decoder = (WebPDecoder*)hs;
        if (decoder->getFrames().empty())
        {
            return IDE_CorruptData;
        }

        pii->dwFlags = decoder->hasAnimated() ? IIF_ANIM : 0;
        pii->dwFormatID = MAKE_FORMATID('W', 'E', 'B', 'P');
        pii->si.cx = decoder->getWidth();
        pii->si.cy = decoder->getHeight();
        pii->nBPS = 8;
        pii->nSPP = decoder->hasAlpha() ? 4 : 3;
        pii->nPages = decoder->getFrames().size();
        pii->nMetadataTypes = 0;
        pii->hMetadataTypes = NULL;

        return IDE_OK;
    }
    else
    {
        return IDE_InvalidParam;
    }
}

int ID_APIWrapper::GetPageInfo(ID_StateHdl hs, int iPage, ID_PageInfo* ppi)
{
    if (hs)
    {
        WebPDecoder* decoder = (WebPDecoder*)hs;
        // if (iPage < 0 && (size_t)iPage >= decoder->getFrames().size()) {
        //     MessageBox(NULL, "IDE_InvalidParam", "IDP_GetPageInfo", MB_OK);
        //     return IDE_InvalidParam; // Invalid page number
        // }

        ppi->dwFlags = decoder->hasAlpha() ? PPF_RGB | PPF_ALPHA : PPF_RGB;
        ppi->si.cx = decoder->getWidth();
        ppi->si.cy = decoder->getHeight();
        ppi->nBPS = 8;
        ppi->nSPP = decoder->hasAlpha() ? 4 : 3;
        ppi->nDelay = decoder->getDurations()[iPage];
        ZeroMemory(ppi->szTitle, sizeof(ppi->szTitle));
        ppi->iTransColor = 0;
        ppi->nMetadataTypes = 0;
        ppi->hMetadataTypes = NULL;

        // char msg[256] = {0};
        // sprintf_s(msg, 256, "dwFlags=%d, nDelay=%d, iPage=%d", ppi->dwFlags, ppi->nDelay, iPage);
        // MessageBox(NULL, msg, "IDP_GetPageInfo", MB_OK);

        return IDE_OK;
    }
    else
    {
        return IDE_InvalidParam;
    }
}

int ID_APIWrapper::PageDecode(ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio)
{
    // if (pdp->dwFlags && PID_RECT) {
    //     return IDE_InvalidParam;
    // }

    if (hs)
    {
        WebPDecoder* decoder = (WebPDecoder*)hs;
        // if (pdp->nPage > 0 && (size_t)pdp->nPage >= decoder->getFrames().size())
        // {
        //     return IDE_InvalidParam; // Invalid page number
        // }

        int idx = GetTickCount() % decoder->getFrames().size();

        // char msg[256] = {0};
        // sprintf_s(msg, 256, "nPage=%d", idx);
        // MessageBox(NULL, msg, "IDP_PageDecode", MB_OK);

        int width = decoder->getWidth();
        int height = decoder->getHeight();
        HGLOBAL hDIB = decoder->getFrames()[idx];

        pio->dwFlags = POF_TOPDOWN;
        pio->hdib = hDIB;
        pio->hemf = NULL;
        pio->rc.bottom = height;
        pio->rc.right = width;
        pio->rc.left = 0;
        pio->rc.top = 0;
        pio->pParamEx = nullptr;

        return IDE_OK;
    }
    else
    {
        return IDE_InvalidParam;
    }
}

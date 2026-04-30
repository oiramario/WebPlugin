#include "ID_APIWrapper.h"
#include <windows.h>
#include <string.h>
#include "res/resource.h"
#include "WebPDecoder.h"

HMODULE ID_APIWrapper::g_hModule = NULL;

ID_APIWrapper::ID_APIWrapper(ID_ClientInfo* pci)
:   m_WebPFormatInfo {
        .dwFlags = 0, // CIF_REGISTERED
        .dwID = MAKE_FORMATID('W', 'E', 'B', 'P'),
        .szName = "MasterZ / oiramario",
        .szNameShort = "WEBP",
        .pszExtList = (char*)"WEBP\0\0",
        .szDefExt = "WEBP",
        .color = 0,
        .iIcon = 0,
        .pszMimeType = NULL
    },
    m_szFormatInfo {
        m_WebPFormatInfo
    },
    m_PlugInInfo {
        .dwFlags = 0,
        .nVersion = ID_VERSION,
        .szTitle = "WebP Image Codec",
        .iIcon = 0,
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
    // Ensure full file buffer is loaded (required for content inside archives like ZIP)
    if (psi->pfFillBuffer && psi->dwLen > 0) {
        DWORD dwNewPos = 0;
        psi->pfFillBuffer(psi->pParam, psi->dwLen, &dwNewPos);
    }

    WebPDecoder* decoder = new WebPDecoder;
    if (!decoder->decode(psi->pBuf, psi->dwLen))
    {
        delete decoder;
        return IDE_Error;
    }

    *phs = decoder;
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

        pii->dwFlags = decoder->hasAnimated() ? (IIF_ANIM | IIF_MULTIPAGE) : 0;
        pii->dwFormatID = MAKE_FORMATID('W', 'E', 'B', 'P');
        pii->si.cx = decoder->getWidth();
        pii->si.cy = decoder->getHeight();
        pii->nBPS = 8;
        pii->nSPP = decoder->hasAnimated() ? 3 : (decoder->hasAlpha() ? 4 : 3);
        pii->nPages = int(decoder->getFrames().size());

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
        int nPages = int(decoder->getFrames().size());
        if (iPage < 0 || iPage >= nPages) {
            return IDE_NoPage;
        }

        if (decoder->hasAnimated()) {
            ppi->dwFlags = PPF_RGB;
            ppi->nSPP = 3;
        } else {
            ppi->dwFlags = decoder->hasAlpha() ? PPF_RGB | PPF_ALPHA : PPF_RGB;
            ppi->nSPP = decoder->hasAlpha() ? 4 : 3;
        }
        ppi->si.cx = decoder->getWidth();
        ppi->si.cy = decoder->getHeight();
        ppi->nBPS = 8;
        ppi->nDelay = decoder->getFrames()[iPage].delay;
        ppi->szTitle[0] = 0;

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
    if (!hs) return IDE_InvalidParam;

    WebPDecoder* decoder = (WebPDecoder*)hs;
    const auto& frames = decoder->getFrames();
    int nPages = int(frames.size());
    if (pdp->nPage < 0 || pdp->nPage >= nPages) {
        return IDE_NoPage;
    }

    auto& frame = frames[pdp->nPage];
    auto data = frame.data.data();
    auto size = frame.data.size();

    HGLOBAL hDIB = NULL;

    if (decoder->hasAnimated()) {
        // ACDSee Pro 5 misrenders 32bpp DIBs in multi-page preview (yellow/cyan/red stripes).
        // Convert BGRA→BGR (strip alpha) for compatibility.
        BITMAPINFOHEADER* pBih = (BITMAPINFOHEADER*)data;
        int w = pBih->biWidth;
        int h = abs(pBih->biHeight);
        int srcStride = ((w * 32 + 31) / 32) * 4;  // 32bpp
        int dstStride = ((w * 24 + 31) / 32) * 4;  // 24bpp

        DWORD dstSize = sizeof(BITMAPINFOHEADER) + dstStride * h;
        hDIB = GlobalAlloc(GMEM_MOVEABLE, dstSize);
        if (!hDIB) return IDE_Error;

        uint8_t* pDIB = (uint8_t*)GlobalLock(hDIB);
        if (!pDIB) { GlobalFree(hDIB); return IDE_Error; }

        memcpy(pDIB, data, sizeof(BITMAPINFOHEADER));
        ((BITMAPINFOHEADER*)pDIB)->biBitCount = 24;
        ((BITMAPINFOHEADER*)pDIB)->biSizeImage = dstStride * h;

        const uint8_t* pSrc = data + sizeof(BITMAPINFOHEADER);
        uint8_t* pDst = pDIB + sizeof(BITMAPINFOHEADER);
        for (int y = 0; y < h; y++) {
            const uint8_t* sp = pSrc + y * srcStride;
            uint8_t* dp = pDst + y * dstStride;
            for (int x = 0; x < w; x++) {
                dp[0] = sp[0]; // B
                dp[1] = sp[1]; // G
                dp[2] = sp[2]; // R
                sp += 4;
                dp += 3;
            }
        }

        GlobalUnlock(hDIB);
    } else {
        hDIB = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!hDIB) return IDE_Error;
        uint8_t* pMem = (uint8_t*)GlobalLock(hDIB);
        if (!pMem) { GlobalFree(hDIB); return IDE_Error; }
        memcpy(pMem, data, size);
        GlobalUnlock(hDIB);
    }

    pio->dwFlags = 0;
    pio->hdib = hDIB;
    pio->hemf = NULL;
    pio->rc.left = 0;
    pio->rc.top = 0;
    pio->rc.right = decoder->getWidth();
    pio->rc.bottom = decoder->getHeight();
    pio->pParamEx = NULL;

    return IDE_OK;
}


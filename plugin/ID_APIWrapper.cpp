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

    // Force disable "Sharpen subsampled images" which causes hang
    // on large animated WebP (takes effect next ACDSee launch)
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\ACD Systems\\ACDSee Pro\\50",
            0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0;
        RegSetValueExA(hKey, "ViewerSharpen", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        RegCloseKey(hKey);
    }
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

    WebPDecoder* decoder = new (std::nothrow) WebPDecoder;
    if (!decoder) {
        return IDE_Error;
    }

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
        if (decoder->getFrameCount() == 0)
        {
            return IDE_CorruptData;
        }

        pii->dwFlags = decoder->hasAnimated() ? (IIF_ANIM | IIF_MULTIPAGE) : 0;
        pii->dwFormatID = MAKE_FORMATID('W', 'E', 'B', 'P');
        pii->si.cx = decoder->getWidth();
        pii->si.cy = decoder->getHeight();
        pii->nBPS = decoder->getBitsPerSample();
        pii->nSPP = 3;
        pii->nPages = decoder->getFrameCount();

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
        int nPages = decoder->getFrameCount();
        if (iPage < 0 || iPage >= nPages) {
            return IDE_NoPage;
        }

        if (decoder->hasAnimated()) {
            ppi->dwFlags = PPF_RGB;
            ppi->nSPP = 3;
        } else {
            ppi->dwFlags = PPF_RGB;
            ppi->nSPP = 3;
        }
        ppi->si.cx = decoder->getWidth();
        ppi->si.cy = decoder->getHeight();
        ppi->nBPS = decoder->getBitsPerSample();
        ppi->nDelay = decoder->getFrameDelay(iPage);
        ppi->szTitle[0] = 0;

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
    int nPages = decoder->getFrameCount();
    if (pdp->nPage < 0 || pdp->nPage >= nPages) {
        return IDE_NoPage;
    }

    const int width  = decoder->getWidth();
    const int height = decoder->getHeight();
    const int bytesPerPixel = 3;

    auto& frame = decoder->getFrame(pdp->nPage);

    int rowBytes = width * bytesPerPixel;
    int stride   = ((rowBytes + 3) / 4) * 4;
    int imageSize = stride * height;

    HGLOBAL hDIB = GlobalAlloc(GMEM_FIXED, sizeof(BITMAPINFOHEADER) + imageSize);
    if (!hDIB) {
        return IDE_Error;
    }

    BITMAPINFOHEADER bih = {0};
    bih.biSize        = sizeof(BITMAPINFOHEADER);
    bih.biWidth       = width;
    bih.biHeight      = height;
    bih.biPlanes      = 1;
    bih.biBitCount    = bytesPerPixel * 8;
    bih.biCompression = BI_RGB;
    bih.biSizeImage   = imageSize;
    memcpy(hDIB, &bih, sizeof(BITMAPINFOHEADER));

    uint8_t* dst = (uint8_t*)hDIB + sizeof(BITMAPINFOHEADER);
    const uint8_t* src = frame.data();
    memcpy(dst, src, imageSize);

    pio->dwFlags = 0;
    pio->hdib = hDIB;
    pio->hemf = NULL;
    pio->rc.left = 0;
    pio->rc.top = 0;
    pio->rc.right = width;
    pio->rc.bottom = height;
    pio->pParamEx = NULL;

    return IDE_OK;
}

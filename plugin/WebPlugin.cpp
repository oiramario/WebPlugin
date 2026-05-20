#include "WebPlugin.h"
#include <windows.h>
#include <shlobj.h>
#include <string.h>
#include "res/resource.h"
#include "WebPDecoder.h"

HMODULE WebPlugin::g_hModule = NULL;

static bool SetRegStringIfChanged(HKEY root, const char* subKey, const char* value)
{
    char existing[MAX_PATH + 16] = {0};
    DWORD size = sizeof(existing);
    LSTATUS r = RegGetValueA(root, subKey, NULL, RRF_RT_REG_SZ, NULL, existing, &size);
    if (r == ERROR_SUCCESS && strcmp(existing, value) == 0) {
        return false;
    }

    HKEY hKey;
    if (RegCreateKeyExA(root, subKey, 0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return false;
    }
    RegSetValueExA(hKey, NULL, 0, REG_SZ, (const BYTE*)value, (DWORD)(strlen(value) + 1));
    RegCloseKey(hKey);
    return true;
}

static void RegisterWebPIcon()
{
    char path[MAX_PATH];
    if (!GetModuleFileNameA(WebPlugin::g_hModule, path, MAX_PATH))
        return;

    char iconRef[MAX_PATH + 16];
    sprintf_s(iconRef, "%s,-%d", path, IDI_ICON_WEBP);

    bool changed = false;

    char progID[128] = {0};
    DWORD size = sizeof(progID);
    if (RegGetValueA(HKEY_CLASSES_ROOT, ".webp", NULL, RRF_RT_REG_SZ, NULL, progID, &size) == ERROR_SUCCESS) {
        char keyPath[256];
        sprintf_s(keyPath, "Software\\Classes\\%s\\DefaultIcon", progID);
        changed |= SetRegStringIfChanged(HKEY_CURRENT_USER, keyPath, iconRef);
    } else {
        changed |= SetRegStringIfChanged(HKEY_CURRENT_USER, "Software\\Classes\\.webp", "WebP.Image");
        changed |= SetRegStringIfChanged(HKEY_CURRENT_USER, "Software\\Classes\\WebP.Image\\DefaultIcon", iconRef);
    }

    if (changed) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    }
}

WebPlugin::WebPlugin()
:   m_FormatInfo {
        .dwFlags = 0,
        .dwID = MAKE_FORMATID('W', 'E', 'B', 'P'),
        .szName = "MasterZ / oiramario",
        .szNameShort = "WEBP",
        .pszExtList = (char*)"WEBP\0\0",
        .szDefExt = "WEBP",
        .color = 0,
        .iIcon = 0,
        .pszMimeType = NULL
    },
    m_PlugInInfo {
        .dwFlags = 0,
        .nVersion = ID_VERSION,
        .szTitle = "WebP Image Codec",
        .iIcon = 0,
        .nFormats = 1,
        .pFormatInfo = &m_FormatInfo
    }
{
    {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\ACD Systems\\ACDSee Pro\\50",
                0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            DWORD current = 0, type = 0, size = sizeof(current);
            LSTATUS r = RegQueryValueExA(hKey, "ViewerSharpen", NULL, &type,
                                         (LPBYTE)&current, &size);
            bool needsWrite = (r != ERROR_SUCCESS) || (type != REG_DWORD) || (current != 0);
            if (needsWrite) {
                DWORD zero = 0;
                RegSetValueExA(hKey, "ViewerSharpen", 0, REG_DWORD,
                               (const BYTE*)&zero, sizeof(zero));
            }
            RegCloseKey(hKey);
        }
    }

    RegisterWebPIcon();
}

INT_PTR CALLBACK WebPlugin::AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            char version[64] = {0};
            LoadString(WebPlugin::g_hModule, IDS_VERSION, version, sizeof(version));
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

void WebPlugin::ShowPlugInDialog(HWND hWndParent)
{
    DialogBox(WebPlugin::g_hModule, MAKEINTRESOURCE(IDD_ABOUT), hWndParent, AboutDlgProc);
}

int WebPlugin::OpenImage(ID_SourceInfo* psi, ID_StateHdl* phs)
{
    {
        char buf[512];
        sprintf_s(buf, "WebPlugin::OpenImage: dwFlags=0x%lX pszFN=%s pBuf=%p dwLen=%lu pfFillBuffer=%p pParam=%p",
                  psi->dwFlags,
                  psi->pszFN ? psi->pszFN : "(null)",
                  psi->pBuf,
                  psi->dwLen,
                  psi->pfFillBuffer,
                  psi->pParam);
        OutputDebugStringA(buf);
    }
    if (!psi || !phs) {
        OutputDebugStringA("WebPlugin::OpenImage: psi or phs is NULL -> IDE_InvalidParam");
        return IDE_InvalidParam;
    }

    if ((psi->dwFlags & SIF_VIRTUALFILENAME) && psi->pfFillBuffer && psi->dwLen > 0) {
        DWORD dwNewPos = 0;
        psi->pfFillBuffer(psi->pParam, psi->dwLen, &dwNewPos);
    }

    WebPDecoder* decoder = new (std::nothrow) WebPDecoder;
    if (!decoder) {
        OutputDebugStringA("WebPlugin::OpenImage: new WebPDecoder FAILED -> IDE_FatalError");
        return IDE_FatalError;
    }

    if (!decoder->decode({psi->pBuf, psi->dwLen}))
    {
        OutputDebugStringA("WebPlugin::OpenImage: decode() FAILED -> IDE_UnknownFormat");
        delete decoder;
        return IDE_UnknownFormat;
    }

    *phs = decoder;
    {
        char buf[256];
        sprintf_s(buf, "WebPlugin::OpenImage: -> hs=%p (anim=%d frames=%d %dx%d)",
                  *phs, decoder->hasAnimated(), decoder->getFrameCount(),
                  decoder->getWidth(), decoder->getHeight());
        OutputDebugStringA(buf);
    }
    return IDE_OK;
}

int WebPlugin::CloseImage(ID_StateHdl hs)
{
    {
        char buf[128];
        sprintf_s(buf, "WebPlugin::CloseImage: hs=%p", hs);
        OutputDebugStringA(buf);
    }
    if (hs)
    {
        WebPDecoder* decoder = (WebPDecoder*)hs;
        delete decoder;
    }

    return IDE_OK;
}

int WebPlugin::GetImageInfo(ID_StateHdl hs, ID_ImageInfo* pii)
{
    {
        char buf[128];
        sprintf_s(buf, "WebPlugin::GetImageInfo: hs=%p", hs);
        OutputDebugStringA(buf);
    }
    if (!hs) {
        OutputDebugStringA("WebPlugin::GetImageInfo: hs is NULL -> IDE_InvalidParam");
        return IDE_InvalidParam;
    }

    WebPDecoder* decoder = (WebPDecoder*)hs;
    if (decoder->getFrameCount() == 0)
    {
        OutputDebugStringA("WebPlugin::GetImageInfo: frameCount==0 -> IDE_CorruptData");
        return IDE_CorruptData;
    }

    pii->dwFlags = decoder->hasAnimated() ? (IIF_ANIM | IIF_MULTIPAGE) : 0;
    pii->dwFormatID = MAKE_FORMATID('W', 'E', 'B', 'P');
    pii->si.cx = decoder->getWidth();
    pii->si.cy = decoder->getHeight();
    pii->nBPS = decoder->getBitsPerSample();
    pii->nSPP = decoder->hasAlpha() ? 4 : 3;
    pii->nPages = decoder->getFrameCount();

    return IDE_OK;
}

int WebPlugin::GetPageInfo(ID_StateHdl hs, int iPage, ID_PageInfo* ppi)
{
    {
        char buf[128];
        sprintf_s(buf, "WebPlugin::GetPageInfo: hs=%p iPage=%d", hs, iPage);
        OutputDebugStringA(buf);
    }
    if (!hs)
    {
        OutputDebugStringA("WebPlugin::GetPageInfo: hs is NULL -> IDE_InvalidParam");
        return IDE_InvalidParam;
    }
    WebPDecoder* decoder = (WebPDecoder*)hs;
    int nPages = decoder->getFrameCount();
    if (iPage < 0 || iPage >= nPages) {
        char buf[128];
        sprintf_s(buf, "WebPlugin::GetPageInfo: iPage=%d out of range [0,%d) -> IDE_NoPage", iPage, nPages);
        OutputDebugStringA(buf);
        return IDE_NoPage;
    }

    ppi->dwFlags = decoder->hasAlpha() ? PPF_RGB | PPF_ALPHA : PPF_RGB;
    ppi->nSPP = decoder->hasAlpha() ? 4 : 3;
    ppi->si.cx = decoder->getWidth();
    ppi->si.cy = decoder->getHeight();
    ppi->nBPS = decoder->getBitsPerSample();
    ppi->nDelay = decoder->getFrameDelay(iPage);
    ppi->szTitle[0] = 0;

    return IDE_OK;
}

int WebPlugin::PageDecode(ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio)
{
    {
        char buf[256];
        sprintf_s(buf, "WebPlugin::PageDecode: hs=%p nPage=%d nWidth=%d nHeight=%d dwFlags=0x%lX quality=%d",
                  hs, pdp->nPage, pdp->nWidth, pdp->nHeight, pdp->dwFlags, pdp->quality);
        OutputDebugStringA(buf);
    }
    if (!hs || !pdp || !pio) {
        OutputDebugStringA("WebPlugin::PageDecode: hs, pdp, or pio is NULL -> IDE_InvalidParam");
        return IDE_InvalidParam;
    }

    WebPDecoder* decoder = (WebPDecoder*)hs;
    int nPages = decoder->getFrameCount();
    if (pdp->nPage < 0 || pdp->nPage >= nPages) {
        char buf[128];
        sprintf_s(buf, "WebPlugin::PageDecode: nPage=%d out of range -> IDE_NoPage", pdp->nPage);
        OutputDebugStringA(buf);
        return IDE_NoPage;
    }

    const int srcW = decoder->getWidth();
    const int srcH = decoder->getHeight();
    const int dstStride  = ((srcW * 3 + 3) / 4) * 4;
    const size_t imageSize = static_cast<size_t>(dstStride) * srcH;
    const size_t dibSize   = sizeof(BITMAPINFOHEADER) + imageSize;

    HGLOBAL hDIB = GlobalAlloc(GMEM_FIXED, dibSize);
    if (!hDIB) {
        OutputDebugStringA("WebPlugin::PageDecode: GlobalAlloc FAILED -> IDE_TooBig");
        return IDE_TooBig;
    }

    BITMAPINFOHEADER* pBih = static_cast<BITMAPINFOHEADER*>(hDIB);
    pBih->biSize        = sizeof(BITMAPINFOHEADER);
    pBih->biWidth       = srcW;
    pBih->biHeight      = srcH;
    pBih->biPlanes      = 1;
    pBih->biBitCount    = 24;
    pBih->biCompression = BI_RGB;
    pBih->biSizeImage   = static_cast<DWORD>(imageSize);

    uint8_t* pDst = reinterpret_cast<uint8_t*>(hDIB) + sizeof(BITMAPINFOHEADER);

    if (!decoder->getFrame(pdp->nPage, pDst, dstStride)) {
        GlobalFree(hDIB);
        OutputDebugStringA("WebPlugin::PageDecode: getFrame FAILED -> IDE_CorruptData");
        return IDE_CorruptData;
    }

    pio->dwFlags = 0;
    pio->hdib = hDIB;
    pio->hemf = NULL;
    pio->rc.left = 0;
    pio->rc.top = 0;
    pio->rc.right = srcW;
    pio->rc.bottom = srcH;
    pio->pParamEx = NULL;

    return IDE_OK;
}

// -- WebPlugin - ACDSee Image Decoder Plug-in implementation --------------
//
// Each method corresponds to one ACDSee IDP_* function call. The decoder
// state (a WebPDecoder*) is passed between calls via the opaque ID_StateHdl.
//
// +=========================================================================+
// |  Registry side-effects (on construction)                                |
// |  1. ViewerSharpen = 0 - workaround for freeze on large animated WebP    |
// |  2. ViewerSSReadAhead = 0 - avoid OOM in 32-bit process                 |
// |  3. .webp -> Explorer icon registration (HKCU overlay)                  |
// +=========================================================================+
//

#include "WebPlugin.h"
#include <windows.h>
#include <shlobj.h>
#include <cstring>
#include "res/resource.h"
#include "WebPDecoder.h"

HMODULE WebPlugin::g_hModule = nullptr;

// ===========================================================================
// Helper: write REG_SZ only if the value differs (avoids unnecessary
// Explorer icon cache invalidation and registry churn).
// ===========================================================================

static bool SetRegStringIfChanged(HKEY root, const char* subKey, const char* value)
{
    char existing[MAX_PATH + 16] = {0};
    DWORD size = sizeof(existing);
    LSTATUS r = RegGetValueA(root, subKey, NULL, RRF_RT_REG_SZ, NULL, existing, &size);
    if (r == ERROR_SUCCESS && strcmp(existing, value) == 0) {
        return false;  // already set, no change
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

// ===========================================================================
// Helper: register the plug-in's icon for .webp files in Explorer.
//
// Priority: modify existing ProgID's DefaultIcon (HKCU overlay), otherwise
// create a minimal WebP.Image association. The icon is shipped inside the
// plug-in DLL itself (resource IDI_ICON_WEBP).
// ===========================================================================

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
        // Existing ProgID - overlay its DefaultIcon in HKCU
        char keyPath[256];
        sprintf_s(keyPath, "Software\\Classes\\%s\\DefaultIcon", progID);
        changed |= SetRegStringIfChanged(HKEY_CURRENT_USER, keyPath, iconRef);
    } else {
        // No ProgID exists - create a minimal one
        changed |= SetRegStringIfChanged(HKEY_CURRENT_USER, "Software\\Classes\\.webp", "WebP.Image");
        changed |= SetRegStringIfChanged(HKEY_CURRENT_USER, "Software\\Classes\\WebP.Image\\DefaultIcon", iconRef);
    }

    if (changed) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    }
}

// ===========================================================================
// Helper: set a REG_DWORD value to 0 if it isn't already 0.
// Skips non-existent keys.
// ===========================================================================

static void SetRegDWordZero(const char* subKey, const char* valueName)
{
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, subKey, 0,
                      KEY_QUERY_VALUE | KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;

    DWORD current = 0, type = 0, size = sizeof(current);
    LSTATUS r = RegQueryValueExA(hKey, valueName, NULL, &type, (LPBYTE)&current, &size);
    if (r != ERROR_SUCCESS || type != REG_DWORD || current != 0) {
        DWORD zero = 0;
        RegSetValueExA(hKey, valueName, 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
    }
    RegCloseKey(hKey);
}

// ===========================================================================
// Construction
// ===========================================================================

WebPlugin::WebPlugin()
:   m_FormatInfo {
        .dwFlags     = 0,
        .dwID        = MAKE_FORMATID('W', 'E', 'B', 'P'),
        .szName      = "Google WebP Image",
        .szNameShort = "WEBP",
        .pszExtList  = (char*)"WEBP\0\0",    // double-null terminated list
        .szDefExt    = "WEBP",
        .color       = 0,
        .iIcon       = 0,
        .pszMimeType = NULL,
    },
    m_PlugInInfo {
        .dwFlags   = 0,
        .nVersion  = ID_VERSION,
        .szTitle   = "WebP Image Codec",
        .iIcon     = 0,
        .nFormats  = 1,
        .pFormatInfo = &m_FormatInfo,
    }
{
    // -- ACDSee workarounds (registry) --------------------------------------
    //
    // ViewerSharpen = 0
    //   "Sharpen subsampled images" freezes large animated images on the
    //   last frame during playback. Disabling avoids the hang.
    //
    // ViewerSSReadAhead = 0
    //   Pre-decoding the next animation frame (read-ahead) can exhaust the
    //   ~2 GB virtual address space of a 32-bit process.
    //
    SetRegDWordZero("Software\\ACD Systems\\ACDSee Pro\\50", "ViewerSharpen");
    SetRegDWordZero("Software\\ACD Systems\\ACDSee Pro\\50", "ViewerSSReadAhead");

    RegisterWebPIcon();
}

// ===========================================================================
// OpenImage - called by ACDSee to open a WebP file
// ===========================================================================
//
// ACDSee's call sequence:
//   IDP_Init -> IDP_OpenImage -> IDP_GetImageInfo / IDP_GetPageInfo(i)
//              -> IDP_PageDecode(i) -> IDP_CloseImage
//
// The decoded state handle (hs) is a WebPDecoder*.

int WebPlugin::OpenImage(ID_SourceInfo* psi, ID_StateHdl* phs)
{
    if (!psi || !phs) {
        DebugWebPTrace("WebPlugin::OpenImage: psi or phs is NULL -> IDE_InvalidParam");
        return IDE_InvalidParam;
    }

    DebugWebPTrace("WebPlugin::OpenImage: dwFlags=0x%lX pszFN=%s pBuf=%p dwLen=%lu pfFillBuffer=%p pParam=%p",
                   psi->dwFlags,
                   psi->pszFN ? psi->pszFN : "(null)",
                   psi->pBuf, psi->dwLen,
                   psi->pfFillBuffer, psi->pParam);

    // Fill the entire buffer if this is a virtual file (e.g. WebP inside ZIP)
    if ((psi->dwFlags & SIF_VIRTUALFILENAME) && psi->pfFillBuffer && psi->dwLen > 0) {
        DWORD dwNewPos = 0;
        if (!psi->pfFillBuffer(psi->pParam, psi->dwLen, &dwNewPos) || dwNewPos != psi->dwLen) {
            DebugWebPTrace("WebPlugin::OpenImage: pfFillBuffer FAILED (%lu/%lu) -> IDE_CorruptData",
                           dwNewPos, psi->dwLen);
            return IDE_CorruptData;
        }
    }

    // Create the decoder
    WebPDecoder* decoder = new (std::nothrow) WebPDecoder;
    if (!decoder) {
        DebugWebPTrace("WebPlugin::OpenImage: new WebPDecoder FAILED -> IDE_FatalError");
        return IDE_FatalError;
    }

    if (!decoder->decode({ psi->pBuf, psi->dwLen })) {
        DebugWebPTrace("WebPlugin::OpenImage: decode() FAILED -> IDE_UnknownFormat");
        delete decoder;
        return IDE_UnknownFormat;
    }

    *phs = decoder;
    DebugWebPTrace("WebPlugin::OpenImage: -> hs=%p (anim=%d frames=%d %dx%d)",
                   *phs, decoder->hasAnimated(), decoder->getFrameCount(),
                   decoder->getWidth(), decoder->getHeight());
    return IDE_OK;
}

// ===========================================================================
// GetImageInfo - image-level metadata (size, frame count, animation flag)
// ===========================================================================

int WebPlugin::GetImageInfo(ID_StateHdl hs, ID_ImageInfo* pii)
{
    DebugWebPTrace("WebPlugin::GetImageInfo: hs=%p", hs);
    if (!hs) {
        DebugWebPTrace("WebPlugin::GetImageInfo: hs is NULL -> IDE_InvalidParam");
        return IDE_InvalidParam;
    }

    WebPDecoder* decoder = static_cast<WebPDecoder*>(hs);
    if (decoder->getFrameCount() == 0) {
        DebugWebPTrace("WebPlugin::GetImageInfo: frameCount==0 -> IDE_CorruptData");
        return IDE_CorruptData;
    }

    pii->dwFlags    = decoder->hasAnimated() ? (IIF_ANIM | IIF_MULTIPAGE) : 0;
    pii->dwFormatID = MAKE_FORMATID('W', 'E', 'B', 'P');
    pii->si.cx      = decoder->getWidth();
    pii->si.cy      = decoder->getHeight();
    pii->nBPS       = decoder->getBitsPerSample();
    pii->nSPP       = decoder->hasAlpha() ? 4 : 3;
    pii->nPages     = decoder->getFrameCount();

    return IDE_OK;
}

// ===========================================================================
// GetPageInfo - per-frame metadata (dimensions, delay, alpha)
// ===========================================================================

int WebPlugin::GetPageInfo(ID_StateHdl hs, int iPage, ID_PageInfo* ppi)
{
    DebugWebPTrace("WebPlugin::GetPageInfo: hs=%p iPage=%d", hs, iPage);
    if (!hs) {
        DebugWebPTrace("WebPlugin::GetPageInfo: hs is NULL -> IDE_InvalidParam");
        return IDE_InvalidParam;
    }

    WebPDecoder* decoder = static_cast<WebPDecoder*>(hs);
    int nPages = decoder->getFrameCount();
    if (iPage < 0 || iPage >= nPages) {
        DebugWebPTrace("WebPlugin::GetPageInfo: iPage=%d out of range [0,%d) -> IDE_NoPage", iPage, nPages);
        return IDE_NoPage;
    }

    ppi->dwFlags  = decoder->hasAlpha() ? (PPF_RGB | PPF_ALPHA) : PPF_RGB;
    ppi->nSPP     = decoder->hasAlpha() ? 4 : 3;
    ppi->si.cx    = decoder->getWidth();
    ppi->si.cy    = decoder->getHeight();
    ppi->nBPS     = decoder->getBitsPerSample();
    ppi->nDelay   = decoder->getFrameDelay(iPage);
    ppi->szTitle[0] = 0;

    return IDE_OK;
}

// ===========================================================================
// PageDecode - decode a frame and return it as a DIB (HGLOBAL)
// ===========================================================================
//
// Output is a 24-bit bottom-up BMP DIB (BITMAPINFOHEADER + pixel data).
// The caller (ACDSee) owns the HGLOBAL and will GlobalFree() it.

int WebPlugin::PageDecode(ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio)
{
    if (!hs || !pdp || !pio) {
        DebugWebPTrace("WebPlugin::PageDecode: hs, pdp, or pio is NULL -> IDE_InvalidParam");
        return IDE_InvalidParam;
    }

    DebugWebPTrace("WebPlugin::PageDecode: hs=%p nPage=%d nWidth=%d nHeight=%d dwFlags=0x%lX quality=%d",
                   hs, pdp->nPage, pdp->nWidth, pdp->nHeight, pdp->dwFlags, pdp->quality);

    WebPDecoder* decoder = static_cast<WebPDecoder*>(hs);
    int nPages = decoder->getFrameCount();
    if (pdp->nPage < 0 || pdp->nPage >= nPages) {
        DebugWebPTrace("WebPlugin::PageDecode: nPage=%d out of range -> IDE_NoPage", pdp->nPage);
        return IDE_NoPage;
    }

    // Resolve output dimensions and allocate DIB
    const auto [dstW, dstH] = decoder->resolveOutputSize(pdp->nWidth, pdp->nHeight);
    const int    dstStride  = ((dstW * 3 + 3) / 4) * 4;      // 4-byte aligned
    const size_t imageSize  = (size_t)dstStride * dstH;
    const size_t dibSize    = sizeof(BITMAPINFOHEADER) + imageSize;

    HGLOBAL hDIB = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, dibSize);
    if (!hDIB) {
        DWORD err = GetLastError();
        MEMORYSTATUSEX ms = {};
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        DebugWebPTrace("WebPlugin::PageDecode: GlobalAlloc FAILED"
                       " frame=%d/%d size=%dx%d dibSize=%zu KB"
                       " availVirt=%zu MB availPhys=%zu MB LastError=%lu -> IDE_TooBig",
                       pdp->nPage, nPages,
                       dstW, dstH, dibSize / 1024,
                       (size_t)ms.ullAvailVirtual / (1024 * 1024),
                       (size_t)ms.ullAvailPhys / (1024 * 1024),
                       err);
        return IDE_TooBig;
    }

    // Fill BITMAPINFOHEADER (24-bit bottom-up BI_RGB)
    BITMAPINFOHEADER* pBih = static_cast<BITMAPINFOHEADER*>(hDIB);
    pBih->biSize        = sizeof(BITMAPINFOHEADER);
    pBih->biWidth       = dstW;
    pBih->biHeight      = dstH;
    pBih->biPlanes      = 1;
    pBih->biBitCount    = 24;
    pBih->biCompression = BI_RGB;
    pBih->biSizeImage   = (DWORD)imageSize;

    // Decode pixel data into buffer after the BITMAPINFOHEADER
    uint8_t* pDst = static_cast<uint8_t*>(hDIB) + sizeof(BITMAPINFOHEADER);
    if (!decoder->getFrame(pdp->nPage, pDst, dstStride, dstW, dstH)) {
        GlobalFree(hDIB);
        DebugWebPTrace("WebPlugin::PageDecode: getFrame FAILED -> IDE_CorruptData");
        return IDE_CorruptData;
    }

    // Populate output struct
    pio->dwFlags = 0;
    pio->hdib    = hDIB;
    pio->hemf    = NULL;
    pio->rc.left   = 0;
    pio->rc.top    = 0;
    pio->rc.right  = dstW;
    pio->rc.bottom = dstH;
    pio->pParamEx  = NULL;

    return IDE_OK;
}

// ===========================================================================
// CloseImage - destroy the decoder created by OpenImage
// ===========================================================================

int WebPlugin::CloseImage(ID_StateHdl hs)
{
    DebugWebPTrace("WebPlugin::CloseImage: hs=%p", hs);
    delete static_cast<WebPDecoder*>(hs);
    return IDE_OK;
}

// ===========================================================================
// ShowPlugInDialog - "About" dialog
// ===========================================================================

void WebPlugin::ShowPlugInDialog(HWND hWndParent)
{
    DialogBox(WebPlugin::g_hModule, MAKEINTRESOURCE(IDD_ABOUT),
              hWndParent, AboutDlgProc);
}

// ===========================================================================
// AboutDlgProc - About dialog window procedure
// ===========================================================================

INT_PTR CALLBACK WebPlugin::AboutDlgProc(HWND hDlg, UINT message,
                                         WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG: {
        char version[64] = {0};
        LoadString(WebPlugin::g_hModule, IDS_VERSION, version, sizeof(version));
        SetDlgItemText(hDlg, IDC_VERSION, version);
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}

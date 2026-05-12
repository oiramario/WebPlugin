#include "ID_APIWrapper.h"
#include <windows.h>
#include <shlobj.h>
#include <string.h>
#include "res/resource.h"
#include "WebPDecoder.h"

HMODULE ID_APIWrapper::g_hModule = NULL;

// Write `value` to the default REG_SZ entry of (root, subKey) only if the current
// value differs (or is absent). Returns true if a write actually happened.
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

void ID_APIWrapper::RegisterWebPIcon()
{
    char path[MAX_PATH];
    if (!GetModuleFileNameA(g_hModule, path, MAX_PATH))
        return;

    char iconRef[MAX_PATH + 16];
    sprintf_s(iconRef, "%s,-%d", path, IDI_ICON_WEBP);

    bool changed = false;

    // Read current ProgID for .webp
    char progID[128] = {0};
    DWORD size = sizeof(progID);
    if (RegGetValueA(HKEY_CLASSES_ROOT, ".webp", NULL, RRF_RT_REG_SZ, NULL, progID, &size) == ERROR_SUCCESS) {
        // Override icon on existing ProgID under HKCU
        char keyPath[256];
        sprintf_s(keyPath, "Software\\Classes\\%s\\DefaultIcon", progID);
        changed |= SetRegStringIfChanged(HKEY_CURRENT_USER, keyPath, iconRef);
    } else {
        // No ProgID: create minimal association with icon
        changed |= SetRegStringIfChanged(HKEY_CURRENT_USER, "Software\\Classes\\.webp", "WebP.Image");
        changed |= SetRegStringIfChanged(HKEY_CURRENT_USER, "Software\\Classes\\WebP.Image\\DefaultIcon", iconRef);
    }

    // Only notify Explorer if we actually touched something
    if (changed) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    }
}

ID_APIWrapper::ID_APIWrapper(ID_ClientInfo* /*pci*/)
:   m_szFormatInfo {
        {
            .dwFlags = 0, // CIF_REGISTERED
            .dwID = MAKE_FORMATID('W', 'E', 'B', 'P'),
            .szName = "MasterZ / oiramario",
            .szNameShort = "WEBP",
            .pszExtList = (char*)"WEBP\0\0",
            .szDefExt = "WEBP",
            .color = 0,
            .iIcon = 0,
            .pszMimeType = NULL
        }
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
    // Force disable "Sharpen subsampled images" which causes hang
    // on large animated WebP (takes effect next ACDSee launch).
    // Only write if currently non-zero or missing.
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

    // Register WebP icon for Windows Explorer
    RegisterWebPIcon();
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

    if (!decoder->decode({psi->pBuf, psi->dwLen}))
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
        pii->nSPP = decoder->hasAlpha() ? 4 : 3;
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

        ppi->dwFlags = decoder->hasAlpha() ? PPF_RGB | PPF_ALPHA : PPF_RGB;
        ppi->nSPP = decoder->hasAlpha() ? 4 : 3;
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

    HGLOBAL hDIB = GlobalAlloc(GMEM_FIXED, decoder->getDIBSize());
    if (!hDIB) {
        return IDE_Error;
    }
    decoder->writeDIB(pdp->nPage, hDIB);

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

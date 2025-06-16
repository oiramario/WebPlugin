#include "ID_APIWrapper.h"
#include <windows.h>
#include <string.h>
#include "res/resource.h"
#include "WebPDecoder.h"

HMODULE ID_APIWrapper::g_hModule = NULL;

ID_APIWrapper::ID_APIWrapper(ID_ClientInfo* pci)
:   m_WebPFormatInfo {
        .dwFlags = CIF_REGISTERED,
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
        int nPages = (size_t)decoder->getFrames().size();
        if (iPage < 0 || iPage > nPages) {
            // MessageBox(NULL, "IDE_InvalidParam", "IDP_GetPageInfo", MB_OK);
            return IDE_NoPage;
        }

        ppi->dwFlags |= decoder->hasAlpha() ? PPF_RGB | PPF_ALPHA : PPF_RGB;
        ppi->si.cx = decoder->getWidth();
        ppi->si.cy = decoder->getHeight();
        ppi->nBPS = 8;
        ppi->nSPP = decoder->hasAlpha() ? 4 : 3;
        ppi->nDelay = 100; // decoder->getDurations()[iPage];
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

bool SaveDIBToBMP(HGLOBAL hDIB, const char* filePath);
int ID_APIWrapper::PageDecode(ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio)
{
    // if (pdp->dwFlags && PID_RECT) {
    //     return IDE_InvalidParam;
    // }

    if (hs)
    {
        WebPDecoder* decoder = (WebPDecoder*)hs;
        const auto& frames = decoder->getFrames();
        int nPages = (size_t)frames.size();
        if (pdp->nPage < 0 || pdp->nPage > nPages) {
            // MessageBox(NULL, "IDE_InvalidParam", "IDP_GetPageInfo", MB_OK);
            return IDE_NoPage;
        }

        int idx = GetTickCount() % nPages; // pdp->nPage
        auto& frame = frames[idx];
        auto size = frame.data.size();
        auto data = frame.data.data();

        // char msg[256] = {0};
        // sprintf_s(msg, 256, "nPage=%d", idx);
        // MessageBox(NULL, msg, "IDP_PageDecode", MB_OK);

        int width = decoder->getWidth();
        int height = decoder->getHeight();
        HGLOBAL hDIB = GlobalAlloc(GMEM_MOVEABLE, size);
        uint8_t* pMem = (uint8_t*)GlobalLock(hDIB);
        memcpy(pMem, data, size);
        GlobalUnlock(hDIB);

        pio->dwFlags = 0;
        pio->hdib = hDIB;
        pio->hemf = NULL;
        pio->rc.bottom = 0;
        pio->rc.right = 0;
        pio->rc.left = 0;
        pio->rc.top = 0;
        pio->pParamEx = NULL;

        // char name[256] = {0};
        // sprintf_s(name, 256, "d:\\%d.bmp", idx);
        // SaveDIBToBMP(hDIB, name);

        return IDE_OK;
    }
    else
    {
        return IDE_InvalidParam;
    }
}

#include <fstream>

bool SaveDIBToBMP(HGLOBAL hDIB, const char* filePath) {
    if (!hDIB || !filePath) {
        return false;
    }

    // 锁定内存获取指针
    void* pDIB = GlobalLock(hDIB);
    if (!pDIB) {
        return false;
    }

    // 获取BITMAPINFOHEADER
    BITMAPINFOHEADER* pBMIH = (BITMAPINFOHEADER*)pDIB;
    
    // 计算位图数据大小
    DWORD dwBmBitsSize = ((pBMIH->biWidth * pBMIH->biBitCount + 31) / 32) * 4 * pBMIH->biHeight;
    
    // 如果有调色板，计算调色板大小
    DWORD dwPaletteSize = 0;
    if (pBMIH->biBitCount <= 8) {
        // 对于8位及以下的位图，调色板大小为2^biBitCount个RGBQUAD
        dwPaletteSize = (1 << pBMIH->biBitCount) * sizeof(RGBQUAD);
    }
    
    // 创建BMP文件头
    BITMAPFILEHEADER bmfh;
    bmfh.bfType = 0x4D42; // "BM"
    bmfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + 
                  dwPaletteSize + dwBmBitsSize;
    bmfh.bfReserved1 = 0;
    bmfh.bfReserved2 = 0;
    bmfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwPaletteSize;
    
    // 打开文件进行写入
    std::ofstream file(filePath, std::ios::binary);
    if (!file) {
        GlobalUnlock(hDIB);
        return false;
    }
    
    // 写入文件头
    file.write((char*)&bmfh, sizeof(BITMAPFILEHEADER));
    
    // 写入信息头
    file.write((char*)pBMIH, sizeof(BITMAPINFOHEADER));
    
    // 写入调色板（如果有）
    if (dwPaletteSize > 0) {
        char* pPalette = (char*)pBMIH + sizeof(BITMAPINFOHEADER);
        file.write(pPalette, dwPaletteSize);
    }
    
    // 写入位图数据
    char* pBits = (char*)pBMIH + sizeof(BITMAPINFOHEADER) + dwPaletteSize;
    file.write(pBits, dwBmBitsSize);
    
    // 关闭文件
    file.close();
    
    // 解锁内存
    GlobalUnlock(hDIB);
    
    return true;
}

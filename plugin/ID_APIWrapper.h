#ifndef ID_APIWrapper_h
#define ID_APIWrapper_h

#include "ID_PlugIn.h"

class ID_APIWrapper
{
public:
    ID_APIWrapper(ID_ClientInfo* pci);
    ~ID_APIWrapper();

    void ShowPlugInDialog(HWND hWndParent);
    int OpenImage(ID_SourceInfo* psi, ID_StateHdl* phs);
    int CloseImage(ID_StateHdl hs);
    int GetImageInfo(ID_StateHdl hs, ID_ImageInfo* pii);
    int GetPageInfo(ID_StateHdl hs, int iPage, ID_PageInfo* ppi);
    int PageDecode(ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio);

    const ID_PlugInInfo* GetPlugInInfo() const { return &m_PlugInInfo; }

private:
    static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

private:
    ID_ClientInfo m_ClientInfo;
    ID_FormatInfo m_WebPFormatInfo;
    ID_FormatInfo m_szFormatInfo[1];
    ID_PlugInInfo m_PlugInInfo;

public:
    static HMODULE g_hModule;
};

#endif

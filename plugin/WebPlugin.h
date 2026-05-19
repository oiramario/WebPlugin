#ifndef WebPlugin_h
#define WebPlugin_h

#include "ID_PlugIn.h"
#include "lancir.h"

class WebPlugin
{
public:
    WebPlugin();
    ~WebPlugin() = default;

    void ShowPlugInDialog(HWND hWndParent);
    int OpenImage(ID_SourceInfo* psi, ID_StateHdl* phs);
    int CloseImage(ID_StateHdl hs);
    int GetImageInfo(ID_StateHdl hs, ID_ImageInfo* pii);
    int GetPageInfo(ID_StateHdl hs, int iPage, ID_PageInfo* ppi);
    int PageDecode(ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio);

    ID_PlugInInfo* GetPlugInInfo() { return &m_PlugInInfo; }

private:
    static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    ID_FormatInfo m_FormatInfo;
    ID_PlugInInfo m_PlugInInfo;
    avir::CLancIR m_Resizer;

public:
    static HMODULE g_hModule;
};

#endif

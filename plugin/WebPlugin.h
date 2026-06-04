// -- WebPlugin - ACDSee Image Decoder Plug-in class ----------------------
//
// Implements the ACDSee Image Decoder API (ID_PlugIn.h). Each public
// method maps to one IDP_* export. Lifetime model:
//   IDP_Init -> OpenImage(+/-) -> GetImageInfo / GetPageInfo / PageDecode
//             -> CloseImage -> IDP_CloseImage
//
// See WebPlugin.cpp for the full implementation and registry side-effects.
//

#ifndef WebPlugin_h
#define WebPlugin_h

#include "ID_PlugIn.h"

class WebPlugin {
public:
    WebPlugin();
    ~WebPlugin() = default;

    ID_PlugInInfo* GetPlugInInfo() { return &m_PlugInInfo; }

    // -- ACDSee Image Decoder API ------------------------------------------
    int OpenImage   (ID_SourceInfo* psi, ID_StateHdl* phs);
    int GetImageInfo(ID_StateHdl hs, ID_ImageInfo* pii);
    int GetPageInfo (ID_StateHdl hs, int iPage, ID_PageInfo* ppi);
    int PageDecode  (ID_StateHdl hs, ID_DecodeParam* pdp, ID_ImageOut* pio);
    int CloseImage  (ID_StateHdl hs);

    void ShowPlugInDialog(HWND hWndParent);

    // Module handle set by DllMain - used for resource loading
    static HMODULE g_hModule;

private:
    // About dialog procedure
    static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT message,
                                         WPARAM wParam, LPARAM lParam);

    ID_FormatInfo m_FormatInfo;
    ID_PlugInInfo m_PlugInInfo;
};

#endif // WebPlugin_h

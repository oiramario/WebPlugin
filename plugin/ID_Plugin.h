#ifndef _ID_Plugin_hpp__
#define _ID_Plugin_hpp__

#include <windows.h>

#ifndef PLUGIN_API
#define PLUGIN_API extern "C"
#endif

typedef void* ID_StateHdl;

///////////////////
// MAKE_FORMATID //
///////////////////
#ifndef MAKE_FORMATID
#define MAKE_FORMATID(a,b,c,d) (((DWORD)(d)<<24) | ((DWORD)(c)<<16) | ((DWORD)(b)<<8) | ((DWORD)(a)))
#endif

/////////////////////
// Plug-in version //
/////////////////////
#define ID_VERSION 200

//////////////////
// Status codes //
//////////////////
enum  // error codes:
{
    IDE_OK                  =  0,  // success
    IDE_Error               = -1,  // unspecified error
    IDE_InvalidOp           = -2,  // operation cannot be completed at this time
    IDE_InvalidParam        = -3,  // invalid parameters
    IDE_NotImplemented      = -4,  // missing implementation
    IDE_TruncatedData       = -5,  // premature EOF encountered
    IDE_UnknownFormat       = -6,  // unknown input format
    IDE_CorruptData         = -7,  // bad format / corrupt data
    IDE_TooBig              = -8,  // image is too big
    IDE_NoPage              = -9,  // specified page does not exist
    IDE_CreateBitmap        = -10, // error creating bitmap
    IDE_Cancelled           = -11, // operation cancelled by user
    IDE_NoMetadata          = -12, // no metadata exists for the specified page of the image
    IDE_NoAudio             = -13, // no audio clip exists for the image
    IDE_FatalError          = -14, // Unhandled exception
    IDE_EmbeddedData        = -15, // no embedded data exists for the specified page of the image
    IDE_NoColorProfile      = -16, // no color profile exists for the image

    // other errors...

    // Plug-in defined errors start at -1000
    IDE_FirstPlugInError    = -1000,
};

/////////////////
// CIF_* flags //
/////////////////
#define CIF_REGISTERED 1 // Calling application is registered

/////////////////
// IIF_* flags //
/////////////////
enum
{
    IIF_MULTIPAGE       = 1,  // Image has multiple pages
    IIF_ANIM            = 2,  // Image is an animation
    IIF_METADATATYPES   = 64, // Image has 1 or more metadata types and the metadata types variables are valid
    IIF_XTRANS          = 128 // This is a raw image with an XTrans sensor
};

/////////////////
// PPF_* flags //
/////////////////
// Note: The PPF_DPI and PPF_TRANSCOLOR flags are mutually exclusive
enum
{
    PPF_RGB             = 1,    // Image data is RGB
    PPF_GRAYSCALE       = 2,    // Image data is grayscale
    PPF_COLORMAP        = 4,    // Image data is colormapped
    PPF_ALPHA           = 8,    // Image has alpha channel
    PPF_VECTOR          = 16,   // Image is vector-based
    PPF_DPI             = 64,   // The siDPI field is valid
    PPF_TRANSCOLOR      = 512,  // This page has a transparent colour and the iTransColor field is valid
    PPF_METADATATYPES   = 2048  // Page has 1 or more metadata types and the metadata types variables are valid
};

/////////////////
// PID_* flags //
/////////////////
enum
{
    PID_RECT                     = 1,   // Decode only the given rectanglular portion of the image
    PID_GET16BPC                 = 2,   // Decode 16-bits per channel (if available)
    PID_NEEDALPHACHANNEL         = 4,   // doesnt degrade alpha channeled images. ie 32 stays as 32 not returned 24bpp
    PID_DEMOSAIKED               = 8,	// Demosaik the RAW image using pixel grouping (will not modify original data) return default RPP(XML)
    PID_APPLY_DEVELOP_PROCESSING = 128, // Apply all develop processing to the RAW. Brushing is not supported at the moment 
};

/////////////////
// FIF_* flags //
/////////////////
enum
{
   FIF_THUMBNAILONLY = 1,  // Plug-in only generates a thumbnail representation of the file
   FIF_VIEWWINDOW    = 2,  // Plug-in can create a view window for displaying the image
   FIF_ISVIDEO       = 4,  // This "image" is actually a video file
   FIF_ISAUDIO       = 8,  // This "image" is actually an audio file
   FIF_HAS16BPC      = 16, // 16-bits per channel decode available
   FIF_ISRAW         = 32, // This "image" is actually a RAW file
   FIF_ISVECTOR      = 64, // This image is in vector format
   FIF_ISDOCUMENT    = 128,// This "image" is an office document
};


/////////////////
// IOF_* flags //
/////////////////
enum
{
   IOF_HASRAWPARAMEX   = 1,  // Has RAW ParamEx structure available
   IOF_ISTHUMBNAIL     = 2,  // Output image has been decoded from an embedded thumbnail image
   IOF_ISPREVIEW       = 4,  // Output image has been decoded from an embedded preview image
   IOF_ISROTATED       = 8,  // Output image has been rotated based on metadata (e.g. EXIF orientation)
   IOF_ISPREMULTIPLIED = 16, // Some formats require that the transparency value is applied to the pixel data
};

/////////////////
// POF_* flags //
/////////////////
enum
{
    POF_TOPDOWN     = 1,     // Output is by rows starting from the top
    POF_BOTTOMUP    = 2,     // Output is by rows starting from the bottom
    POF_LEFTTORIGHT = 4,     // Output is by columns starting from the left
    POF_RIGHTTOLEFT = 8,     // Output is by columns starting from the right
    POF_INTERLACED  = 16,    // Output is by random row
    POF_MULTIPASS   = 32,    // Output is done in multiple passes
    POF_TILED       = 64,    // Output is tiled
    POF_METAFILE    = 128,   // A metafile is generated
    POF_RECT        = 256,   // Only the given rectanglular portion of the image will be decoded
    POF_ISTHUMBNAIL = 512,   // Output image has been decoded from an embedded thumbnail image
    POF_ISPREVIEW   = 1024,  // Output image has been decoded from an embedded preview image
    POF_ISROTATED   = 2048,  // Output image has been rotated based on metadata (e.g. EXIF orientation)
};

///////////////////////
// PIQ_* enumeration //
///////////////////////
enum
{
   PIQ_USEEMBEDDEDTHUMBNAIL               = 0,  // Use an embedded thumbnail when available and applicable
   PIQ_SPEED                              = 1,  // Favour speed over quality (for display)
   PIQ_QUALITY                            = 2,  // Favour quality over speed (for editing/conversion)
   PIQ_USEEMBEDDEDTHUMBNAILALWAYSIFEXISTS = 3,  // Use the embedded thumbnail always if it exists
   PIQ_USEPREVIEWIMAGE                    = 4,  // Use the embedded preview image
   PIQ_DECODEFORRAWENCODE                 = 5,  // Used for encoding raw to DNG
   PIQ_USEFULLSIZEPREVIEWIMAGE            = 6   // Use the full size embedded preview image (it's usually faster in the viewer to decode the 
                                                // full size embedded JPG & avoid all the resampling)
};

///////////////////
// ID_ClientInfo //
///////////////////
struct ID_ClientInfo
{
    DWORD   dwFlags;       // CIF_* flags
    char    szCompany[40]; // Name of company of calling application
    char    szAppName[40]; // Name of calling application.
};

///////////////////
// ID_FormatInfo //
///////////////////
// Specifies information about an image format supported bythe plug-in.
// The plug-in owns the memory pointed to by pszExtList and must not free it
// until the plug-in library is unloaded.
struct ID_FormatInfo
{
    DWORD       dwFlags;        // FIF_* flags
    DWORD       dwID;           // unique identifier for this format
    char        szName[40];     // name of this format (e.g., "Windows BMP")
    char        szNameShort[8]; // short name of this format (e.g., "BMP")
    char*       pszExtList;     // list of filename extensions for this format (e.g., "BMP\0DIB\0RLE\0")
    char        szDefExt[8];    // default extension for this format (e.g., "BMP")
    COLORREF    color;          // background colour to use when highlighting
    UINT        iIcon;          // index of icon to display in Explorer
    char*       pszMimeType;    // MIME type name
};

///////////////////
// ID_PlugInInfo //
///////////////////
// Specifies information about the plug-in.
// The plug-in owns the memory pointed to by pFormatInfo and must not free it until
// the plug-in library is unloaded.
struct ID_PlugInInfo
{
    DWORD            dwFlags;     // Flags -- set to 0
    int              nVersion;    // Plug-in specificiation version (100)
    char             szTitle[40]; // Plug-in title
    UINT             iIcon;       // Plug-in icon resource id
    int              nFormats;    // Number of formats supported by plug-in
    ID_FormatInfo*   pFormatInfo; // Information about each format supported
};

//////////////////
// ID_ImageInfo //
//////////////////
// Information about an image.
struct ID_ImageInfo
{
    DWORD       dwFlags;          // IIF_* flags
    DWORD       dwFormatID;       // format identifier
    SIZE        si;               // image dimensions
    int         nBPS;             // bits per sample
    int         nSPP;             // samples per pixel
    int         nPages;           // number of pages
};

///////////////////
// ID_SourceInfo //
///////////////////
// Defines the image source data.
// The archive plug-in must make a copy of the content of ID_SourceInfo.
// It may not retain a pointer to either ID_SourceInfo or its pszFN member
struct ID_SourceInfo
{
   ID_SourceInfo() { ZeroMemory(this, sizeof(*this)); };

   DWORD    dwFlags; // SIF_* flags
   char*    pszFN;   // Name of image file, if applicable (can be NULL).
   BYTE*    pBuf;    // Pointer to image file data
   DWORD    dwLen;   // Byte length of image file
    
                     // Call-back function to read more data info buffer. 
                     // The function attempts to read as much additional data into buf as
                     // necessary to make dwBufRead at least as large as dwPos.
                     // Returns TRUE if successful, FALSE otherwise.
   BOOL(*pfFillBuffer)(void* pUserParam, DWORD dwPos, DWORD* pdwNewPos);
   void* pParam;     // Calling application can set this to whatever it likes
};

//////////////////
// IDP_PageInfo //
//////////////////
// Information about a page of an image.
struct ID_PageInfo
{
    DWORD       dwFlags;          // PPF_* flags
    SIZE        si;               // Page dimensions
    int         nBPS;             // Bits per sample
    int         nSPP;             // Samples per pixel
    int         nDelay;           // Frame delay in ms (for animation)
    char        szTitle[32];      // Page title 
};

/////////////////
// ID_ImageOut //
/////////////////
// Defines the output of the ID_PageDecode function.
// The calling application is responsible for freeing hdib and hemf
// using GlobalFree() and DeleteEnhMetaFile(), resp.
struct ID_ImageOut
{
   DWORD            dwFlags;    // IOF_* flags
   HGLOBAL          hdib;       // handle to DIB
   HENHMETAFILE     hemf;       // handle to an enhanced metafile
   RECT             rc;         // Rectangle defining area of source image decoded
   void*		    pParamEx;   // Extended parameter
};

/////////////////////
// ID_ProgressFunc //
/////////////////////
// Application-supplied progress callback function.
// Called by the plug-in periodically during image decoding.
// The decoding progress is indicated by (nProgressNum/nProgressDen), which 
// will vary over the decoding process starting with 0 and ending with 1 
// (nProgressNum==nProgressDen).
// Returns: The application returns a non-zero value to continue decoding.
// If zero is returned, then the plug-in should abort the decode process and
// return IDE_Cancelled.
// 
//  pParam           Application-defined value
//  nProgressNum     Numerator of progress
//  nProgressDen     Denominator of progress
typedef int(__stdcall *ID_ProgressFunc)(void* pParam, int nProgressNum, int nProgressDen);

////////////////////
// ID_DecodeParam //
////////////////////
struct ID_DecodeParam
{
    DWORD               dwFlags;    // PID_* flags
    int                 nPage;      // Page to decode
    RECT                rc;         // Region of page to decode, in source pixels (if PID_RECT specified)
    int                 nWidth;     // Desired width  of output image/region
    int                 nHeight;    // Desired height of output image/region
    int                 quality;    // PIQ_* value
    ID_ProgressFunc     pf;         // Progress callback function
    void*               pParam;     // Progress callback parameter
    void*	            pParamEx;   // Extended parameter
};

////////////////////
// ID_OutputParam //
////////////////////
struct ID_OutputParam
{
   ID_OutputParam() { ZeroMemory(this, sizeof(*this)); };

   DWORD			dwFlags;        // POF_* flags
   RECT				rc;             // Region of source page to be decoded, in source pixels (if POF_RECT specified)
   int				nWidth;         // Output width
   int				nHeight;        // Output height
   int				bpp;            // Output bits per pixel (1, 4, 8, 16 or 24)
   int				nColorMapLen;   // #Entries in output colormap
   RGBQUAD			colormap[256];  // Output colormap   
};

///////////////////
// ID_StepOutput //
///////////////////
struct ID_StepOutput
{
   ID_StepOutput() { ZeroMemory(this, sizeof(*this)); };

   DWORD dwFlags;          // Flags -- set to 0
   RECT  rc;               // Rectangle defining area of image decoded (in dest. pixels)
   BYTE* pBuf;             // Pointer to buffer containing output data.  (The Plug-in owns the buffer.  The calling application will use the buffer right away and will not modify its content.)
   int   nProgressNumPage; // Decoding progress of current page numerator
   int   nProgressDenPage; // Decoding progress of current page denominator
   int   nProgressNumAll;  // Decoding progress of entire image (all pages) numerator  
   int   nProgressDenAll;  // Decoding progress of entire image (all pages) denominator
};

#endif

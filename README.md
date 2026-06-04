# ID_WebP - WebP Image Decoder Plug-in for ACDSee Pro 5

A Win32 plug-in (`.apl` DLL) that adds native **WebP image decoding** support
to **ACDSee Pro 5**. Implements the ACDSee IDP (Image Decoder Plug-in)
interface using vendored [libwebp](https://chromium.googlesource.com/webm/libwebp/)
for both static and animated WebP.

---

## Features

- **Static WebP decoding** -- Decodes to 24-bit BGR bottom-up DIB (ACDSee's
  native bitmap format), with or without alpha channel.
- **Animated WebP playback** -- Sequential frame iteration via
  `WebPAnimDecoderGetNext`, with per-frame duration metadata extracted at
  open time through a separate `WebPDemux` pass.
- **Alpha blending** -- Transparent/semi-transparent pixels are composited
  onto an 8-pixel checkerboard pattern (grayscale 224/192) at decode time,
  since ACDSee only accepts 24-bit BGR.
- **On-the-fly scaling** -- libwebp's built-in `use_scaling` is activated
  only when the requested output area is less than 55% of the original
  (benchmarked breakeven point), saving decode time and memory.
- **Registry workarounds** -- Automatically disables `ViewerSharpen` ("Sharpen
  subsampled images") which freezes on large animated WebP, and
  `ViewerSSReadAhead` (read-ahead decode) which exhausts the 32-bit virtual
  address space.
- **Explorer icon** -- Registers a `.webp` file icon in Windows Explorer via
  HKCU overlay. The icon is embedded in the plug-in DLL itself.
- **Virtual file support** -- Handles `SIF_VIRTUALFILENAME` with
  `pfFillBuffer` callback, enabling WebP files inside ZIP archives to be
  decoded correctly.
- **Lightweight** -- Single DLL (~50 KB Release build). The only external
  dependency is vendored libwebp, compiled decoder-only with all
  examples/tools/tests/encoder disabled.

## Architecture

```
codec/                   Static library (WebPCodec)
  WebPDecoder.h/.cpp     WebPDecoder -- wraps libwebp decode + demux APIs
  libwebp/               Vendored libwebp (decode + demux only)
plugin/                  Shared library (ID_WebP.apl)
  ID_WebP.cpp            ACDSee IDP exports -- thin delegation to g_pWebPlugin
  WebPlugin.h/.cpp       Plugin logic: WebPlugin, ACDSee API implementation
  ID_WebP.def            DLL export definitions
  dllmain.cpp            DLL entry point (module handle, singleton cleanup)
  res/                   Icon, version resources, About dialog
  ID_PlugIn.h            ACDSee Image Decoder Plug-in SDK header (vendored)
```

### Data flow

```
ACDSee Pro
    | IDP_* exports
ID_WebP.cpp (thin delegation)
    |
WebPlugin (handle management, DIB allocation)
    |
WebPDecoder (libwebp wrapper -- decode, composite, scale)
    |
libwebp (WebPBitstreamFeatures / WebPDecode / WebPAnimDecoder)
    |
  -> 24-bit BGR DIB (HGLOBAL) returned to ACDSee
```

### ACDSee API call sequence

```
IDP_Init
  -> IDP_OpenImage              (create WebPDecoder, probe bitstream)
    -> IDP_GetImageInfo         (dimensions, frame count, animation flag)
    -> IDP_GetPageInfo(i)       (per-frame delay, alpha)
    -> IDP_PageDecode(i)        (decode frame -> HGLOBAL DIB)
    -> ...
  -> IDP_CloseImage             (destroy decoder)
```

The decoder handle (`ID_StateHdl`) is a `WebPDecoder*` passed as an opaque
pointer. All calls are single-threaded serial.

## Build

```bat
cmake -B build -A Win32
cmake --build build --config Release
```

**Output**: `build/bin/Release/`

| Artifact | Description |
|----------|-------------|
| `ID_WebP.apl` | ACDSee plug-in (copy to PlugIns directory) |

### Prerequisites

- **OS**: Windows (tested on Windows 10)
- **Host**: ACDSee Pro 5 (32-bit)
- **Build**: Visual Studio with C++ desktop workload, CMake >= 3.10
- **Target**: **Win32 (x86)** -- ACDSee Pro 5 is a 32-bit process

### Debug build

```bat
cmake -B build -A Win32 -DDEBUG_WEBP_TRACE=ON
cmake --build build --config Release
```

The codec and plugin write diagnostic output via `OutputDebugStringA`,
visible in **DebugView** or **WinDbg**. The `DebugWebPTrace` macro
compiles to a no-op when `DEBUG_WEBP_TRACE` is OFF.

## Key design decisions

- **24-bit BGR DIB is a data constraint, not a display choice** -- ACDSee's
  Image Decoder API only accepts 24-bit bottom-up DIBs. Alpha is composited
  onto a checkerboard background at decode time because there is no alpha
  channel in the output format.
- **Animated decode requires `MODE_BGRA`** -- libwebp's `WebPAnimDecoder`
  rejects non-alpha color modes. Animated frames are always decoded to
  BGRA then composited to BGR.
- **Frame durations decoupled from pixel decoding** -- A separate `WebPDemux`
  pass at open time reads all per-frame delays into a vector. The animation
  decoder handles only pixel data.
- **Sequential-only animation access** -- `WebPAnimDecoderGetNext` is a
  one-way iterator. Random-access seeking is not supported; `PageDecode`
  must be called in order 0..n-1. Resetting to frame 0 calls
  `WebPAnimDecoderReset`.
- **Scaling threshold at 55% area** -- libwebp scaling at decode time saves
  memory only when the target is significantly smaller than the original.
  The breakeven is ~74% linear scale (= 55% area); below that it is cheaper
  to decode full-size and let ACDSee downscale.
- **Non-owning span** -- `WebPDecoder` stores a `std::span` into the source
  buffer rather than copying it, relying on ACDSee's guarantee that `pBuf`
  remains valid between `OpenImage` and `CloseImage`.
- **Registry side-effects in constructor** -- `ViewerSharpen` and
  `ViewerSSReadAhead` are set to 0 every time the plug-in loads. These are
  not optional clean-up items; removing them regresses stability on large
  animated WebP files.
- **`pfFillBuffer` for ZIP'd WebP** -- `OpenImage` must call the source's
  `pfFillBuffer` callback to pull the full file into memory when the
  WebP is inside a ZIP archive; otherwise the buffer may be incomplete.

## Debugging

- Use `DebugView` or `WinDbg` to capture `OutputDebugStringA` traces
- Enable with `-DDEBUG_WEBP_TRACE=ON` (compiles to no-op in Release without it)
- The About dialog (right-click the plug-in in ACDSee) shows the version
- `ID_WebP.apl` can be copied directly to ACDSee's `PlugIns` directory

## Technical details

- **DIB layout**: 24-bit bottom-up `BI_RGB`, 4-byte aligned stride
  (`((width * 3 + 3) / 4) * 4`). The `BITMAPINFOHEADER` is followed
  immediately by pixel data in BGR order. Negative stride writes from
  the bottom row upward.
- **Alpha compositing rounding**: The `compositeBGRAtoBGR` function uses
  `(value + 128 + (value >> 8)) >> 8` instead of plain `value / 256` to
  avoid truncation bias. The checkerboard cell size is 8x8 pixels.
- **Scaling economics**: `resolveOutputSize` compares `reqW * reqH` against
  `width * height * 55 / 100`. At 55% area the decode+scale cost equals
  the full-decode cost. Below that threshold, libwebp's built-in scaling
  saves both time and memory.
- **Frame delay fallback**: If `WebPDemuxGetFrame` fails for a particular
  frame (corrupt metadata), the delay defaults to 100 ms.
- **`has_alpha` propagation**: `GetImageInfo.nSPP` reports 4 for images
  with alpha, and `GetPageInfo` sets `PPF_ALPHA` alongside `PPF_RGB` for
  alpha images. The actual DIB output is always 24-bit BGR -- the alpha
  flag tells ACDSee the original had transparency, not that the output does.

## License

The vendored libwebp is licensed under the BSD 3-Clause License (see
`codec/libwebp/COPYING`). The plug-in wrapper code is available under the
MIT license.

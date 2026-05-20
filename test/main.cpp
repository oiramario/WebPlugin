#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

#include "WebPlugin.h"
#include "WebPDecoder.h"

bool SaveDIBToBMP(const void* dib, size_t dibSize, const char* filePath);

static LARGE_INTEGER g_freq;
static double toMs(LARGE_INTEGER a, LARGE_INTEGER b) {
    return (b.QuadPart - a.QuadPart) * 1000.0 / g_freq.QuadPart;
}

static SIZE_T processPrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(),
                         reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                         sizeof(pmc));
    return pmc.PrivateUsage;
}

static void printLine(const char* label, double ms, int count = 1) {
    if (count > 1)
        printf("  %-26s %8.3f ms  (avg %.4f ms/call)\n", label, ms, ms / count);
    else
        printf("  %-26s %8.3f ms\n", label, ms);
}

// ── mode 1: normal (free DIB after each frame) ────────────────────────────────
static void runNormal(WebPlugin& plugin, ID_StateHdl hs,
                      int nFrames, int W, int H, bool saveBmp)
{
    LARGE_INTEGER t0, t1;
    double msPageDecode = 0, msFree = 0;
    int fails = 0;

    // Retrieve the underlying decoder for sub-phase stats
    // (cast is safe: OpenImage stores WebPDecoder* as ID_StateHdl)
    WebPDecoder* decoder = static_cast<WebPDecoder*>(hs);

    for (int i = 0; i < nFrames; i++) {
        ID_DecodeParam dp;
        dp.nPage = i;
        ID_ImageOut io;

        QueryPerformanceCounter(&t0);
        int rc = plugin.PageDecode(hs, &dp, &io);
        QueryPerformanceCounter(&t1);
        msPageDecode += toMs(t0, t1);

        if (rc != IDE_OK) {
            fails++;
            continue;
        }

        if (saveBmp && i < 5) {
            const BITMAPINFOHEADER* bih = static_cast<const BITMAPINFOHEADER*>(io.hdib);
            size_t dibSize = sizeof(BITMAPINFOHEADER) + bih->biSizeImage;
            char name[64]; sprintf_s(name, "d:\\%d.bmp", i);
            SaveDIBToBMP(io.hdib, dibSize, name);
        }

        QueryPerformanceCounter(&t0);
        GlobalFree(io.hdib);
        QueryPerformanceCounter(&t1);
        msFree += toMs(t0, t1);
    }

    // ── GlobalAlloc micro-benchmark (same size, isolated) ──────────────────
    const int dstStride = ((W * 3 + 3) / 4) * 4;
    const size_t dibSz  = sizeof(BITMAPINFOHEADER) + (size_t)dstStride * H;
    double msAlloc = 0;
    for (int i = 0; i < nFrames; i++) {
        QueryPerformanceCounter(&t0);
        HGLOBAL h = GlobalAlloc(GMEM_FIXED, dibSz);
        QueryPerformanceCounter(&t1);
        msAlloc += toMs(t0, t1);
        if (h) GlobalFree(h);
    }

    // ── Sub-phase breakdown ─────────────────────────────────────────────────
    const auto& fs = decoder->getFrameStats();
    double msGetNext   = fs.usGetNext   / 1000.0;
    double msComposite = fs.usComposite / 1000.0;
    double msGetFrame  = msGetNext + msComposite;
    double msPageDecodeHeader = msPageDecode - msAlloc - msGetFrame; // BITMAPINFOHEADER setup

    printf("\n  PageDecode breakdown (%d frames):\n", nFrames);
    printf("  %-30s %8s   %s\n", "Sub-phase", "Total", "Avg/frame");
    printf("  %s\n", std::string(58, '-').c_str());
    auto row = [&](const char* label, double total) {
        printf("  %-30s %8.3f ms  %.4f ms\n", label, total, total / nFrames);
    };
    row("GlobalAlloc",              msAlloc);
    row("  WebPAnimDecoderGetNext", msGetNext);
    row("  compositeBGRAtoBGR",     msComposite);
    row("  getFrame total",         msGetFrame);
    row("  BITMAPINFOHEADER setup", msPageDecodeHeader);
    printf("  %s\n", std::string(58, '-').c_str());
    row("IDP_PageDecode total",     msPageDecode);
    row("GlobalFree",               msFree);

    if (fails) printf("  WARN: %d frame(s) PageDecode failed\n", fails);
}

// ── mode 2: ACDSee buffer-all (hold every DIB until CloseImage) ───────────────
static void runBufferAll(WebPlugin& plugin, ID_StateHdl hs,
                         int nFrames, int W, int H)
{
    const int dstStride = ((W * 3 + 3) / 4) * 4;
    const size_t dibSz  = sizeof(BITMAPINFOHEADER) + (size_t)dstStride * H;

    const SIZE_T LIMIT_32BIT = 2ULL * 1024 * 1024 * 1024;
    const SIZE_T HEADROOM    = 256ULL * 1024 * 1024;
    const SIZE_T SAFE_LIMIT  = LIMIT_32BIT - HEADROOM;

    std::vector<HGLOBAL> handles;
    handles.reserve(nFrames);

    SIZE_T basePrivate = processPrivateBytes();
    SIZE_T cumulative  = 0;
    int    firstFail   = -1;
    int    cap32bit    = -1;

    printf("\n  Frame  CumDIB(MB)  PrivateMem(MB)  Status\n");
    printf("  %s\n", std::string(52, '-').c_str());

    for (int i = 0; i < nFrames; i++) {
        cumulative += dibSz;
        if (cap32bit < 0 && cumulative > SAFE_LIMIT)
            cap32bit = i;

        ID_DecodeParam dp;
        dp.nPage = i;
        ID_ImageOut io;
        int rc = plugin.PageDecode(hs, &dp, &io);

        SIZE_T priv = (processPrivateBytes() - basePrivate) / (1024 * 1024);
        const char* status = (rc == IDE_OK) ? "OK" : "FAILED";

        if (rc != IDE_OK && firstFail < 0) firstFail = i;
        if (rc == IDE_OK) handles.push_back(io.hdib);

        if (i % 10 == 0 || i == cap32bit || rc != IDE_OK)
            printf("  %5d  %10.1f  %14zu  %s%s\n",
                   i, (double)cumulative / 1048576.0, priv, status,
                   (i == cap32bit) ? "  <- 32-bit SAFE LIMIT" : "");
    }

    printf("  %s\n", std::string(52, '-').c_str());
    printf("  Allocated:    %d DIBs  (%.1f MB)\n",
           (int)handles.size(), (double)handles.size() * dibSz / 1048576.0);
    printf("  32-bit would fail at frame: %d  (%.1f MB cumulative)\n",
           cap32bit, (double)cap32bit * dibSz / 1048576.0);
    if (firstFail >= 0)
        printf("  Actual 64-bit failure at frame: %d\n", firstFail);
    else
        printf("  Actual result (64-bit): all %d frames succeeded\n", nFrames);

    printf("  Releasing %d DIBs...\n", (int)handles.size());
    for (HGLOBAL h : handles) GlobalFree(h);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.webp> [--buffer-all] [--save-bmp]\n", argv[0]);
        return 1;
    }

    bool bufferAll = false, saveBmp = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--buffer-all") == 0) bufferAll = true;
        if (strcmp(argv[i], "--save-bmp")   == 0) saveBmp   = true;
    }

    QueryPerformanceFrequency(&g_freq);
    LARGE_INTEGER t0, t1;

    // ── File I/O ──────────────────────────────────────────────────────────────
    QueryPerformanceCounter(&t0);
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) { fprintf(stderr, "Cannot open: %s\n", argv[1]); return 1; }
    file.seekg(0, std::ios::end); size_t fileSize = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    file.close();
    QueryPerformanceCounter(&t1);
    double msIO = toMs(t0, t1);

    // ── IDP_Init equivalent: construct WebPlugin ──────────────────────────────
    WebPlugin plugin;

    // ── IDP_OpenImage ─────────────────────────────────────────────────────────
    ID_SourceInfo psi;
    psi.pszFN = argv[1];
    psi.pBuf  = data.data();
    psi.dwLen = (DWORD)fileSize;

    ID_StateHdl hs = nullptr;
    QueryPerformanceCounter(&t0);
    int rc = plugin.OpenImage(&psi, &hs);
    QueryPerformanceCounter(&t1);
    double msOpen = toMs(t0, t1);

    if (rc != IDE_OK) {
        fprintf(stderr, "OpenImage failed: %d\n", rc);
        return 1;
    }

    // ── IDP_GetImageInfo ──────────────────────────────────────────────────────
    ID_ImageInfo ii;
    QueryPerformanceCounter(&t0);
    rc = plugin.GetImageInfo(hs, &ii);
    QueryPerformanceCounter(&t1);
    double msInfo = toMs(t0, t1);

    if (rc != IDE_OK) {
        fprintf(stderr, "GetImageInfo failed: %d\n", rc);
        plugin.CloseImage(hs);
        return 1;
    }

    const int nFrames = ii.nPages;
    const int W = ii.si.cx, H = ii.si.cy;
    const bool anim  = (ii.dwFlags & IIF_ANIM) != 0;
    const bool alpha = (ii.nSPP == 4);
    const int  dstStride = ((W * 3 + 3) / 4) * 4;
    const size_t dibSz   = sizeof(BITMAPINFOHEADER) + (size_t)dstStride * H;

    printf("\nFile  : %s\n", argv[1]);
    printf("Size  : %.1f MB\n", fileSize / 1048576.0);
    printf("Image : %dx%d  %s\n", W, H, alpha ? "BGRA" : "BGR");
    printf("Type  : %s  %d frame(s)\n", anim ? "Animated" : "Static", nFrames);
    printf("DIB   : %.1f KB/frame  total if buffered = %.1f MB\n",
           dibSz / 1024.0, (double)dibSz * nFrames / 1048576.0);

    // ── IDP_GetPageInfo x N ───────────────────────────────────────────────────
    QueryPerformanceCounter(&t0);
    int totalDur = 0;
    for (int i = 0; i < nFrames; i++) {
        ID_PageInfo pi;
        plugin.GetPageInfo(hs, i, &pi);
        totalDur += pi.nDelay;
    }
    QueryPerformanceCounter(&t1);
    double msPageInfo = toMs(t0, t1);

    // ── PageDecode phases ─────────────────────────────────────────────────────
    if (bufferAll) {
        printf("\n── ACDSee buffer-all simulation ──────────────────────────────\n");
        runBufferAll(plugin, hs, nFrames, W, H);
    } else {
        printf("\nACDSee call sequence timings:\n");
        printf("  %-26s %8s\n", "Phase", "Time");
        printf("  %s\n", std::string(48, '-').c_str());
        printLine("File I/O",           msIO);
        printLine("IDP_OpenImage",      msOpen);
        printLine("IDP_GetImageInfo",   msInfo);
        printLine("IDP_GetPageInfo xN", msPageInfo, nFrames);
        runNormal(plugin, hs, nFrames, W, H, saveBmp);
    }

    // ── IDP_CloseImage ────────────────────────────────────────────────────────
    QueryPerformanceCounter(&t0);
    plugin.CloseImage(hs);
    QueryPerformanceCounter(&t1);
    printLine("IDP_CloseImage", toMs(t0, t1));

    printf("\nAnimation duration: %d ms\n", totalDur);
    return 0;
}

bool SaveDIBToBMP(const void* dib, size_t dibSize, const char* filePath) {
    if (!dib || dibSize < sizeof(BITMAPINFOHEADER) || !filePath) return false;
    BITMAPFILEHEADER bmfh = {};
    bmfh.bfType    = 0x4D42;
    bmfh.bfSize    = sizeof(BITMAPFILEHEADER) + (DWORD)dibSize;
    bmfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    std::ofstream out(filePath, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(&bmfh), sizeof(BITMAPFILEHEADER));
    out.write(reinterpret_cast<const char*>(dib),   dibSize);
    return true;
}

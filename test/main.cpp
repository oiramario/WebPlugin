#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <vector>
#include <filesystem>
#include "WebPDecoder.h"
#include <windows.h>
#include "avir.h"
#include "avir_float4_sse.h"
#include "avir_float8_avx.h"
#include "lancir.h"

bool SaveDIBToBMP(const std::vector<uint8_t>& dibData, const char* filePath);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input.webp>" << std::endl;
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << argv[1] << std::endl;
        return 1;
    }

    file.seekg(0, std::ios::end);
    size_t size = (size_t)file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    file.close();

    WebPDecoder decoder;
    if (!decoder.decode(data)) {
        std::cerr << "Decoding failed" << std::endl;
        return 1;
    }

    std::cout << "Successfully decoded WebP" << std::endl;
    std::cout << "Dimensions: " << decoder.getWidth() << "x" << decoder.getHeight() << std::endl;
    std::cout << "Format: " << (decoder.hasAlpha() ? "BGRA" : "BGR") << std::endl;
    std::cout << "Type: " << (decoder.hasAnimated()
        ? "Animated (" + std::to_string(decoder.getFrameCount()) + " frames)"
        : "Static") << std::endl;

    if (decoder.hasAnimated()) {
        int total_duration = 0;
        int nFrames = decoder.getFrameCount();
        int stride = ((decoder.getWidth() * 3 + 3) / 4) * 4;
        size_t imageSize = stride * decoder.getHeight();
        size_t dibSize = sizeof(BITMAPINFOHEADER) + imageSize;

        for (int i = 0; i < nFrames; ++i) {
            char name[256] = {0};
            sprintf_s(name, 256, "d:\\%d.bmp", i);

            const Frame& frame = decoder.getFrame(i);
            std::vector<uint8_t> dib(dibSize);
            BITMAPINFOHEADER bih = {};
            bih.biSize        = sizeof(BITMAPINFOHEADER);
            bih.biWidth       = decoder.getWidth();
            bih.biHeight      = decoder.getHeight();
            bih.biPlanes      = 1;
            bih.biBitCount    = 24;
            bih.biCompression = BI_RGB;
            bih.biSizeImage   = (DWORD)imageSize;
            memcpy(dib.data(), &bih, sizeof(BITMAPINFOHEADER));
            memcpy(dib.data() + sizeof(BITMAPINFOHEADER), frame.data(), imageSize);
            SaveDIBToBMP(dib, name);

            std::cout << "  Frame " << i << ": " << decoder.getFrameDelay(i) << "ms" << std::endl;
            total_duration += decoder.getFrameDelay(i);
        }
        std::cout << "Total animation duration: " << total_duration << "ms" << std::endl;
    } else {
        int stride = ((decoder.getWidth() * 3 + 3) / 4) * 4;
        size_t imageSize = stride * decoder.getHeight();
        size_t dibSize = sizeof(BITMAPINFOHEADER) + imageSize;

        const Frame& frame = decoder.getFrame(0);
        std::vector<uint8_t> dib(dibSize);
        BITMAPINFOHEADER bih = {};
        bih.biSize        = sizeof(BITMAPINFOHEADER);
        bih.biWidth       = decoder.getWidth();
        bih.biHeight      = decoder.getHeight();
        bih.biPlanes      = 1;
        bih.biBitCount    = 24;
        bih.biCompression = BI_RGB;
        bih.biSizeImage   = (DWORD)imageSize;
        memcpy(dib.data(), &bih, sizeof(BITMAPINFOHEADER));
        memcpy(dib.data() + sizeof(BITMAPINFOHEADER), frame.data(), imageSize);
        SaveDIBToBMP(dib, "d:\\1.bmp");
    }

    // quality comparison: save each algorithm's output per ratio
    {
        const int srcW = decoder.getWidth();
        const int srcH = decoder.getHeight();
        const int srcStride = ((srcW * 3 + 3) / 4) * 4;
        const Frame& frame = decoder.getFrame(0);

        avir::CImageResizer<>                         scalar;
        avir::CImageResizer<avir::fpclass_float4>     sse2;
        avir::CImageResizer<avir::fpclass_float8_dil> avx2;
        avir::CLancIR                                 lancir;

        const double ratios[] = { 0.9, 0.7, 0.5, 0.3, 0.1 };

        // helper: pack avir tmp -> strided DIB pixels
        auto avirToDIB = [](const std::vector<uint8_t>& tmp,
                            uint8_t* pix, int dstW, int dstH, int dstStride) {
            const uint8_t* src = tmp.data();
            uint8_t* dst = pix;
            for (int y = 0; y < dstH; ++y, src += dstW * 3, dst += dstStride)
                memcpy(dst, src, dstW * 3);
        };

        // helper: build and save a DIB
        auto saveDIB = [](const char* path, const uint8_t* pix,
                          int w, int h, int stride) {
            const size_t imageSize = static_cast<size_t>(stride) * h;
            std::vector<uint8_t> dib(sizeof(BITMAPINFOHEADER) + imageSize);
            BITMAPINFOHEADER bih = {};
            bih.biSize        = sizeof(BITMAPINFOHEADER);
            bih.biWidth       = w;
            bih.biHeight      = h;
            bih.biPlanes      = 1;
            bih.biBitCount    = 24;
            bih.biCompression = BI_RGB;
            bih.biSizeImage   = static_cast<DWORD>(imageSize);
            memcpy(dib.data(), &bih, sizeof(BITMAPINFOHEADER));
            memcpy(dib.data() + sizeof(BITMAPINFOHEADER), pix, imageSize);
            SaveDIBToBMP(dib, path);
        };

        std::cout << "\nQuality comparison + timing (" << srcW << "x" << srcH << "):\n";
        std::cout << "ratio\t\tscalar\t\tsse2\t\tavx2\t\tlancir_la3\tlancir_la2\n";

        for (double ratio : ratios) {
            const int pct     = static_cast<int>(ratio * 100);
            const int dstW    = (std::max)(1, static_cast<int>(srcW * ratio));
            const int dstH    = (std::max)(1, static_cast<int>(srcH * ratio));
            const int dstStride = ((dstW * 3 + 3) / 4) * 4;
            const size_t pixBytes = static_cast<size_t>(dstStride) * dstH;

            char dir[32];
            sprintf_s(dir, "d:\\%d", pct);
            std::filesystem::create_directories(dir);

            char path[64];
            std::vector<uint8_t> tmp(static_cast<size_t>(dstW) * 3 * dstH);
            std::vector<uint8_t> pix(pixBytes);

            auto time = [](auto fn) {
                auto t0 = std::chrono::high_resolution_clock::now();
                fn();
                return std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - t0).count();
            };

            double ms_scalar = time([&] {
                scalar.resizeImage(frame.data(), srcW, srcH, srcStride,
                                   tmp.data(), dstW, dstH, 3, 0.0);
                avirToDIB(tmp, pix.data(), dstW, dstH, dstStride);
            });
            sprintf_s(path, "%s\\scalar.bmp", dir);
            saveDIB(path, pix.data(), dstW, dstH, dstStride);

            double ms_sse2 = time([&] {
                sse2.resizeImage(frame.data(), srcW, srcH, srcStride,
                                 tmp.data(), dstW, dstH, 3, 0.0);
                avirToDIB(tmp, pix.data(), dstW, dstH, dstStride);
            });
            sprintf_s(path, "%s\\sse2.bmp", dir);
            saveDIB(path, pix.data(), dstW, dstH, dstStride);

            double ms_avx2 = time([&] {
                avx2.resizeImage(frame.data(), srcW, srcH, srcStride,
                                 tmp.data(), dstW, dstH, 3, 0.0);
                avirToDIB(tmp, pix.data(), dstW, dstH, dstStride);
            });
            sprintf_s(path, "%s\\avx2.bmp", dir);
            saveDIB(path, pix.data(), dstW, dstH, dstStride);

            avir::CLancIRParams lp3(srcStride, dstStride);
            double ms_la3 = time([&] {
                lancir.resizeImage(frame.data(), srcW, srcH,
                                   pix.data(), dstW, dstH, 3, &lp3);
            });
            sprintf_s(path, "%s\\lancir_la3.bmp", dir);
            saveDIB(path, pix.data(), dstW, dstH, dstStride);

            avir::CLancIRParams lp2(srcStride, dstStride);
            lp2.la = 2.0;
            double ms_la2 = time([&] {
                lancir.resizeImage(frame.data(), srcW, srcH,
                                   pix.data(), dstW, dstH, 3, &lp2);
            });
            sprintf_s(path, "%s\\lancir_la2.bmp", dir);
            saveDIB(path, pix.data(), dstW, dstH, dstStride);

            std::cout << pct << "%\t-> " << dstW << "x" << dstH << "\t"
                      << ms_scalar << "ms\t\t" << ms_sse2 << "ms\t\t"
                      << ms_avx2   << "ms\t\t" << ms_la3  << "ms\t\t"
                      << ms_la2    << "ms\n";
        }
    }

    return 0;
}


// dibData 的内容已是 BITMAPINFOHEADER + 像素数据（DIB 格式），直接追加文件头写入即可。
bool SaveDIBToBMP(const std::vector<uint8_t>& dibData, const char* filePath) {
    if (dibData.size() < sizeof(BITMAPINFOHEADER) || !filePath)
        return false;

    BITMAPFILEHEADER bmfh = {};
    bmfh.bfType = 0x4D42;
    bmfh.bfSize = sizeof(BITMAPFILEHEADER) + (DWORD)dibData.size();
    bmfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    std::ofstream out(filePath, std::ios::binary);
    if (!out) return false;

    out.write(reinterpret_cast<const char*>(&bmfh), sizeof(BITMAPFILEHEADER));
    out.write(reinterpret_cast<const char*>(dibData.data()), dibData.size());
    return true;
}

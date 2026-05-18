#include <iostream>
#include <fstream>
#include <string>
#include "WebPDecoder.h"
#include <windows.h>

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
            bih.biSizeImage   = imageSize;
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
        bih.biSizeImage   = imageSize;
        memcpy(dib.data(), &bih, sizeof(BITMAPINFOHEADER));
        memcpy(dib.data() + sizeof(BITMAPINFOHEADER), frame.data(), imageSize);
        SaveDIBToBMP(dib, "d:\\1.bmp");
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

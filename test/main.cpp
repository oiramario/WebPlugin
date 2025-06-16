#include <iostream>
#include <fstream>
#include "WebPDecoder.h"
#include <windows.h>

bool SaveDIBToBMP(HGLOBAL hDIB, const char* filePath);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <input.webp>" << std::endl;
        return 1;
    }
    
    // 读取WebP文件
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
    
    // 创建解码器实例并解码
    WebPDecoder decoder;
    if (!decoder.decode(data.data(), size)) {
        std::cerr << "Decoding failed: " << decoder.getError() << std::endl;
        return 1;
    }
    
    // 输出解码信息
    std::cout << "Successfully decoded WebP" << std::endl;
    std::cout << "Dimensions: " << decoder.getWidth() << "x" << decoder.getHeight() << std::endl;
    std::cout << "Format: " << (decoder.hasAlpha() ? "RGBA" : "RGB") << std::endl;
    std::cout << "Type: " << (decoder.hasAnimated() ? "Animated (" + std::to_string(decoder.getFrames().size()) + " frames)" : "Static") << std::endl;
    
    // 输出帧信息
    if (decoder.hasAnimated()) {
        int total_duration = 0;
        const auto& frames = decoder.getFrames();
        const auto& durations = decoder.getDurations();
        
        for (size_t i = 0; i < frames.size(); ++i) {
            HGLOBAL hDIB = frames[i];
            char name[256] = {0};
            sprintf_s(name, 256, "d:\\%d.bmp", i);
            SaveDIBToBMP(hDIB, name);

            total_duration += durations[i];
        }
        std::cout << "Total animation duration: " << total_duration << "ms" << std::endl;
    } else {
        const auto& frame = decoder.getFrames()[0];
        const auto& duration = decoder.getDurations()[0];
        HGLOBAL hDIB = frame;
        SaveDIBToBMP(hDIB, "d:\\1.bmp");
    }
    
    return 0;
}


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

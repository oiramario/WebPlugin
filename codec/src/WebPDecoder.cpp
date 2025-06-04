#include "WebPDecoder.h"
#include <webp/decode.h>
#include <webp/demux.h>
#include <iostream>
#include <windows.h>

WebPDecoder::WebPDecoder()
    : has_animation_(false), has_alpha_(false), width_(0), height_(0) {
}

WebPDecoder::~WebPDecoder() {
    for (auto& hDIB : frames_) {
        GlobalFree(hDIB);
    }
}

bool WebPDecoder::decode(const uint8_t* data, size_t size) {
    // 清空之前的结果
    for (auto& hDIB : frames_) {
        GlobalFree(hDIB);
    }
    frames_.clear();
    durations_.clear();

    // 获取图像特征
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(data, size, &features) != VP8_STATUS_OK) {
        setError("Failed to get WebP features");
        return false;
    }

    width_ = features.width;
    height_ = features.height;
    has_alpha_ = (features.has_alpha != 0);
    has_animation_ = features.has_animation;

    int bytesPerPixel = has_alpha_ ? 4 : 3;
    int stride = ((width_ * bytesPerPixel + 3) / 4) * 4;  // 4字节对齐
    int imageSize = stride * height_;

    // 创建DIB辅助函数
    auto createDIB = [&](const uint8_t* pixelData) -> HGLOBAL {
        // 创建DIB
        BITMAPINFOHEADER bih = {0};
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = width_;
        bih.biHeight = height_;
        bih.biPlanes = 1;
        bih.biBitCount = bytesPerPixel * 8;
        bih.biCompression = BI_RGB;

        // 分配内存
        HGLOBAL hDIB = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
        if (!hDIB)
            return nullptr;
        
        BITMAPINFOHEADER* pBIH = (BITMAPINFOHEADER*)GlobalLock(hDIB);
        if (!pBIH) {
            GlobalFree(hDIB);
            return nullptr;
        }
        
        // 复制位图信息头
        memcpy(pBIH, &bih, sizeof(BITMAPINFOHEADER));

        // 复制像素数据（垂直翻转，因为Windows DIB是从下到上存储的）
        uint8_t* pPixels = (uint8_t*)pBIH + sizeof(BITMAPINFOHEADER);
        for (int y = 0; y < height_; y++) {
            memcpy(pPixels + (height_ - 1 - y) * stride, 
                pixelData + y * width_ * bytesPerPixel, 
                width_ * bytesPerPixel);
        }

        GlobalUnlock(hDIB);

        return hDIB;
    };

    if (has_animation_) {
        // 动画解码
        WebPAnimDecoderOptions options;
        WebPAnimDecoderOptionsInit(&options);
        options.color_mode = has_alpha_ ? MODE_BGRA : MODE_BGR;
        
        WebPData webp_data = {data, size};
        WebPAnimDecoder* dec = WebPAnimDecoderNew(&webp_data, &options);
        if (!dec) {
            setError("Failed to create WebP animation decoder");
            return false;
        }

        // 获取动画信息
        WebPAnimInfo anim_info;
        if (!WebPAnimDecoderGetInfo(dec, &anim_info)) {
            WebPAnimDecoderDelete(dec);
            setError("Failed to get WebP animation info");
            return false;
        }

        // 解码每一帧
        while (WebPAnimDecoderHasMoreFrames(dec)) {
            uint8_t* rgba;
            int timestamp;
            
            if (!WebPAnimDecoderGetNext(dec, &rgba, &timestamp)) {
                break;
            }
            
            // 创建DIB
            HGLOBAL hDIB = createDIB(rgba);
            if (hDIB) {
                frames_.push_back(hDIB);
                durations_.push_back(timestamp);
            }
        }

        // 清理资源
        WebPAnimDecoderDelete(dec);

        // 检查是否成功解码至少一帧
        if (frames_.empty()) {
            setError("Failed to decode any animation frames");
            return false;
        }
    }
    else {
        // 静态图像解码
        uint8_t* bgr = WebPDecodeBGR(data, size, 0, 0);
        if (!bgr) {
            setError("Failed to decode WebP image");
            return false;
        }

        // 创建DIB
        HGLOBAL hDIB = createDIB(bgr);
        if (hDIB) {
            frames_.push_back(hDIB);
            durations_.push_back(0);
        } else {
            setError("Failed to create DIB");
            WebPFree(bgr);
            return false;
        }

        // 释放解码结果
        WebPFree(bgr);
    }

    return true;
}

void WebPDecoder::setError(const std::string& error) {
    error_msg_ = error;
    std::cerr << "WebPDecoder Error: " << error << std::endl;
}

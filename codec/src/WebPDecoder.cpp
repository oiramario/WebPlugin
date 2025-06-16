#include "WebPDecoder.h"
#include <webp/decode.h>
#include <webp/demux.h>
#include <iostream>
#include <windows.h>

WebPDecoder::WebPDecoder()
    : has_animation_(false), has_alpha_(false), width_(0), height_(0) {
}

WebPDecoder::~WebPDecoder() {
    frames_.clear();
}

bool WebPDecoder::decode(const uint8_t* data, size_t size) {
    // 清空之前的结果
    frames_.clear();

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

    int bytesPerPixel = has_animation_ ? 4 : (has_alpha_ ? 4 : 3);
    int stride = ((width_ * bytesPerPixel + 3) / 4) * 4;  // 4字节对齐
    int imageSize = stride * height_;

    // 创建DIB辅助函数
    auto createDIB = [&](const uint8_t* pixelData) -> std::vector<uint8_t> {
        // 创建DIB
        BITMAPINFOHEADER bih = {0};
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = width_;
        bih.biHeight = height_;
        bih.biPlanes = 1;
        bih.biBitCount = bytesPerPixel * 8;
        bih.biCompression = BI_RGB;

        // 分配内存
        std::vector<uint8_t> data(sizeof(BITMAPINFOHEADER) + imageSize);
        
        // 复制位图信息头
        memcpy(&data[0], &bih, sizeof(BITMAPINFOHEADER));

        // 复制像素数据（垂直翻转，因为Windows DIB是从下到上存储的）
        uint8_t* pPixels = (uint8_t*)(&data[0]) + sizeof(BITMAPINFOHEADER);
        for (int y = 0; y < height_; y++) {
            memcpy(pPixels + (height_ - 1 - y) * stride, 
                pixelData + y * width_ * bytesPerPixel, 
                width_ * bytesPerPixel);
        }

        return data;
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
        uint8_t* pixels = nullptr;
        int timestamp = 0;
        while (WebPAnimDecoderGetNext(dec, &pixels, &timestamp)) {
            // 创建DIB
            std::vector<uint8_t> data = createDIB(pixels);
            Frame frame = { data, timestamp };
            frames_.push_back(frame);
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
        uint8_t* pixels = WebPDecodeBGR(data, size, 0, 0);
        if (!pixels) {
            setError("Failed to decode WebP image");
            return false;
        }

        // 创建DIB
        std::vector<uint8_t> data = createDIB(pixels);
        Frame frame = { data, 0 };
        frames_.push_back(frame);

        // 释放解码结果
        WebPFree(pixels);
    }

    return true;
}

void WebPDecoder::setError(const std::string& error) {
    error_msg_ = error;
    std::cerr << "WebPDecoder Error: " << error << std::endl;
}

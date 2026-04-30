#include "WebPDecoder.h"
#include <webp/decode.h>
#include <webp/demux.h>
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

    // 动画始终使用 BGRA（需要合成画布），静态图按是否有 alpha 决定
    if (has_animation_) has_alpha_ = true;
    int bytesPerPixel = has_alpha_ ? 4 : 3;
    int stride = ((width_ * bytesPerPixel + 3) / 4) * 4;  // 4字节对齐
    int imageSize = stride * height_;

    // 创建 Windows DIB（底部在前，4字节行对齐）
    auto createDIB = [&](const uint8_t* pixelData) -> std::vector<uint8_t> {
        BITMAPINFOHEADER bih = {0};
        bih.biSize      = sizeof(BITMAPINFOHEADER);
        bih.biWidth     = width_;
        bih.biHeight    = height_;
        bih.biPlanes    = 1;
        bih.biBitCount  = bytesPerPixel * 8;
        bih.biCompression = BI_RGB;

        std::vector<uint8_t> dib(sizeof(BITMAPINFOHEADER) + imageSize);
        memcpy(dib.data(), &bih, sizeof(BITMAPINFOHEADER));

        // DIB 像素从底行到顶行存储，需垂直翻转
        uint8_t* pPixels = dib.data() + sizeof(BITMAPINFOHEADER);
        for (int y = 0; y < height_; y++) {
            memcpy(pPixels + (height_ - 1 - y) * stride,
                   pixelData + y * width_ * bytesPerPixel,
                   width_ * bytesPerPixel);
        }

        return dib;
    };

    if (has_animation_) {
        // 动画解码：始终使用 BGRA 以正确处理画布合成与半透明混合
        WebPAnimDecoderOptions options;
        WebPAnimDecoderOptionsInit(&options);
        options.color_mode = MODE_BGRA;

        WebPData webp_data = {data, size};
        WebPAnimDecoder* dec = WebPAnimDecoderNew(&webp_data, &options);
        if (!dec) {
            setError("Failed to create WebP animation decoder");
            return false;
        }

        WebPAnimInfo anim_info;
        if (!WebPAnimDecoderGetInfo(dec, &anim_info)) {
            WebPAnimDecoderDelete(dec);
            setError("Failed to get WebP animation info");
            return false;
        }
        // WebPAnimDecoderGetNext 返回的 timestamp 是从动画起始累积的毫秒数，
        // 需转换为每帧独立持续时间。
        uint8_t* pixels = nullptr;
        int timestamp = 0;
        int prev_timestamp = 0;
        while (WebPAnimDecoderGetNext(dec, &pixels, &timestamp)) {
            std::vector<uint8_t> frameData = createDIB(pixels);
            int duration = timestamp - prev_timestamp;
            frames_.push_back({ frameData, duration });
            prev_timestamp = timestamp;
        }
        WebPAnimDecoderDelete(dec);

        if (frames_.empty()) {
            setError("Failed to decode any animation frames");
            return false;
        }
    }
    else {
        // 静态图像解码
        uint8_t* pixels = has_alpha_
            ? WebPDecodeBGRA(data, size, nullptr, nullptr)
            : WebPDecodeBGR(data, size, nullptr, nullptr);
        if (!pixels) {
            setError("Failed to decode WebP image");
            return false;
        }

        frames_.push_back({ createDIB(pixels), 0 });
        WebPFree(pixels);
    }

    return true;
}

void WebPDecoder::setError(const std::string& error) {
    error_msg_ = error;
}

#include "WebPDecoder.h"
#include <webp/decode.h>
#include <webp/demux.h>
#include <windows.h>

WebPDecoder::WebPDecoder() {
}

WebPDecoder::~WebPDecoder() {
    cleanupDecoder();
}

void WebPDecoder::cleanupDecoder() {
    if (anim_decoder_) {
        WebPAnimDecoderDelete(anim_decoder_);
        anim_decoder_ = nullptr;
    }
}


bool WebPDecoder::decode(const uint8_t* data, size_t size) {
    frames_.clear();
    raw_data_.clear();
    cleanupDecoder();

    WebPBitstreamFeatures features;
    if (WebPGetFeatures(data, size, &features) != VP8_STATUS_OK) {
        OutputDebugStringA("WebPDecoder: Failed to get WebP features\n");
        return false;
    }

    width_ = features.width;
    height_ = features.height;
    has_alpha_ = (features.has_alpha != 0);
    has_animation_ = features.has_animation;

    if (has_animation_) {
        // 动画 —— 懒解码：只解析元信息，帧像素按需解码
        has_alpha_ = true;
        raw_data_.assign(data, data + size);

        WebPAnimDecoderOptions options;
        WebPAnimDecoderOptionsInit(&options);
        options.color_mode = MODE_BGRA;

        WebPData webp_data = {raw_data_.data(), raw_data_.size()};
        anim_decoder_ = WebPAnimDecoderNew(&webp_data, &options);
        if (!anim_decoder_) {
            OutputDebugStringA("WebPDecoder: Failed to create WebP animation decoder\n");
            return false;
        }

        WebPAnimInfo anim_info;
        if (!WebPAnimDecoderGetInfo(anim_decoder_, &anim_info)) {
            cleanupDecoder();
            OutputDebugStringA("WebPDecoder: Failed to get WebP animation info\n");
            return false;
        }

        total_frames_ = anim_info.frame_count;
        frames_.reserve(total_frames_);
        decoded_frames_ = 0;
        prev_timestamp_ = 0;
        all_decoded_ = false;

        if (total_frames_ == 0) {
            cleanupDecoder();
            OutputDebugStringA("WebPDecoder: No frames in animation\n");
            return false;
        }

        // 提取帧延迟表（无需解码像素）
        frame_delays_.reserve(total_frames_);
        {
            WebPData d = {raw_data_.data(), raw_data_.size()};
            WebPDemuxer* demux = WebPDemux(&d);
            if (demux) {
                for (int i = 1; i <= total_frames_; i++) {
                    WebPIterator iter;
                    if (WebPDemuxGetFrame(demux, i, &iter)) {
                        frame_delays_.push_back(iter.duration);
                        WebPDemuxReleaseIterator(&iter);
                    } else {
                        frame_delays_.push_back(0);
                    }
                }
                WebPDemuxDelete(demux);
            } else {
                // demux 失败时全部 fallback 为 0
                frame_delays_.assign(total_frames_, 0);
            }
        }

        return true;
    }
    else {
        // 静态 —— 直接解码
        total_frames_ = 1;
        all_decoded_ = true;

        int bytesPerPixel = has_alpha_ ? 4 : 3;
        int imageSize = width_ * height_ * bytesPerPixel;

        uint8_t* pixels = has_alpha_
            ? WebPDecodeBGRA(data, size, nullptr, nullptr)
            : WebPDecodeBGR(data, size, nullptr, nullptr);
        if (!pixels) {
            OutputDebugStringA("WebPDecoder: Failed to decode WebP image\n");
            return false;
        }

        std::vector<uint8_t> rawPixels(pixels, pixels + imageSize);
        WebPFree(pixels);

        // 垂直翻转：源 top-down → 存储 bottom-up（补齐 stride 对齐）
        {
            int rowBytes = width_ * bytesPerPixel;
            int stride   = ((rowBytes + 3) / 4) * 4;
            std::vector<uint8_t> flipped(stride * height_);
            uint8_t* dst = flipped.data() + (height_ - 1) * stride;
            const uint8_t* src = rawPixels.data();
            for (int y = 0; y < height_; y++) {
                memcpy(dst, src, rowBytes);
                dst -= stride;
                src += rowBytes;
            }
            rawPixels.swap(flipped);
        }

        frames_.push_back({ std::move(rawPixels), 0 });
        return true;
    }
}

std::vector<uint8_t> WebPDecoder::bgraToBGR(const uint8_t* bgra) const {
    const int rowBytes = width_ * 3;
    const int stride   = ((rowBytes + 3) / 4) * 4;
    std::vector<uint8_t> bgr(stride * height_);

    // 垂直翻转 + 补齐 stride 对齐：源 top-down BGRA → 输出 bottom-up BGR
    uint8_t* dst = bgr.data() + (height_ - 1) * stride;
    const uint8_t* srcRow = bgra;
    for (int y = 0; y < height_; y++) {
        uint8_t*       dp = dst;
        const uint8_t* sp = srcRow;
        const uint8_t* spEnd = sp + width_ * 4;

        // 每次处理 2 像素（8 bytes BGRA → 6 bytes BGR）
        for (; sp + 8 <= spEnd; sp += 8, dp += 6) {
            uint64_t px;
            memcpy(&px, sp, 8);
            *(uint32_t*)(dp)     = (uint32_t)(px & 0xFFFFFF) | ((uint32_t)((px >> 32) & 0xFF) << 24);
            *(uint16_t*)(dp + 4) = (uint16_t)((px >> 40) & 0xFFFF);
        }

        // 剩余 1 像素
        for (; sp < spEnd; sp += 4, dp += 3) {
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
        }

        dst    -= stride;
        srcRow += width_ * 4;
    }

    return bgr;
}

// 解码下一帧（动画），返回 false 表示已无更多帧
bool WebPDecoder::decodeNextFrame() {
    if (!anim_decoder_ || all_decoded_) {
        return false;
    }

    uint8_t* pixels = nullptr;
    int timestamp = 0;
    if (!WebPAnimDecoderGetNext(anim_decoder_, &pixels, &timestamp)) {
        all_decoded_ = true;
        cleanupDecoder();
        return false;
    }

    // WebPAnimDecoderGetNext timestamp 是累积毫秒数，转成每帧独立持续时间
    int duration = timestamp - prev_timestamp_;
    prev_timestamp_ = timestamp;

    frames_.push_back({ bgraToBGR(pixels), duration });
    decoded_frames_++;

    if (decoded_frames_ >= total_frames_) {
        all_decoded_ = true;
        cleanupDecoder();
    }

    return true;
}

const std::vector<Frame>& WebPDecoder::getFrames() {
    // 向后兼容：确保所有帧都已解码
    if (has_animation_ && !all_decoded_) {
        while (decodeNextFrame()) {}
    }
    return frames_;
}

const Frame& WebPDecoder::getFrame(int index) {
    // 按需解码到目标帧
    if (has_animation_) {
        while (index >= static_cast<int>(frames_.size()) && decodeNextFrame()) {}
    }
    if (index >= static_cast<int>(frames_.size())) {
        return frames_.back();  // return last valid frame to avoid UB
    }
    return frames_[index];
}

int WebPDecoder::getFrameDelay(int index) const {
    if (has_animation_ && index >= 0 && index < static_cast<int>(frame_delays_.size())) {
        return frame_delays_[index];
    }
    return 0;
}


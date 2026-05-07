#include "WebPDecoder.h"
#include <webp/decode.h>
#include <webp/demux.h>
#include <windows.h>

WebPDecoder::~WebPDecoder() {
    if (anim_decoder_) {
        WebPAnimDecoderDelete(anim_decoder_);
        anim_decoder_ = nullptr;
    }
}

bool WebPDecoder::decode(const uint8_t* data, size_t size) {
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(data, size, &features) != VP8_STATUS_OK) {
        OutputDebugStringA("WebPDecoder: Failed to get WebP features\n");
        return false;
    }

    width_ = features.width;
    height_ = features.height;
    has_alpha_ = (features.has_alpha != 0);
    has_animation_ = features.has_animation;

    // Pre-allocate frame buffer (knowing dimensions and pixel format)
    {
        int bpp = 3;
        int rowBytes = width_ * bpp;
        stride_ = ((rowBytes + 3) / 4) * 4;
        current_frame_.resize(stride_ * height_);
    }

    if (has_animation_) {
        return decodeAnimation(data, size);
    } else {
        return decodeStatic(data, size);
    }
}

bool WebPDecoder::decodeStatic(const uint8_t* data, size_t size) {
    total_frames_ = 1;

    uint8_t* pixels = WebPDecodeBGR(data, size, nullptr, nullptr);
    if (!pixels) {
        OutputDebugStringA("WebPDecoder: Failed to decode WebP image\n");
        return false;
    }

    // Flip top-down source -> bottom-up
    int rowBytes = width_ * 3;
    uint8_t* dst = current_frame_.data() + (height_ - 1) * stride_;
    const uint8_t* src = pixels;
    for (int y = 0; y < height_; y++) {
        memcpy(dst, src, rowBytes);
        dst -= stride_;
        src += rowBytes;
    }
    WebPFree(pixels);

    return true;
}

bool WebPDecoder::decodeAnimation(const uint8_t* data, size_t size) {
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
        OutputDebugStringA("WebPDecoder: Failed to get WebP animation info\n");
        return false;
    }

    total_frames_ = anim_info.frame_count;
    if (total_frames_ == 0) {
        OutputDebugStringA("WebPDecoder: No frames in animation\n");
        return false;
    }

    // Pre-extract frame delay table (no pixel decoding needed)
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
            frame_delays_.assign(total_frames_, 0);
        }
    }

    return true;
}

const Frame& WebPDecoder::getFrame(int index) {
    if (!has_animation_) {
        return current_frame_;
    }

    // Sequential decode forward to target frame
    while (current_frame_index_ < index) {
        uint8_t* pixels = nullptr;
        int timestamp = 0;
        if (!WebPAnimDecoderGetNext(anim_decoder_, &pixels, &timestamp)) {
            break;
        }
        bgraToBGR(pixels);
        current_frame_index_++;
    }

    return current_frame_;
}

int WebPDecoder::getFrameDelay(int index) const {
    if (has_animation_ && total_frames_ > 0) {
        return frame_delays_[index];
    }
    return 0;
}

void WebPDecoder::bgraToBGR(const uint8_t* bgra) {
    const int rowBytes = width_ * 3;
    const int stride   = ((rowBytes + 3) / 4) * 4;

    // Vertical flip: top-down BGRA -> bottom-up BGR
    uint8_t* out = current_frame_.data() + (height_ - 1) * stride;
    const uint8_t* srcRow = bgra;
    for (int y = 0; y < height_; y++) {
        uint8_t*       dp = out;
        const uint8_t* sp = srcRow;
        const uint8_t* spEnd = sp + width_ * 4;

        for (; sp + 8 <= spEnd; sp += 8, dp += 6) {
            uint64_t px;
            memcpy(&px, sp, 8);
            *(uint32_t*)(dp)     = (uint32_t)(px & 0xFFFFFF) | ((uint32_t)((px >> 32) & 0xFF) << 24);
            *(uint16_t*)(dp + 4) = (uint16_t)((px >> 40) & 0xFFFF);
        }

        for (; sp < spEnd; sp += 4, dp += 3) {
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
        }

        out    -= stride;
        srcRow += width_ * 4;
    }
}

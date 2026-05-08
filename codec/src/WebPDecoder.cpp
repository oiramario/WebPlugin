#include "WebPDecoder.h"
#include <webp/decode.h>
#include <webp/demux.h>
#include <windows.h>

// Checkerboard pattern for transparent background
static constexpr int kCheckerCell = 8;
static constexpr uint8_t kCheckerLight = 224;
static constexpr uint8_t kCheckerDark = 192;

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

    // Pre-allocate frame buffer — ACDSee only supports 24-bit BGR DIB, so 3 BPP
    {
        int bpp = 3;
        frame_row_bytes_ = width_ * bpp;
        frame_stride_ = ((frame_row_bytes_ + 3) / 4) * 4;
        current_frame_.resize(frame_stride_ * height_);
    }

    if (has_animation_) {
        return decodeAnimation(data, size);
    } else {
        return decodeStatic(data, size);
    }
}

bool WebPDecoder::decodeStatic(const uint8_t* data, size_t size) {
    total_frames_ = 1;
    uint8_t* dst = current_frame_.data() + (height_ - 1) * frame_stride_;

    if (!has_alpha_) {
        uint8_t* pixels = WebPDecodeBGR(data, size, nullptr, nullptr);
        if (!pixels) return false;
        for (int y = 0; y < height_; y++) {
            memcpy(dst, pixels + y * frame_row_bytes_, frame_row_bytes_);
            dst -= frame_stride_;
        }
        WebPFree(pixels);
    } else {
        uint8_t* pixels = WebPDecodeBGRA(data, size, nullptr, nullptr);
        if (!pixels) return false;

        for (int y = 0; y < height_; y++) {
            const uint8_t* sp = pixels + y * width_ * 4;
            uint8_t* dp = dst;
            int cellY = y / kCheckerCell;
            for (int x = 0; x < width_; x++) {
                uint8_t a = sp[3];
                if (a == 255) {
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
                } else {
                    uint8_t bg = ((x / kCheckerCell + cellY) & 1) ? kCheckerDark : kCheckerLight;
                    if (a == 0) {
                        dp[0] = bg; dp[1] = bg; dp[2] = bg;
                    } else {
                        uint8_t inv = 255 - a;
                        dp[0] = (uint8_t)((sp[0] * a + bg * inv) / 255);
                        dp[1] = (uint8_t)((sp[1] * a + bg * inv) / 255);
                        dp[2] = (uint8_t)((sp[2] * a + bg * inv) / 255);
                    }
                }
                sp += 4;
                dp += 3;
            }
            dst -= frame_stride_;
        }
        WebPFree(pixels);
    }
    return true;
}

bool WebPDecoder::decodeAnimation(const uint8_t* data, size_t size) {
    raw_data_.assign(data, data + size);

    WebPAnimDecoderOptions options;
    WebPAnimDecoderOptionsInit(&options);
    // WebPAnimDecoder requires an alpha-capable mode (BGRA/RGBA/bgrA/rgbA),
    // non-alpha modes like MODE_BGR are rejected in ApplyDecoderOptions.
    // Alpha is also needed for inter-frame blending during compositing.
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

    if (current_frame_index_ >= total_frames_) {
        return current_frame_;
    }

    uint8_t* pixels = nullptr;
    int timestamp = 0;
    if (!WebPAnimDecoderGetNext(anim_decoder_, &pixels, &timestamp)) {
        return current_frame_;
    }

    // BGRA -> BGR with checkerboard blend for transparency, flip bottom-up
    {
        uint8_t* out = current_frame_.data() + (height_ - 1) * frame_stride_;
        for (int y = 0; y < height_; y++) {
            const uint8_t* sp = pixels + y * width_ * 4;
            uint8_t* dp = out;
            int cellY = y / kCheckerCell;
            for (int x = 0; x < width_; x++) {
                uint8_t a = sp[3];
                if (a == 255) {
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
                } else {
                    uint8_t bg = ((x / kCheckerCell + cellY) & 1) ? kCheckerDark : kCheckerLight;
                    if (a == 0) {
                        dp[0] = bg; dp[1] = bg; dp[2] = bg;
                    } else {
                        uint8_t inv = 255 - a;
                        dp[0] = (uint8_t)((sp[0] * a + bg * inv) / 255);
                        dp[1] = (uint8_t)((sp[1] * a + bg * inv) / 255);
                        dp[2] = (uint8_t)((sp[2] * a + bg * inv) / 255);
                    }
                }
                sp += 4;
                dp += 3;
            }
            out -= frame_stride_;
        }
    }

    current_frame_index_++;
    return current_frame_;
}

int WebPDecoder::getFrameDelay(int index) const {
    if (has_animation_ && total_frames_ > 0) {
        return frame_delays_[index];
    }
    return 0;
}

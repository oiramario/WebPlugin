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

bool WebPDecoder::decode(std::span<const uint8_t> bytes) {
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK) {
        OutputDebugStringA("WebPDecoder::decode: WebPGetFeatures FAILED");
        return false;
    }

    width_ = features.width;
    height_ = features.height;
    has_alpha_ = (features.has_alpha != 0);
    has_animation_ = features.has_animation;

    src_bytes_ = bytes;

    frame_stride_ = ((width_ * 3 + 3) / 4) * 4;
    current_frame_.resize(static_cast<size_t>(frame_stride_) * height_);

    if (has_animation_) {
        WebPAnimDecoderOptions options;
        WebPAnimDecoderOptionsInit(&options);
        options.color_mode = MODE_BGRA;

        WebPData webp_data = {src_bytes_.data(), src_bytes_.size()};
        anim_decoder_ = WebPAnimDecoderNew(&webp_data, &options);
        if (!anim_decoder_) {
            OutputDebugStringA("WebPDecoder::decode: WebPAnimDecoderNew FAILED");
            return false;
        }

        WebPAnimInfo anim_info;
        if (!WebPAnimDecoderGetInfo(anim_decoder_, &anim_info)) {
            OutputDebugStringA("WebPDecoder::decode: WebPAnimDecoderGetInfo FAILED");
            return false;
        }

        total_frames_ = anim_info.frame_count;
        if (total_frames_ == 0) {
            OutputDebugStringA("WebPDecoder::decode: frame_count == 0");
            return false;
        }
    } else {
        total_frames_ = 1;
    }

    return true;
}

const Frame& WebPDecoder::getFrame(int /*index*/) {
    if (!has_animation_) {
        if (!has_alpha_) {
            uint8_t* pixels = WebPDecodeBGR(src_bytes_.data(), src_bytes_.size(), nullptr, nullptr);
            if (pixels) {
                uint8_t* dst = current_frame_.data() + (height_ - 1) * frame_stride_;
                for (int y = 0; y < height_; y++) {
                    memcpy(dst, pixels + y * width_ * 3, width_ * 3);
                    dst -= frame_stride_;
                }
                WebPFree(pixels);
            }
        } else {
            uint8_t* pixels = WebPDecodeBGRA(src_bytes_.data(), src_bytes_.size(), nullptr, nullptr);
            if (pixels) {
                compositeBGRAtoBGR(pixels);
                WebPFree(pixels);
            }
        }
    } else {
        uint8_t* pixels = nullptr;
        int timestamp = 0;
        if (WebPAnimDecoderGetNext(anim_decoder_, &pixels, &timestamp)) {
            compositeBGRAtoBGR(pixels);
        } else {
            OutputDebugStringA("WebPDecoder::getFrame: WebPAnimDecoderGetNext FAILED");
        }
    }
    return current_frame_;
}

int WebPDecoder::getFrameDelay(int index) {
    if (!has_animation_ || index < 0 || index >= total_frames_) {
        return 0;
    }

    if (!delays_loaded_) {
        constexpr int kDefaultFrameDelayMs = 100;
        frame_delays_.reserve(total_frames_);
        WebPData d = {src_bytes_.data(), src_bytes_.size()};
        WebPDemuxer* demux = WebPDemux(&d);
        if (demux) {
            for (int i = 1; i <= total_frames_; i++) {
                WebPIterator iter;
                if (WebPDemuxGetFrame(demux, i, &iter)) {
                    frame_delays_.push_back(iter.duration > 0 ? iter.duration : kDefaultFrameDelayMs);
                    WebPDemuxReleaseIterator(&iter);
                } else {
                    frame_delays_.push_back(kDefaultFrameDelayMs);
                }
            }
            WebPDemuxDelete(demux);
        } else {
            frame_delays_.assign(total_frames_, kDefaultFrameDelayMs);
        }
        delays_loaded_ = true;
    }

    return frame_delays_[index];
}

void WebPDecoder::compositeBGRAtoBGR(const uint8_t* src_bgra) {
    uint8_t* out = current_frame_.data() + (height_ - 1) * frame_stride_;
    for (int y = 0; y < height_; y++) {
        const uint8_t* sp = src_bgra + y * width_ * 4;
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
                    uint32_t inv = 255u - a;
                    uint32_t b0 = sp[0] * a + bg * inv;
                    uint32_t b1 = sp[1] * a + bg * inv;
                    uint32_t b2 = sp[2] * a + bg * inv;
                    dp[0] = (uint8_t)((b0 + 1 + (b0 >> 8)) >> 8);
                    dp[1] = (uint8_t)((b1 + 1 + (b1 >> 8)) >> 8);
                    dp[2] = (uint8_t)((b2 + 1 + (b2 >> 8)) >> 8);
                }
            }
            sp += 4;
            dp += 3;
        }
        out -= frame_stride_;
    }
}


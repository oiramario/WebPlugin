#include "WebPDecoder.h"
#include <webp/decode.h>
#include <webp/demux.h>
#include <windows.h>

// Checkerboard pattern for transparent background
static constexpr int kCheckerCell = 8;
static constexpr uint8_t kCheckerLight = 224;
static constexpr uint8_t kCheckerDark = 192;
static constexpr int kDefaultFrameDelayMs = 100;

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

    if (has_animation_) {
        WebPData webp_data = {src_bytes_.data(), src_bytes_.size()};
        WebPDemuxer* demux = WebPDemux(&webp_data);
        if (!demux) {
            OutputDebugStringA("WebPDecoder::decode: WebPDemux FAILED");
            return false;
        }

        total_frames_ = (int)WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT);

        if (total_frames_ == 0) {
            WebPDemuxDelete(demux);
            OutputDebugStringA("WebPDecoder::decode: frame_count == 0");
            return false;
        }

        frame_delays_.resize(total_frames_);
        for (int i = 0; i < total_frames_; i++) {
            WebPIterator iter;
            if (WebPDemuxGetFrame(demux, i + 1, &iter)) {
                frame_delays_[i] = iter.duration > 0 ? iter.duration : kDefaultFrameDelayMs;
                WebPDemuxReleaseIterator(&iter);
            } else {
                frame_delays_[i] = kDefaultFrameDelayMs;
            }
        }

        WebPDemuxDelete(demux);
    } else {
        total_frames_ = 1;
    }

    return true;
}

bool WebPDecoder::getFrame(int /*index*/, uint8_t* dst, int stride) {
    if (!has_animation_) {
        if (!has_alpha_) {
            uint8_t* pixels = WebPDecodeBGR(src_bytes_.data(), src_bytes_.size(), nullptr, nullptr);
            if (!pixels) {
                OutputDebugStringA("WebPDecoder::getFrame: WebPDecodeBGR FAILED");
                return false;
            }
            uint8_t* d = dst + (height_ - 1) * stride;
            for (int y = 0; y < height_; y++) {
                memcpy(d, pixels + y * width_ * 3, width_ * 3);
                d -= stride;
            }
            WebPFree(pixels);
        } else {
            uint8_t* pixels = WebPDecodeBGRA(src_bytes_.data(), src_bytes_.size(), nullptr, nullptr);
            if (!pixels) {
                OutputDebugStringA("WebPDecoder::getFrame: WebPDecodeBGRA FAILED");
                return false;
            }
            compositeBGRAtoBGR(pixels, dst, stride);
            WebPFree(pixels);
        }
    } else {
        if (!ensureAnimDecoder())
            return false;

        uint8_t* pixels = nullptr;
        int timestamp = 0;
        if (!WebPAnimDecoderGetNext(anim_decoder_, &pixels, &timestamp)) {
            OutputDebugStringA("WebPDecoder::getFrame: WebPAnimDecoderGetNext FAILED");
            return false;
        }
        compositeBGRAtoBGR(pixels, dst, stride);
    }
    return true;
}

bool WebPDecoder::ensureAnimDecoder() {
    if (anim_decoder_) return true;

    WebPAnimDecoderOptions options;
    WebPAnimDecoderOptionsInit(&options);
    options.color_mode = MODE_BGRA;
    options.use_threads = 1;

    WebPData webp_data = {src_bytes_.data(), src_bytes_.size()};
    anim_decoder_ = WebPAnimDecoderNew(&webp_data, &options);
    if (!anim_decoder_) {
        OutputDebugStringA("WebPDecoder::ensureAnimDecoder: WebPAnimDecoderNew FAILED");
        return false;
    }

    return true;
}

int WebPDecoder::getFrameDelay(int index) {
    if (!has_animation_ || index < 0 || index >= total_frames_)
        return 0;
    return frame_delays_[index];
}

void WebPDecoder::compositeBGRAtoBGR(const uint8_t* src_bgra, uint8_t* dst_buffer, int stride) {
    uint8_t* out = dst_buffer + (height_ - 1) * stride;
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
        out -= stride;
    }
}


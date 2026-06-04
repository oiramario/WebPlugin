// -- WebPDecoder implementation --------------------------------------------
//
// Delegates to libwebp for actual decoding. Two paths:
//   - Static:  WebPDecodeBGR / WebPDecodeBGRA + compositeBGRAtoBGR
//   - Animated: WebPAnimDecoder (sequential iteration)
//
// Output is always 24-bit bottom-up BGR (DIB) - ACDSee's native format.
// Alpha is blended against a checkerboard pattern at decode time.
//

#include "WebPDecoder.h"
#include <webp/decode.h>
#include <webp/demux.h>
#include <windows.h>

// -- Checkerboard constants for transparent background ---------------------
// Simulates a standard transparency grid (8 px cells, light/dark gray).
static constexpr int    kCheckerCell      = 8;
static constexpr uint8_t kCheckerLight    = 224;
static constexpr uint8_t kCheckerDark     = 192;
static constexpr int    kDefaultFrameDelayMs = 100;

// ===========================================================================
// Lifecycle
// ===========================================================================

WebPDecoder::~WebPDecoder() {
    WebPAnimDecoderDelete(anim_decoder_);
}

// ===========================================================================
// decode() - probe the bitstream and collect metadata
// ===========================================================================

bool WebPDecoder::decode(std::span<const uint8_t> bytes) {
    // Probe WebP bitstream features
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(bytes.data(), (size_t)bytes.size(), &features) != VP8_STATUS_OK) {
        DebugWebPTrace("WebPDecoder::decode: WebPGetFeatures FAILED");
        return false;
    }

    width_         = features.width;
    height_        = features.height;
    has_alpha_     = (features.has_alpha != 0);
    has_animation_ = features.has_animation;

    src_bytes_ = bytes;

    if (has_animation_) {
        // -- Animated path: pre-scan frame delays with WebPDemux ------------
        WebPData webp_data = { src_bytes_.data(), src_bytes_.size() };
        WebPDemuxer* demux = WebPDemux(&webp_data);
        if (!demux) {
            DebugWebPTrace("WebPDecoder::decode: WebPDemux FAILED");
            return false;
        }

        total_frames_ = (int)WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT);
        if (total_frames_ == 0) {
            WebPDemuxDelete(demux);
            DebugWebPTrace("WebPDecoder::decode: frame_count == 0");
            return false;
        }

        // Read per-frame durations (separate from pixel decoding)
        frame_delays_.resize(total_frames_);
        for (int i = 0; i < total_frames_; i++) {
            WebPIterator iter;
            if (WebPDemuxGetFrame(demux, i + 1, &iter)) {
                frame_delays_[i] = iter.duration > 0 ? iter.duration : kDefaultFrameDelayMs;
                WebPDemuxReleaseIterator(&iter);
            } else {
                DebugWebPTrace("WebPDecoder::decode: WebPDemuxGetFrame FAILED for frame %d, using default delay", i);
                frame_delays_[i] = kDefaultFrameDelayMs;
            }
        }

        WebPDemuxDelete(demux);
    } else {
        // -- Static path ----------------------------------------------------
        total_frames_ = 1;
    }

    return true;
}

// ===========================================================================
// resolveOutputSize - decide whether to use libwebp's built-in scaling
// ===========================================================================
//
// Scaling at decode time is cheaper than decoding full-size + GC, but only
// when the target area is significantly smaller than the original. The
// breakeven is ~74% linear scale (0.74^2 ~ 0.55 of original area).
//

std::pair<int,int> WebPDecoder::resolveOutputSize(int reqW, int reqH) const {
    if (reqW <= 0 || reqH <= 0)
        return { width_, height_ };

    if ((int64_t)reqW * reqH >= (int64_t)width_ * height_ * 55 / 100)
        return { width_, height_ };

    return { reqW, reqH };
}

// ===========================================================================
// getFrame - decode a single frame into a bottom-up BGR buffer
// ===========================================================================

bool WebPDecoder::getFrame(int index, uint8_t* dst, int stride, int outW, int outH) {
    if (index < 0 || index >= total_frames_) {
        DebugWebPTrace("WebPDecoder::getFrame: index=%d out of range [0,%d)", index, total_frames_);
        return false;
    }

    const bool scaling = (outW != width_ || outH != height_);
    DebugWebPTrace("WebPDecoder::getFrame[%d] %s %s %s -> %dx%d",
                   index,
                   has_animation_ ? "anim"   : "static",
                   has_alpha_     ? "alpha"  : "noalpha",
                   scaling        ? "scale"  : "orig",
                   outW, outH);

    if (!has_animation_) {
        // -- Static frame ----------------------------------------------------
        WebPDecoderConfig config;
        WebPInitDecoderConfig(&config);
        config.options.use_threads = 1;

        if (scaling) {
            config.options.use_scaling   = 1;
            config.options.scaled_width  = outW;
            config.options.scaled_height = outH;
        }

        VP8StatusCode st = VP8_STATUS_OK;
        if (!has_alpha_) {
            // No alpha: decode directly into DIB (bottom-up stride)
            config.output.colorspace         = MODE_BGR;
            config.output.u.RGBA.rgba        = dst + (size_t)(outH - 1) * stride;
            config.output.u.RGBA.stride      = -stride;
            config.output.u.RGBA.size        = (size_t)stride * outH;
            config.output.is_external_memory = 1;

            st = WebPDecode(src_bytes_.data(), src_bytes_.size(), &config);
        } else {
            // Has alpha: decode to BGRA, then composite onto checkerboard
            config.output.colorspace = MODE_BGRA;

            st = WebPDecode(src_bytes_.data(), src_bytes_.size(), &config);
            if (st == VP8_STATUS_OK)
                compositeBGRAtoBGR(config.output.u.RGBA.rgba, dst, stride, outW, outH);
        }

        WebPFreeDecBuffer(&config.output);
        if (st != VP8_STATUS_OK) {
            DebugWebPTrace("WebPDecoder::getFrame: WebPDecode FAILED");
            return false;
        }
    } else {
        // -- Animated frame --------------------------------------------------
        if (!ensureAnimDecoder(scaling ? outW : 0, scaling ? outH : 0))
            return false;

        // Reset to frame 0 on each sequential pass (ACDSee calls 0..n-1)
        if (index == 0)
            WebPAnimDecoderReset(anim_decoder_);

        uint8_t* pixels    = nullptr;
        int      timestamp = 0;
        if (!WebPAnimDecoderGetNext(anim_decoder_, &pixels, &timestamp)) {
            DebugWebPTrace("WebPDecoder::getFrame: WebPAnimDecoderGetNext FAILED");
            return false;
        }

        compositeBGRAtoBGR(pixels, dst, stride, outW, outH);
    }

    return true;
}

// ===========================================================================
// ensureAnimDecoder - lazy-create / reconfigure the animation decoder
// ===========================================================================
//
// Uses MODE_BGRA (libwebp rejects non-alpha modes for animation). Scale
// parameters are baked in at creation time; a mid-stream scale change
// destroys and recreates the decoder.

bool WebPDecoder::ensureAnimDecoder(int scaleW, int scaleH) {
    if (anim_decoder_) {
        if (anim_scale_w_ == scaleW && anim_scale_h_ == scaleH)
            return true;
        // Scale parameters changed - recreate
        WebPAnimDecoderDelete(anim_decoder_);
        anim_decoder_ = nullptr;
    }

    WebPAnimDecoderOptions options;
    WebPAnimDecoderOptionsInit(&options);
    options.color_mode   = MODE_BGRA;   // libwebp requires alpha mode for anim
    options.use_threads  = 1;
    options.scaled_width  = scaleW;
    options.scaled_height = scaleH;

    WebPData webp_data = { src_bytes_.data(), src_bytes_.size() };
    anim_decoder_ = WebPAnimDecoderNew(&webp_data, &options);
    if (!anim_decoder_) {
        DebugWebPTrace("WebPDecoder::ensureAnimDecoder: WebPAnimDecoderNew FAILED");
        return false;
    }

    anim_scale_w_ = scaleW;
    anim_scale_h_ = scaleH;
    return true;
}

// ===========================================================================
// getFrameDelay - delay for a given animation frame (milliseconds)
// ===========================================================================

int WebPDecoder::getFrameDelay(int index) const {
    if (!has_animation_ || index < 0 || index >= total_frames_)
        return 0;
    return frame_delays_[index];
}

// ===========================================================================
// compositeBGRAtoBGR - blend BGRA frame onto checkerboard -> bottom-up BGR
// ===========================================================================
//
// Alpha-aware compositing against an 8-pixel checkerboard (grayscale 224/192).
// The rounding formula (value + 128 + (value >> 8)) >> 8 computes
// unbiased /255:  value/256 + value/65536 + 0.5, which rounds to nearest
// rather than truncating.  Plain value/256 would darken the output by ~0.4%
// per blend, compounding across frames.
//

void WebPDecoder::compositeBGRAtoBGR(const uint8_t* src_bgra, uint8_t* dst_buffer,
                                     int stride, int w, int h)
{
    // Start at bottom row (bottom-up DIB)
    uint8_t* out = dst_buffer + (size_t)(h - 1) * stride;

    for (int y = 0; y < h; y++) {
        const uint8_t* sp = src_bgra + (size_t)y * w * 4;   // source: 4 bytes/pixel (BGRA)
        uint8_t*       dp = out;                              // dest:   3 bytes/pixel (BGR)
        int cellY = y / kCheckerCell;

        for (int x = 0; x < w; x++) {
            uint8_t a = sp[3];  // alpha

            if (a == 255) {
                // Fully opaque: write pixel directly
                dp[0] = sp[0];  // B
                dp[1] = sp[1];  // G
                dp[2] = sp[2];  // R
            } else {
                // Determine checkerboard background color
                uint8_t bg = ((x / kCheckerCell + cellY) & 1) ? kCheckerDark : kCheckerLight;

                if (a == 0) {
                    // Fully transparent: background only
                    dp[0] = bg;
                    dp[1] = bg;
                    dp[2] = bg;
                } else {
                    // Partial transparency: alpha-blend with rounding
                    uint32_t inv = 255u - a;
                    uint32_t b0 = sp[0] * a + bg * inv;   // blue
                    uint32_t b1 = sp[1] * a + bg * inv;   // green
                    uint32_t b2 = sp[2] * a + bg * inv;   // red

                    // (value + 128) / 256 with proper rounding
                    dp[0] = (uint8_t)((b0 + 128 + (b0 >> 8)) >> 8);
                    dp[1] = (uint8_t)((b1 + 128 + (b1 >> 8)) >> 8);
                    dp[2] = (uint8_t)((b2 + 128 + (b2 >> 8)) >> 8);
                }
            }

            sp += 4;
            dp += 3;
        }

        out -= stride;  // move to next row upward (bottom-up DIB)
    }
}

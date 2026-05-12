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
        return decodeAnimation(bytes);
    } else {
        return decodeStatic(bytes);
    }
}

bool WebPDecoder::decodeStatic(std::span<const uint8_t> bytes) {
    total_frames_ = 1;
    // Defer pixel decode to first getFrame() call.
    // The caller (ACDSee) guarantees psi->pBuf remains valid until CloseImage,
    // so storing a non-owning reference is safe.
    src_bytes_ = bytes;
    static_decoded_ = false;
    return true;
}

bool WebPDecoder::decodeAnimation(std::span<const uint8_t> bytes) {
    // Borrow the caller's bytes — lifetime contract documented on decode().
    src_bytes_ = bytes;

    WebPAnimDecoderOptions options;
    WebPAnimDecoderOptionsInit(&options);
    // WebPAnimDecoder requires an alpha-capable mode (BGRA/RGBA/bgrA/rgbA),
    // non-alpha modes like MODE_BGR are rejected in ApplyDecoderOptions.
    // Alpha is also needed for inter-frame blending during compositing.
    options.color_mode = MODE_BGRA;

    WebPData webp_data = {src_bytes_.data(), src_bytes_.size()};
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

    return true;
}

const Frame& WebPDecoder::getFrame(int index) {
    if (!has_animation_) {
        // Lazy decode on first access.
        if (!static_decoded_) {
            uint8_t* dst = current_frame_.data() + (height_ - 1) * frame_stride_;
            if (!has_alpha_) {
                uint8_t* pixels = WebPDecodeBGR(src_bytes_.data(), src_bytes_.size(), nullptr, nullptr);
                if (pixels) {
                    for (int y = 0; y < height_; y++) {
                        memcpy(dst, pixels + y * frame_row_bytes_, frame_row_bytes_);
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
            static_decoded_ = true;
        }
        return current_frame_;
    }

    // Rewind on loop replay (e.g., PageDecode(0) after the last frame).
    if (index < current_frame_index_) {
        WebPAnimDecoderReset(anim_decoder_);
        current_frame_index_ = -1;
    } else if (index == current_frame_index_) {
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

    compositeBGRAtoBGR(pixels);

    current_frame_index_++;
    return current_frame_;
}

int WebPDecoder::getFrameDelay(int index) {
    if (!has_animation_ || index < 0 || index >= total_frames_) {
        return 0;
    }

    // Lazily extract frame delay table on first access.
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
                    // (n + 1 + (n >> 8)) >> 8  ==  n / 255  for n in [0, 65280]; max here is 255*255.
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

size_t WebPDecoder::getDIBSize() const {
    return sizeof(BITMAPINFOHEADER) + static_cast<size_t>(frame_stride_) * height_;
}

void WebPDecoder::writeDIB(int page, void* out) {
    const Frame& frame = getFrame(page);
    const int imageSize = frame_stride_ * height_;

    BITMAPINFOHEADER bih = {};
    bih.biSize        = sizeof(BITMAPINFOHEADER);
    bih.biWidth       = width_;
    bih.biHeight      = height_;
    bih.biPlanes      = 1;
    bih.biBitCount    = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage   = imageSize;

    uint8_t* dst = static_cast<uint8_t*>(out);
    memcpy(dst, &bih, sizeof(BITMAPINFOHEADER));
    memcpy(dst + sizeof(BITMAPINFOHEADER), frame.data(), imageSize);
}

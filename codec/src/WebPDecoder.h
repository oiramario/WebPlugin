#ifndef WEBP_DECODER_H
#define WEBP_DECODER_H

#include <cstdint>
#include <span>
#include <vector>

using Frame = std::vector<uint8_t>;

struct WebPAnimDecoder;

class WebPDecoder {
public:
    WebPDecoder() = default;
    ~WebPDecoder();

    // Decodes the WebP data at `bytes`.
    // For animated WebP, the backing memory must remain valid for the lifetime
    // of this WebPDecoder - libwebp's animation decoder holds pointers into it.
    // For static WebP, the data is copied internally and `bytes` need not outlive the call.
    bool decode(std::span<const uint8_t> bytes);

    bool hasAnimated() const { return has_animation_; }
    bool hasAlpha() const { return has_alpha_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getBitsPerSample() const { return 8; }
    int getFrameStride() const { return frame_stride_; }
    int getFrameCount() const { return has_animation_ ? total_frames_ : 1; }
    int getFrameDelay(int index);
    const Frame& getFrame(int index);

private:
    // Image info
    int width_ = 0;
    int height_ = 0;
    bool has_alpha_ = false;
    bool has_animation_ = false;
    int total_frames_ = 0;

    // Frame buffer (ACDSee only supports 24-bit BGR DIB)
    int frame_stride_ = 0;
    Frame current_frame_;

    // Non-owning view of caller's bytes (for static lazy decode and animation).
    std::span<const uint8_t> src_bytes_;
    WebPAnimDecoder* anim_decoder_ = nullptr;
    std::vector<int> frame_delays_;
    bool delays_loaded_ = false;

    // BGRA top-down → BGR bottom-up into current_frame_,
    // translucent pixels composited against a checkerboard background.
    void compositeBGRAtoBGR(const uint8_t* src_bgra);
};

#endif // WEBP_DECODER_H

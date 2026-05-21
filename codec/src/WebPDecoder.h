#ifndef WEBP_DECODER_H
#define WEBP_DECODER_H

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#define WEBP_TRACE 0

struct WebPAnimDecoder;

class WebPDecoder {
public:
    // -- Lifecycle --
    WebPDecoder() = default;
    ~WebPDecoder();
    bool decode(std::span<const uint8_t> bytes);

    // -- Image info --
    bool hasAnimated() const { return has_animation_; }
    bool hasAlpha() const { return has_alpha_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getBitsPerSample() const { return 8; }
    int getFrameCount() const { return has_animation_ ? total_frames_ : 1; }
    int getFrameDelay(int index);

    // -- Decode --
    std::pair<int,int> resolveOutputSize(int reqW, int reqH) const;
    bool getFrame(int index, uint8_t* dst, int stride, int outW, int outH);

private:
    // Image properties (set by decode())
    int width_ = 0;
    int height_ = 0;
    bool has_alpha_ = false;
    bool has_animation_ = false;
    int total_frames_ = 0;

    // Source data (must outlive decoder for animated WebP)
    std::span<const uint8_t> src_bytes_;

    // Animation state
    WebPAnimDecoder* anim_decoder_ = nullptr;
    std::vector<int> frame_delays_;
    int anim_scale_w_ = 0;
    int anim_scale_h_ = 0;

    // Internal helpers
    bool ensureAnimDecoder(int scaleW, int scaleH);
    void compositeBGRAtoBGR(const uint8_t* src_bgra, uint8_t* dst, int out_stride, int w, int h);
};

#endif // WEBP_DECODER_H

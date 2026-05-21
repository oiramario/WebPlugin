#ifndef WEBP_DECODER_H
#define WEBP_DECODER_H

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

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
    int getFrameCount() const { return has_animation_ ? total_frames_ : 1; }
    int getFrameDelay(int index);

    // Returns the effective output size for a decode request.
    // use_scaling yields net benefit only when target area < 49% of original
    // (~70% linear scale). Animation and invalid requests fall back to the
    // original size. Pass the result to the getFrame overloads below.
    std::pair<int,int> resolveOutputSize(int reqW, int reqH) const;

    // Full-resolution decode — outputs at the original width × height.
    bool getFrameOrig(int index, uint8_t* dst, int stride);
    // Baseline: internal alloc + manual flip (no external-memory trick). For benchmarking only.
    bool getFrameOrigSlow(int index, uint8_t* dst, int stride);
    // Scaled decode — outputs at outW × outH. Only valid for static images;
    // call resolveOutputSize first to confirm scaling is beneficial.
    bool getFrameScale(int index, uint8_t* dst, int stride, int outW, int outH);

private:
    // Image info
    int width_ = 0;
    int height_ = 0;
    bool has_alpha_ = false;
    bool has_animation_ = false;
    int total_frames_ = 0;

    // Non-owning view of caller's bytes (for static lazy decode and animation).
    std::span<const uint8_t> src_bytes_;
    WebPAnimDecoder* anim_decoder_ = nullptr;
    std::vector<int> frame_delays_;

    bool ensureAnimDecoder();

    // BGRA top-down → BGR bottom-up into dst,
    // translucent pixels composited against a checkerboard background.
    void compositeBGRAtoBGR(const uint8_t* src_bgra, uint8_t* dst, int out_stride, int w, int h);
};

#endif // WEBP_DECODER_H

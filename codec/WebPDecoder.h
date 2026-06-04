// -- WebPDecoder - decode static & animated WebP into 24-bit BGR DIB --
//
// Thin C++ wrapper around libwebp. Output is always 24-bit bottom-up
// BGR (DIB format), with alpha blended against a checkerboard pattern.
// Animated frames are decoded sequentially via WebPAnimDecoderGetNext.
//

#ifndef WEBP_DECODER_H
#define WEBP_DECODER_H

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Debug trace macro
// ---------------------------------------------------------------------------
#ifdef DEBUG_WEBP_TRACE
#  define DebugWebPTrace(fmt, ...) \
       do { char _t[512]; sprintf_s(_t, (fmt), ##__VA_ARGS__); OutputDebugStringA(_t); } while(0)
#else
#  define DebugWebPTrace(fmt, ...) ((void)0)
#endif

struct WebPAnimDecoder;

class WebPDecoder {
public:
    // -- Lifecycle -----------------------------------------------------------
    WebPDecoder() = default;
    ~WebPDecoder();
    bool decode(std::span<const uint8_t> bytes);

    // -- Image info ----------------------------------------------------------
    bool hasAnimated() const { return has_animation_; }
    bool hasAlpha()     const { return has_alpha_; }
    int  getWidth()     const { return width_; }
    int  getHeight()    const { return height_; }
    int  getBitsPerSample() const { return 8; }
    int  getFrameCount()    const { return has_animation_ ? total_frames_ : 1; }
    int  getFrameDelay(int index) const;

    // -- Frame decode --------------------------------------------------------
    // Resolve output size: if ACDSee requests a scale that saves significant
    // memory (>=45% reduction), scale at decode time; otherwise decode full.
    std::pair<int,int> resolveOutputSize(int reqW, int reqH) const;
    bool getFrame(int index, uint8_t* dst, int stride, int outW, int outH);

private:
    // Image properties (set by decode())
    int   width_          = 0;
    int   height_         = 0;
    bool  has_alpha_      = false;
    bool  has_animation_  = false;
    int   total_frames_   = 0;

    // Non-owning view of the source data passed to decode().
    // ACDSee guarantees pBuf stays valid between OpenImage and CloseImage.
    // Static WebP re-reads this on every getFrame(); animated WebP copies
    // it into the WebPAnimDecoder on first ensureAnimDecoder() call.
    std::span<const uint8_t> src_bytes_;

    // Animation state
    WebPAnimDecoder* anim_decoder_   = nullptr;
    std::vector<int> frame_delays_;
    int              anim_scale_w_   = 0;
    int              anim_scale_h_   = 0;

    // Internal helpers
    bool ensureAnimDecoder(int scaleW, int scaleH);
    void compositeBGRAtoBGR(const uint8_t* src_bgra, uint8_t* dst,
                            int out_stride, int w, int h);
};

#endif // WEBP_DECODER_H

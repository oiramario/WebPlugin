#ifndef WEBP_DECODER_H
#define WEBP_DECODER_H

#include <cstdint>
#include <vector>

using Frame = std::vector<uint8_t>;

struct WebPAnimDecoder;

class WebPDecoder {
public:
    WebPDecoder() = default;
    ~WebPDecoder();

    bool decode(const uint8_t* data, size_t size);

    bool hasAnimated() const { return has_animation_; }
    bool hasAlpha() const { return has_alpha_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getBitsPerSample() const { return 8; }
    int getFrameCount() const { return has_animation_ ? total_frames_ : 1; }
    int getFrameDelay(int index) const;
    const Frame& getFrame(int index);

private:
    // Image info
    int width_ = 0;
    int height_ = 0;
    bool has_alpha_ = false;
    bool has_animation_ = false;
    int total_frames_ = 0;

    // Frame buffer (ACDSee only supports 24-bit BGR DIB)
    int frame_row_bytes_ = 0;
    int frame_stride_ = 0;
    Frame current_frame_;

    // Animation state
    std::vector<uint8_t> raw_data_;
    WebPAnimDecoder* anim_decoder_ = nullptr;
    int current_frame_index_ = -1;
    std::vector<int> frame_delays_;

    bool decodeStatic(const uint8_t* data, size_t size);
    bool decodeAnimation(const uint8_t* data, size_t size);
};

#endif // WEBP_DECODER_H

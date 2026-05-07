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
    bool has_animation_ = false;
    bool has_alpha_ = false;
    int width_ = 0;
    int height_ = 0;
    int total_frames_ = 0;

    // 静态图 / 动画当前帧共用
    Frame current_frame_;

    // 帧延迟表（decode 时离线提取，无需解码像素）
    std::vector<int> frame_delays_;

    // 动画原始数据，供解码器按需读取
    std::vector<uint8_t> raw_data_;
    WebPAnimDecoder* anim_decoder_ = nullptr;
    int stride_ = 0;
    int current_frame_index_ = -1;

    bool decodeStatic(const uint8_t* data, size_t size);
    bool decodeAnimation(const uint8_t* data, size_t size);
    void bgraToBGR(const uint8_t* bgra);
};

#endif // WEBP_DECODER_H

#ifndef WEBP_DECODER_H
#define WEBP_DECODER_H

#include <cstdint>
#include <vector>

struct Frame {
    std::vector<uint8_t>    data;
    int                     delay;
};

struct WebPAnimDecoder;

class WebPDecoder {
public:
    WebPDecoder();
    ~WebPDecoder();

    bool decode(const uint8_t* data, size_t size);

    bool hasAnimated() const { return has_animation_; }
    bool hasAlpha() const { return has_alpha_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getBitsPerSample() const { return 8; }

    // 快速获取总帧数（无需解码像素）
    int getFrameCount() const { return has_animation_ ? total_frames_ : 1; }

    // 获取帧延迟（无需解码像素），仅动画有效
    int getFrameDelay(int index) const;

    // 按需解码指定帧（动画），静态图直接返回
    const Frame& getFrame(int index);

    // 获取所有帧 — 对动画会触发剩余帧的懒解码
    const std::vector<Frame>& getFrames();

private:
    bool has_animation_ = false;
    bool has_alpha_ = false;
    int width_ = 0;
    int height_ = 0;

    std::vector<Frame> frames_;

    // 动画懒解码状态
    std::vector<uint8_t> raw_data_;
    WebPAnimDecoder* anim_decoder_ = nullptr;
    int total_frames_ = 0;
    int decoded_frames_ = 0;
    int prev_timestamp_ = 0;
    bool all_decoded_ = false;
    std::vector<int> frame_delays_;  // 延迟表，decode 时即填完，无需解码像素

    bool decodeNextFrame();
    void cleanupDecoder();
    std::vector<uint8_t> bgraToBGR(const uint8_t* bgra) const;
};

#endif // WEBP_DECODER_H

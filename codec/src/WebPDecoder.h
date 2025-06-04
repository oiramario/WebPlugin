#ifndef WEBP_DECODER_H
#define WEBP_DECODER_H

#include <vector>
#include <string>

class WebPDecoder {
public:
    // 构造函数：接收WebP数据和大小
    WebPDecoder();
    // 析构函数
    ~WebPDecoder();
    
    // 解码WebP图像
    bool decode(const uint8_t* data, size_t size);
    
    // 获取解码结果
    bool hasAnimated() const { return has_animation_; }
    bool hasAlpha() const { return has_alpha_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    const std::vector<void*>& getFrames() const { return frames_; }
    const std::vector<int>& getDurations() const { return durations_; }
    
    // 获取错误信息
    std::string getError() const { return error_msg_; }

private:
    bool has_animation_;        // 是否为动画
    bool has_alpha_;            // 是否有Alpha通道
    int width_;                 // 图像宽度
    int height_;                // 图像高度
    std::vector<void*> frames_;  // 帧数据
    std::vector<int> durations_; // 帧持续时间
    std::string error_msg_;     // 错误信息
    
    // 设置错误信息
    void setError(const std::string& error);
};

#endif // WEBP_DECODER_H

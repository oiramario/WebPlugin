#ifndef WEBP_DECODER_H
#define WEBP_DECODER_H

#include <cstdint>
#include <vector>
#include <string>

struct Frame {
    std::vector<uint8_t>    data;
    int                     delay;
};

class WebPDecoder {
public:
    WebPDecoder();
    ~WebPDecoder();

    bool decode(const uint8_t* data, size_t size);

    bool hasAnimated() const { return has_animation_; }
    bool hasAlpha() const { return has_alpha_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    const std::vector<Frame>& getFrames() const { return frames_; }

    std::string getError() const { return error_msg_; }

private:
    bool has_animation_;
    bool has_alpha_;
    int width_;
    int height_;
    std::vector<Frame> frames_;
    std::string error_msg_;

    void setError(const std::string& error);
};

#endif // WEBP_DECODER_H

// Copyright 2015 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the COPYING file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS. All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
// -----------------------------------------------------------------------------
//
//  AnimDecoder implementation.
//

#ifdef HAVE_CONFIG_H
#include "src/webp/config.h"
#endif

#include <assert.h>
#include <string.h>

#include "src/utils/utils.h"
#include "src/webp/decode.h"
#include "src/webp/demux.h"
#include "src/webp/mux.h"
#include "src/webp/mux_types.h"
#include "src/webp/types.h"

#define NUM_CHANNELS 4

// Scale a full-res dimension down to the scaled canvas.
#define SCALE_DIM(val, full, scaled) \
    ((int)((unsigned)(val) * (unsigned)(scaled) / (unsigned)(full)))

// Channel extraction from a uint32_t representation of a uint8_t RGBA/BGRA
// buffer.
#ifdef WORDS_BIGENDIAN
#define CHANNEL_SHIFT(i) (24 - (i) * 8)
#else
#define CHANNEL_SHIFT(i) ((i) * 8)
#endif

typedef void (*BlendRowFunc)(uint32_t* const, const uint32_t* const, int);
static void BlendPixelRowNonPremult(uint32_t* const src,
                                    const uint32_t* const dst, int num_pixels);
static void BlendPixelRowPremult(uint32_t* const src, const uint32_t* const dst,
                                 int num_pixels);

struct WebPAnimDecoder {
  WebPDemuxer* demux;              // Demuxer created from given WebP bitstream.
  WebPDecoderConfig config;        // Decoder config.
  // Note: we use a pointer to a function blending multiple pixels at a time to
  // allow possible inlining of per-pixel blending function.
  BlendRowFunc blend_func;         // Pointer to the chose blend row function.
  WebPAnimInfo info;               // Global info about the animation.
  uint8_t* curr_frame;             // Current canvas (not disposed).
  uint8_t* prev_frame_disposed;    // Previous canvas (properly disposed).
  int prev_frame_timestamp;        // Previous frame timestamp (milliseconds).
  WebPIterator prev_iter;          // Iterator object for previous frame.
  int prev_frame_was_keyframe;     // True if previous frame was a keyframe.
  int next_frame;                  // Index of the next frame to be decoded
                                   // (starting from 1).
  int out_canvas_w;                // Scaled canvas width (0 = no scaling).
  int out_canvas_h;                // Scaled canvas height (0 = no scaling).
};

static void DefaultDecoderOptions(WebPAnimDecoderOptions* const dec_options) {
  dec_options->color_mode = MODE_RGBA;
  dec_options->use_threads = 0;
  dec_options->scaled_width = 0;
  dec_options->scaled_height = 0;
}

int WebPAnimDecoderOptionsInitInternal(WebPAnimDecoderOptions* dec_options,
                                       int abi_version) {
  if (dec_options == NULL ||
      WEBP_ABI_IS_INCOMPATIBLE(abi_version, WEBP_DEMUX_ABI_VERSION)) {
    return 0;
  }
  DefaultDecoderOptions(dec_options);
  return 1;
}

WEBP_NODISCARD static int ApplyDecoderOptions(
    const WebPAnimDecoderOptions* const dec_options,
    WebPAnimDecoder* const dec) {
  WEBP_CSP_MODE mode;
  WebPDecoderConfig* config = &dec->config;
  assert(dec_options != NULL);

  mode = dec_options->color_mode;
  if (mode != MODE_RGBA && mode != MODE_BGRA &&
      mode != MODE_rgbA && mode != MODE_bgrA) {
    return 0;
  }
  dec->blend_func = (mode == MODE_RGBA || mode == MODE_BGRA)
                        ? &BlendPixelRowNonPremult
                        : &BlendPixelRowPremult;
  if (!WebPInitDecoderConfig(config)) {
    return 0;
  }
  config->output.colorspace = mode;
  config->output.is_external_memory = 1;
  config->options.use_threads = dec_options->use_threads;
  dec->out_canvas_w = 0;
  dec->out_canvas_h = 0;
  if (dec_options->scaled_width > 0 && dec_options->scaled_height > 0) {
    dec->out_canvas_w = dec_options->scaled_width;
    dec->out_canvas_h = dec_options->scaled_height;
    config->options.use_scaling = 1;
  }
  // Note: config->output.u.RGBA is set at the time of decoding each frame.
  return 1;
}

WebPAnimDecoder* WebPAnimDecoderNewInternal(
    const WebPData* webp_data, const WebPAnimDecoderOptions* dec_options,
    int abi_version) {
  WebPAnimDecoderOptions options;
  WebPAnimDecoder* dec = NULL;
  WebPBitstreamFeatures features;
  if (webp_data == NULL ||
      WEBP_ABI_IS_INCOMPATIBLE(abi_version, WEBP_DEMUX_ABI_VERSION)) {
    return NULL;
  }

  // Validate the bitstream before doing expensive allocations. The demuxer may
  // be more tolerant than the decoder.
  if (WebPGetFeatures(webp_data->bytes, webp_data->size, &features) !=
      VP8_STATUS_OK) {
    return NULL;
  }

  // Note: calloc() so that the pointer members are initialized to NULL.
  dec = (WebPAnimDecoder*)WebPSafeCalloc(1ULL, sizeof(*dec));
  if (dec == NULL) goto Error;

  if (dec_options != NULL) {
    options = *dec_options;
  } else {
    DefaultDecoderOptions(&options);
  }
  if (!ApplyDecoderOptions(&options, dec)) goto Error;

  dec->demux = WebPDemux(webp_data);
  if (dec->demux == NULL) goto Error;

  dec->info.canvas_width = WebPDemuxGetI(dec->demux, WEBP_FF_CANVAS_WIDTH);
  dec->info.canvas_height = WebPDemuxGetI(dec->demux, WEBP_FF_CANVAS_HEIGHT);
  dec->info.loop_count = WebPDemuxGetI(dec->demux, WEBP_FF_LOOP_COUNT);
  dec->info.bgcolor = WebPDemuxGetI(dec->demux, WEBP_FF_BACKGROUND_COLOR);
  dec->info.frame_count = WebPDemuxGetI(dec->demux, WEBP_FF_FRAME_COUNT);

  {
    const int buf_w = dec->out_canvas_w > 0 ? dec->out_canvas_w
                                            : (int)dec->info.canvas_width;
    const int buf_h = dec->out_canvas_h > 0 ? dec->out_canvas_h
                                            : (int)dec->info.canvas_height;
    // Note: calloc() because we fill frame with zeroes as well.
    dec->curr_frame = (uint8_t*)WebPSafeCalloc(
        buf_w * NUM_CHANNELS, buf_h);
    if (dec->curr_frame == NULL) goto Error;
    dec->prev_frame_disposed = (uint8_t*)WebPSafeCalloc(
        buf_w * NUM_CHANNELS, buf_h);
    if (dec->prev_frame_disposed == NULL) goto Error;
  }

  WebPAnimDecoderReset(dec);
  return dec;

 Error:
  WebPAnimDecoderDelete(dec);
  return NULL;
}

int WebPAnimDecoderGetInfo(const WebPAnimDecoder* dec, WebPAnimInfo* info) {
  if (dec == NULL || info == NULL) return 0;
  *info = dec->info;
  return 1;
}

// Returns true if the frame covers the full canvas.
static int IsFullFrame(int width, int height, int canvas_width,
                       int canvas_height) {
  return (width == canvas_width && height == canvas_height);
}

// Clear the canvas to transparent.
WEBP_NODISCARD static int ZeroFillCanvas(uint8_t* buf, uint32_t canvas_width,
                                         uint32_t canvas_height) {
  const uint64_t size =
      (uint64_t)canvas_width * canvas_height * NUM_CHANNELS * sizeof(*buf);
  if (!CheckSizeOverflow(size)) return 0;
  memset(buf, 0, (size_t)size);
  return 1;
}

// Clear given frame rectangle to transparent.
static void ZeroFillFrameRect(uint8_t* buf, int buf_stride, int x_offset,
                              int y_offset, int width, int height) {
  int j;
  assert(width * NUM_CHANNELS <= buf_stride);
  buf += y_offset * buf_stride + x_offset * NUM_CHANNELS;
  for (j = 0; j < height; ++j) {
    memset(buf, 0, width * NUM_CHANNELS);
    buf += buf_stride;
  }
}

// Copy width * height pixels from 'src' to 'dst'.
WEBP_NODISCARD static int CopyCanvas(const uint8_t* src, uint8_t* dst,
                                     uint32_t width, uint32_t height) {
  const uint64_t size = (uint64_t)width * height * NUM_CHANNELS;
  if (!CheckSizeOverflow(size)) return 0;
  assert(src != NULL && dst != NULL);
  memcpy(dst, src, (size_t)size);
  return 1;
}

// Returns true if the current frame is a key-frame.
static int IsKeyFrame(const WebPIterator* const curr,
                      const WebPIterator* const prev,
                      int prev_frame_was_key_frame,
                      int canvas_width, int canvas_height) {
  if (curr->frame_num == 1) {
    return 1;
  } else if ((!curr->has_alpha || curr->blend_method == WEBP_MUX_NO_BLEND) &&
             IsFullFrame(curr->width, curr->height,
                         canvas_width, canvas_height)) {
    return 1;
  } else {
    return (prev->dispose_method == WEBP_MUX_DISPOSE_BACKGROUND) &&
           (IsFullFrame(prev->width, prev->height, canvas_width,
                        canvas_height) ||
            prev_frame_was_key_frame);
  }
}


// Blend a single channel of 'src' over 'dst', given their alpha channel values.
// 'src' and 'dst' are assumed to be NOT pre-multiplied by alpha.
static uint8_t BlendChannelNonPremult(uint32_t src, uint8_t src_a,
                                      uint32_t dst, uint8_t dst_a,
                                      uint32_t scale, int shift) {
  const uint8_t src_channel = (src >> shift) & 0xff;
  const uint8_t dst_channel = (dst >> shift) & 0xff;
  const uint32_t blend_unscaled = src_channel * src_a + dst_channel * dst_a;
  assert(blend_unscaled < (1ULL << 32) / scale);
  return (blend_unscaled * scale) >> CHANNEL_SHIFT(3);
}

// Blend 'src' over 'dst' assuming they are NOT pre-multiplied by alpha.
static uint32_t BlendPixelNonPremult(uint32_t src, uint32_t dst) {
  const uint8_t src_a = (src >> CHANNEL_SHIFT(3)) & 0xff;

  if (src_a == 0) {
    return dst;
  } else {
    const uint8_t dst_a = (dst >> CHANNEL_SHIFT(3)) & 0xff;
    // This is the approximate integer arithmetic for the actual formula:
    // dst_factor_a = (dst_a * (255 - src_a)) / 255.
    const uint8_t dst_factor_a = (dst_a * (256 - src_a)) >> 8;
    const uint8_t blend_a = src_a + dst_factor_a;
    const uint32_t scale = (1UL << 24) / blend_a;

    const uint8_t blend_r = BlendChannelNonPremult(
        src, src_a, dst, dst_factor_a, scale, CHANNEL_SHIFT(0));
    const uint8_t blend_g = BlendChannelNonPremult(
        src, src_a, dst, dst_factor_a, scale, CHANNEL_SHIFT(1));
    const uint8_t blend_b = BlendChannelNonPremult(
        src, src_a, dst, dst_factor_a, scale, CHANNEL_SHIFT(2));
    assert(src_a + dst_factor_a < 256);

    return ((uint32_t)blend_r << CHANNEL_SHIFT(0)) |
           ((uint32_t)blend_g << CHANNEL_SHIFT(1)) |
           ((uint32_t)blend_b << CHANNEL_SHIFT(2)) |
           ((uint32_t)blend_a << CHANNEL_SHIFT(3));
  }
}

// Blend 'num_pixels' in 'src' over 'dst' assuming they are NOT pre-multiplied
// by alpha.
static void BlendPixelRowNonPremult(uint32_t* const src,
                                    const uint32_t* const dst, int num_pixels) {
  int i;
  for (i = 0; i < num_pixels; ++i) {
    const uint8_t src_alpha = (src[i] >> CHANNEL_SHIFT(3)) & 0xff;
    if (src_alpha != 0xff) {
      src[i] = BlendPixelNonPremult(src[i], dst[i]);
    }
  }
}

// Individually multiply each channel in 'pix' by 'scale'.
static WEBP_INLINE uint32_t ChannelwiseMultiply(uint32_t pix, uint32_t scale) {
  uint32_t mask = 0x00FF00FF;
  uint32_t rb = ((pix & mask) * scale) >> 8;
  uint32_t ag = ((pix >> 8) & mask) * scale;
  return (rb & mask) | (ag & ~mask);
}

// Blend 'src' over 'dst' assuming they are pre-multiplied by alpha.
static uint32_t BlendPixelPremult(uint32_t src, uint32_t dst) {
  const uint8_t src_a = (src >> CHANNEL_SHIFT(3)) & 0xff;
  return src + ChannelwiseMultiply(dst, 256 - src_a);
}

// Blend 'num_pixels' in 'src' over 'dst' assuming they are pre-multiplied by
// alpha.
static void BlendPixelRowPremult(uint32_t* const src, const uint32_t* const dst,
                                 int num_pixels) {
  int i;
  for (i = 0; i < num_pixels; ++i) {
    const uint8_t src_alpha = (src[i] >> CHANNEL_SHIFT(3)) & 0xff;
    if (src_alpha != 0xff) {
      src[i] = BlendPixelPremult(src[i], dst[i]);
    }
  }
}

// Returns two ranges (<left, width> pairs) at row 'canvas_y', that belong to
// 'src' but not 'dst'. A point range is empty if the corresponding width is 0.
static void FindBlendRangeAtRow(const WebPIterator* const src,
                                const WebPIterator* const dst, int canvas_y,
                                int* const left1, int* const width1,
                                int* const left2, int* const width2) {
  const int src_max_x = src->x_offset + src->width;
  const int dst_max_x = dst->x_offset + dst->width;
  const int dst_max_y = dst->y_offset + dst->height;
  assert(canvas_y >= src->y_offset && canvas_y < (src->y_offset + src->height));
  *left1 = -1;
  *width1 = 0;
  *left2 = -1;
  *width2 = 0;

  if (canvas_y < dst->y_offset || canvas_y >= dst_max_y ||
      src->x_offset >= dst_max_x || src_max_x <= dst->x_offset) {
    *left1 = src->x_offset;
    *width1 = src->width;
    return;
  }

  if (src->x_offset < dst->x_offset) {
    *left1 = src->x_offset;
    *width1 = dst->x_offset - src->x_offset;
  }

  if (src_max_x > dst_max_x) {
    *left2 = dst_max_x;
    *width2 = src_max_x - dst_max_x;
  }
}

int WebPAnimDecoderGetNext(WebPAnimDecoder* dec,
                           uint8_t** buf_ptr, int* timestamp_ptr) {
  WebPIterator iter;
  uint32_t width;
  uint32_t height;
  int out_w, out_h;
  int is_key_frame;
  int timestamp;
  BlendRowFunc blend_row;

  if (dec == NULL || buf_ptr == NULL || timestamp_ptr == NULL) return 0;
  if (!WebPAnimDecoderHasMoreFrames(dec)) return 0;

  width = dec->info.canvas_width;
  height = dec->info.canvas_height;
  out_w = dec->out_canvas_w > 0 ? dec->out_canvas_w : (int)width;
  out_h = dec->out_canvas_h > 0 ? dec->out_canvas_h : (int)height;
  blend_row = dec->blend_func;

  // Get compressed frame.
  if (!WebPDemuxGetFrame(dec->demux, dec->next_frame, &iter)) {
    return 0;
  }
  timestamp = dec->prev_frame_timestamp + iter.duration;

  // Initialize.
  is_key_frame = IsKeyFrame(&iter, &dec->prev_iter,
                            dec->prev_frame_was_keyframe, width, height);
  if (is_key_frame) {
    if (!ZeroFillCanvas(dec->curr_frame, out_w, out_h)) {
      goto Error;
    }
  } else {
    if (!CopyCanvas(dec->prev_frame_disposed, dec->curr_frame,
                    out_w, out_h)) {
      goto Error;
    }
  }

  // Decode.
  {
    const uint8_t* in = iter.fragment.bytes;
    const size_t in_size = iter.fragment.size;
    const int x_off  = SCALE_DIM(iter.x_offset, width, out_w);
    const int y_off  = SCALE_DIM(iter.y_offset, height, out_h);
    const int frm_w  = SCALE_DIM(iter.width, width, out_w);
    const int frm_h  = SCALE_DIM(iter.height, height, out_h);
    const uint32_t stride = (uint32_t)out_w * NUM_CHANNELS;
    const uint64_t out_offset = (uint64_t)y_off * stride +
                                (uint64_t)x_off * NUM_CHANNELS;
    const uint64_t size = (uint64_t)frm_h * stride;
    WebPDecoderConfig* const config = &dec->config;
    WebPRGBABuffer* const buf = &config->output.u.RGBA;
    if ((size_t)size != size) goto Error;

    config->options.scaled_width  = frm_w;
    config->options.scaled_height = frm_h;
    buf->stride = (int)stride;
    buf->size = (size_t)size;
    buf->rgba = dec->curr_frame + out_offset;

    if (WebPDecode(in, in_size, config) != VP8_STATUS_OK) {
      goto Error;
    }
  }

  // During the decoding of current frame, we may have set some pixels to be
  // transparent (i.e. alpha < 255). However, the value of each of these
  // pixels should have been determined by blending it against the value of
  // that pixel in the previous frame if blending method of is WEBP_MUX_BLEND.
  if (iter.frame_num > 1 && iter.blend_method == WEBP_MUX_BLEND &&
      !is_key_frame) {
    const int x_off  = SCALE_DIM(iter.x_offset, width, out_w);
    const int y_off  = SCALE_DIM(iter.y_offset, height, out_h);
    const int frm_w  = SCALE_DIM(iter.width, width, out_w);
    const int frm_h  = SCALE_DIM(iter.height, height, out_h);
    if (dec->prev_iter.dispose_method == WEBP_MUX_DISPOSE_NONE) {
      int y;
      // Blend transparent pixels with pixels in previous canvas.
      for (y = 0; y < frm_h; ++y) {
        const size_t offset = (y_off + y) * out_w + x_off;
        blend_row((uint32_t*)dec->curr_frame + offset,
                  (uint32_t*)dec->prev_frame_disposed + offset, frm_w);
      }
    } else {
      int y;
      // DISPOSE_BACKGROUND: prev_frame_disposed is already zeroed inside the
      // previous frame rectangle, so blending src over transparent inside that
      // rect is a no-op. Simply blend the full frame region at scaled res.
      for (y = 0; y < frm_h; ++y) {
        const size_t offset = (y_off + y) * out_w + x_off;
        blend_row((uint32_t*)dec->curr_frame + offset,
                  (uint32_t*)dec->prev_frame_disposed + offset, frm_w);
      }
    }
  }

  // Update info of the previous frame and dispose it for the next iteration.
  dec->prev_frame_timestamp = timestamp;
  WebPDemuxReleaseIterator(&dec->prev_iter);
  dec->prev_iter = iter;
  dec->prev_frame_was_keyframe = is_key_frame;
  if (!CopyCanvas(dec->curr_frame, dec->prev_frame_disposed, out_w, out_h)) {
    goto Error;
  }
  if (dec->prev_iter.dispose_method == WEBP_MUX_DISPOSE_BACKGROUND) {
    const int pw = (int)dec->prev_iter.width;
    const int ph = (int)dec->prev_iter.height;
    ZeroFillFrameRect(dec->prev_frame_disposed, out_w * NUM_CHANNELS,
                      SCALE_DIM(dec->prev_iter.x_offset, width, out_w),
                      SCALE_DIM(dec->prev_iter.y_offset, height, out_h),
                      SCALE_DIM(pw, width, out_w),
                      SCALE_DIM(ph, height, out_h));
  }
  ++dec->next_frame;

  // All OK, fill in the values.
  *buf_ptr = dec->curr_frame;
  *timestamp_ptr = timestamp;
  return 1;

 Error:
  WebPDemuxReleaseIterator(&iter);
  return 0;
}

int WebPAnimDecoderHasMoreFrames(const WebPAnimDecoder* dec) {
  if (dec == NULL) return 0;
  return (dec->next_frame <= (int)dec->info.frame_count);
}

void WebPAnimDecoderReset(WebPAnimDecoder* dec) {
  if (dec != NULL) {
    dec->prev_frame_timestamp = 0;
    WebPDemuxReleaseIterator(&dec->prev_iter);
    memset(&dec->prev_iter, 0, sizeof(dec->prev_iter));
    dec->prev_frame_was_keyframe = 0;
    dec->next_frame = 1;
  }
}

const WebPDemuxer* WebPAnimDecoderGetDemuxer(const WebPAnimDecoder* dec) {
  if (dec == NULL) return NULL;
  return dec->demux;
}

void WebPAnimDecoderDelete(WebPAnimDecoder* dec) {
  if (dec != NULL) {
    WebPDemuxReleaseIterator(&dec->prev_iter);
    WebPDemuxDelete(dec->demux);
    WebPSafeFree(dec->curr_frame);
    WebPSafeFree(dec->prev_frame_disposed);
    WebPSafeFree(dec);
  }
}

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <webp/encode.h>
#include <webp/mux.h>

// Per-pixel hash producing visually noisy content (hard to DCT-compress).
// Varying fi ensures every frame yields a unique bitstream.
static inline uint32_t px_hash(uint32_t x, uint32_t y, uint32_t fi)
{
    uint32_t h = fi * 2654435761u ^ x * 73856093u ^ y * 19349663u;
    h ^= h >> 16; h *= 0x45d9f3bu; h ^= h >> 16;
    return h;
}

// Hash at 4x4 block granularity: neighbouring pixels share values,
// DCT high-frequency coefficients stay near zero, giving realistic
// compression ratios similar to natural photos.
static void FillFrame(WebPPicture* pic, int frame_idx)
{
    uint32_t fi = (uint32_t)frame_idx;
    uint32_t* row = pic->argb;
    for (int y = 0; y < pic->height; y++, row += pic->argb_stride) {
        for (int x = 0; x < pic->width; x++) {
            uint32_t h = px_hash((uint32_t)(x >> 4), (uint32_t)(y >> 4), fi);
            row[x] = 0xFF000000u | (h & 0x00FFFFFFu);
        }
    }
}

int main(int argc, char* argv[])
{
    const char* out = (argc > 1) ? argv[1] : "test/perf.webp";
    int N = (argc > 2) ? atoi(argv[2]) : 300;
    float Q = (argc > 3) ? (float)atof(argv[3]) : 30.f;
    bool no_alpha = false;
    for (int a = 4; a < argc; a++)
        if (strcmp(argv[a], "--no-alpha") == 0) no_alpha = true;
    const int W = 3000, H = 2000, DELAY_MS = 100;

    printf("Generating %dx%d %d-frame animated WebP quality=%.0f %s-> %s\n",
           W, H, N, Q, no_alpha ? "(no-alpha) " : "", out);

    WebPAnimEncoderOptions enc_opts;
    WebPAnimEncoderOptionsInit(&enc_opts);
    WebPAnimEncoder* enc = WebPAnimEncoderNew(W, H, &enc_opts);
    if (!enc) { fprintf(stderr, "WebPAnimEncoderNew failed\n"); return 1; }

    WebPConfig config;
    WebPConfigInit(&config);
    config.quality = Q;
    config.method  = 0;

    int timestamp = 0;
    for (int i = 0; i < N; i++) {
        WebPPicture pic;
        WebPPictureInit(&pic);
        pic.width    = W;
        pic.height   = H;
        pic.use_argb = 1;
        if (!WebPPictureAlloc(&pic)) {
            fprintf(stderr, "WebPPictureAlloc failed at frame %d\n", i);
            WebPAnimEncoderDelete(enc);
            return 1;
        }

        FillFrame(&pic, i);

        // Convert ARGB → YUV420 (no alpha plane) so the encoded file has has_alpha=0.
        if (no_alpha && !WebPPictureARGBToYUVA(&pic, WEBP_YUV420)) {
            fprintf(stderr, "WebPPictureARGBToYUVA failed at frame %d\n", i);
            WebPPictureFree(&pic);
            WebPAnimEncoderDelete(enc);
            return 1;
        }

        if (!WebPAnimEncoderAdd(enc, &pic, timestamp, &config)) {
            fprintf(stderr, "WebPAnimEncoderAdd failed at frame %d\n", i);
            WebPPictureFree(&pic);
            WebPAnimEncoderDelete(enc);
            return 1;
        }
        WebPPictureFree(&pic);
        timestamp += DELAY_MS;

        if ((i + 1) % 10 == 0)
            printf("  %d/%d frames encoded\n", i + 1, N);
    }

    WebPAnimEncoderAdd(enc, nullptr, timestamp, nullptr);

    WebPData data;
    WebPDataInit(&data);
    WebPAnimEncoderAssemble(enc, &data);
    WebPAnimEncoderDelete(enc);

    FILE* f = nullptr;
    fopen_s(&f, out, "wb");
    if (!f) { fprintf(stderr, "Cannot open %s for writing\n", out); WebPDataClear(&data); return 1; }
    fwrite(data.bytes, 1, data.size, f);
    fclose(f);

    printf("Done: %.1f MB (%.1f KB/frame)\n",
           (double)data.size / 1048576.0,
           (double)data.size / N / 1024.0);
    WebPDataClear(&data);
    return 0;
}

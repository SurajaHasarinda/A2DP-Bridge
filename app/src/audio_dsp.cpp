/*
 * Captured-audio processing - Implementation
 */

#include "audio_dsp.h"

#include <cmath>

namespace {

/* SPEAKER_* bits of ksmedia.h, in channel order */
constexpr uint32_t FL = 0x1, FR = 0x2, FC = 0x4, LFE = 0x8, BL = 0x10, BR = 0x20, FLC = 0x40, FRC = 0x80,
                   BC = 0x100, SL = 0x200, SR = 0x400;

constexpr float MINUS_3DB = 0.70710678f;
constexpr float MINUS_6DB = 0.5f;
constexpr float KNEE = 0.89125094f;  /* -1 dBFS */

/* Left / right gain of one speaker in the stereo downmix */
void speaker_gains(uint32_t speaker, float &l, float &r) {
    switch (speaker) {
    case FL: l = 1.0f; r = 0.0f; break;
    case FR: l = 0.0f; r = 1.0f; break;
    case FC: case BC: l = r = MINUS_3DB; break;
    case LFE: l = r = MINUS_6DB; break;
    case BL: case SL: case FLC: l = MINUS_3DB; r = 0.0f; break;
    case BR: case SR: case FRC: l = 0.0f; r = MINUS_3DB; break;
    default: l = r = MINUS_6DB; break;  /* top speakers: half to each side */
    }
}

} // namespace

uint32_t default_channel_mask(uint32_t channels) {
    switch (channels) {
    case 1: return FC;
    case 2: return FL | FR;
    case 3: return FL | FR | FC;
    case 4: return FL | FR | BL | BR;
    case 5: return FL | FR | FC | BL | BR;
    case 6: return FL | FR | FC | LFE | BL | BR;
    case 7: return FL | FR | FC | LFE | BC | SL | SR;
    case 8: return FL | FR | FC | LFE | BL | BR | SL | SR;
    default: return 0;
    }
}

void downmix_to_stereo(const float *src, uint32_t frames, uint32_t channels, uint32_t channel_mask,
                       float *dst) {
    /* Gains per channel: the n-th set bit of the mask is the n-th channel */
    float gl[32], gr[32];
    uint32_t mask = channel_mask ? channel_mask : default_channel_mask(channels);
    uint32_t n = 0;
    for (uint32_t bit = 1; bit && n < channels && n < 32; bit <<= 1) {
        if (mask & bit) {
            speaker_gains(bit, gl[n], gr[n]);
            n++;
        }
    }
    for (; n < channels && n < 32; n++) gl[n] = gr[n] = MINUS_6DB;  /* channels the mask doesn't name */
    const uint32_t used = channels < 32 ? channels : 32;

    for (uint32_t f = 0; f < frames; f++) {
        const float *in = src + static_cast<size_t>(f) * channels;
        float l = 0.0f, r = 0.0f;
        for (uint32_t c = 0; c < used; c++) {
            l += in[c] * gl[c];
            r += in[c] * gr[c];
        }
        dst[f * 2] = l;
        dst[f * 2 + 1] = r;
    }
}

float soft_clip(float x) {
    const float a = std::fabs(x);
    if (a <= KNEE) return x;
    const float y = KNEE + (1.0f - KNEE) * std::tanh((a - KNEE) / (1.0f - KNEE));
    return x < 0 ? -y : y;
}

void float_to_int16(const float *src, int16_t *dst, uint32_t samples, uint32_t &state) {
    if (state == 0) state = 0x9E3779B9u;
    for (uint32_t i = 0; i < samples; i++) {
        const float s = src[i];
        if (s == 0.0f) {  /* keep digital silence exact: no dither noise in pauses */
            dst[i] = 0;
            continue;
        }
        /* Two uniform values in [0, 1) (xorshift32) give triangular noise in (-1, 1) LSB */
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        const float u1 = (state >> 8) * (1.0f / 16777216.0f);
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        const float u2 = (state >> 8) * (1.0f / 16777216.0f);
        float v = std::floor(soft_clip(s) * 32767.0f + (u1 - u2) + 0.5f);
        if (v > 32767.0f) v = 32767.0f;
        if (v < -32768.0f) v = -32768.0f;
        dst[i] = static_cast<int16_t>(v);
    }
}

void float_to_int32(const float *src, int32_t *dst, uint32_t samples) {
    for (uint32_t i = 0; i < samples; i++) {
        double v = std::floor(static_cast<double>(soft_clip(src[i])) * 2147483647.0 + 0.5);
        if (v > 2147483647.0) v = 2147483647.0;
        if (v < -2147483648.0) v = -2147483648.0;
        dst[i] = static_cast<int32_t>(v);
    }
}

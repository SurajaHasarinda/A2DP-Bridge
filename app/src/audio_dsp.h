/*
 * Captured-audio processing shared by the GUI service and the CLI:
 * surround downmix, soft clipping and float -> integer conversion.
 */

#ifndef AUDIO_DSP_H
#define AUDIO_DSP_H

#include <cstdint>

/* Default WAVEFORMATEXTENSIBLE channel mask for a channel count, used when
 * the format carries none (1.0, 2.0, 3.0, quad, 5.0, 5.1, 6.1, 7.1) */
uint32_t default_channel_mask(uint32_t channels);

/* Downmix interleaved float audio to stereo (ITU-R BS.775: centre and
 * surrounds at -3 dB, LFE at -6 dB). The speaker of each channel comes from
 * channel_mask (SPEAKER_* bits, in channel order); 0 = default for the count.
 * dst holds frames * 2 floats. The result may exceed full scale: the
 * conversion below soft-clips it. */
void downmix_to_stereo(const float *src, uint32_t frames, uint32_t channels, uint32_t channel_mask,
                       float *dst);

/* Soft clipper: unchanged below -1 dBFS, then bends smoothly towards full
 * scale instead of cutting peaks flat */
float soft_clip(float x);

/* float -> int16 with soft clipping and TPDF dither (+-1 LSB, triangular);
 * exact digital silence stays silent. state: per-stream noise generator. */
void float_to_int16(const float *src, int16_t *dst, uint32_t samples, uint32_t &state);

/* float -> int32 (24/32-bit encoders) with soft clipping and rounding */
void float_to_int32(const float *src, int32_t *dst, uint32_t samples);

#endif /* AUDIO_DSP_H */

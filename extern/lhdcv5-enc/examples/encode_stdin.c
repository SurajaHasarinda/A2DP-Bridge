/*
 * Minimal example: read raw interleaved stereo PCM from stdin, write raw
 * LHDC V5 packets to stdout.
 *
 *   encode_stdin <sample_rate> <bits(16|24)> <bitrate_index 0..12>
 *
 * e.g. 48 kHz / 16-bit / ~400 kbps:
 *   ffmpeg -i song.flac -f s16le -ac 2 -ar 48000 - | encode_stdin 48000 16 5 > song.lhdc
 */
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "lhdcv5_enc.h"

int main(int argc, char **argv) {
  uint32_t sr = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 48000;
  uint32_t bits = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 16;
  uint32_t inx = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : LHDC_QUALITY_LOW;
  lhdcv5_enc_t *enc;
  uint32_t block = 0;
  size_t frame_bytes;
  uint8_t pcm[960 * 2 * 3]; /* max: 960 samples x 2 ch x 3 bytes */
  uint8_t out[4096];
  int32_t ret;

#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif

  enc = lhdcv5_enc_new(LHDC_VERSION_1);
  if (enc == NULL) {
    fprintf(stderr, "failed to create encoder\n");
    return 1;
  }
  ret = lhdcv5_enc_init_encoder(enc, sr, bits, inx, LHDC_MTU_2MBPS,
                                LHDC_ENC_INTERVAL_20MS);
  if (ret != LHDC_FRET_SUCCESS) {
    fprintf(stderr, "init failed: %d\n", ret);
    return 1;
  }
  lhdcv5_enc_get_block_size(enc, &block);
  frame_bytes = (size_t)block * 2 * bits / 8;
  fprintf(stderr, "block=%u samples/ch, %u bytes PCM per frame\n", block,
          (unsigned)frame_bytes);

  while (fread(pcm, 1, frame_bytes, stdin) == frame_bytes) {
    uint32_t written = 0, frames = 0;
    ret = lhdcv5_enc_encode(enc, pcm, frame_bytes, out, sizeof(out), &written, &frames);
    if (ret != LHDC_FRET_SUCCESS) {
      fprintf(stderr, "encode failed: %d\n", ret);
      return 1;
    }
    if (written > 0) {
      fwrite(out, 1, written, stdout);
    }
  }
  lhdcv5_enc_free(enc);
  return 0;
}

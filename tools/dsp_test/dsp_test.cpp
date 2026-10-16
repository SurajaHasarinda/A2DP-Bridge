/*
 * Self-check of audio_dsp: prints PASS / FAIL per check, exit code 1 on failure.
 *   build/tools/dsp_test/a2dpwb_dsp_test.exe
 */

#include "abr_controller.h"
#include "audio_dsp.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

static void check(bool ok, const char *what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

int main() {
    /* 7.1 (FL FR FC LFE BL BR SL SR): centre only -> both sides at -3 dB */
    {
        const float in[8] = {0, 0, 1, 0, 0, 0, 0, 0};
        float out[2];
        downmix_to_stereo(in, 1, 8, 0, out);
        check(near(out[0], 0.7071f) && near(out[1], 0.7071f), "7.1 centre reaches both sides at -3 dB");
    }
    /* 7.1: left surround only -> left side only */
    {
        const float in[8] = {0, 0, 0, 0, 0, 0, 1, 0};
        float out[2];
        downmix_to_stereo(in, 1, 8, 0, out);
        check(near(out[0], 0.7071f) && near(out[1], 0.0f), "7.1 side left goes to the left only");
    }
    /* Quad (FL FR BL BR) from the mask: rear right -> right only, no centre or LFE guess */
    {
        const float in[4] = {0, 0, 0, 1};
        float out[2];
        downmix_to_stereo(in, 1, 4, 0x1 | 0x2 | 0x10 | 0x20, out);
        check(near(out[0], 0.0f) && near(out[1], 0.7071f), "quad rear right goes to the right only");
    }
    /* Soft clip: transparent below -1 dBFS, never above full scale, monotonic */
    {
        bool ok = near(soft_clip(0.5f), 0.5f) && near(soft_clip(-0.8f), -0.8f);
        float prev = -2.0f;
        for (float x = -3.0f; x <= 3.0f; x += 0.01f) {
            const float y = soft_clip(x);
            if (std::fabs(y) > 1.0f || y < prev) ok = false;
            prev = y;
        }
        check(ok, "soft clip: unchanged below -1 dBFS, bounded, monotonic");
    }
    /* int16: silence stays exactly silent, full scale does not wrap, dither averages out */
    {
        std::vector<float> in(48000, 0.0f);
        std::vector<int16_t> out(in.size());
        uint32_t state = 0;
        float_to_int16(in.data(), out.data(), (uint32_t)in.size(), state);
        bool silent = true;
        for (int16_t s : out) silent = silent && s == 0;
        check(silent, "int16: digital silence stays exactly 0 (no dither noise)");

        const float loud[3] = {1.5f, -1.5f, 1.0f};
        int16_t lo[3];
        float_to_int16(loud, lo, 3, state);
        /* +-1.5 lands at full scale without wrapping; 1.0 sits inside the knee, just below it */
        check(lo[0] > 32700 && lo[1] < -32700 && lo[2] > 31000 && lo[2] < lo[0],
              "int16: overs are clipped softly, no wrap-around");

        for (float &v : in) v = 0.25f;
        float_to_int16(in.data(), out.data(), (uint32_t)in.size(), state);
        double sum = 0;
        for (int16_t s : out) sum += s;
        check(std::fabs(sum / out.size() - 0.25 * 32767.0) < 0.1, "int16: dithered level averages to the input");
    }
    /* int32: rounding and bounds */
    {
        const float in[3] = {0.5f, -1.0f, 2.0f};
        int32_t out[3];
        float_to_int32(in, out, 3);
        check(out[0] == 1073741824 && out[1] < -2000000000 && out[2] > 2000000000, "int32: rounded, bounded");
    }
    /* Stepped ABR: down on backlog (at most once a second), up after 10 s of calm, down on drops */
    {
        StepAbr abr;
        abr.reset(3, 200, 0, 0);  /* step down at 100 ms backlog */
        bool ok = abr.update(50, 0, 500) == 0;           /* below the threshold */
        ok = ok && abr.update(150, 0, 1000) == 1;        /* backed up: down */
        ok = ok && abr.update(150, 0, 1500) == 1;        /* not again within a second */
        ok = ok && abr.update(150, 0, 2000) == 2;
        ok = ok && abr.update(150, 0, 3100) == 2;        /* lowest step */
        ok = ok && abr.update(0, 0, 8000) == 2;          /* calm, but not long enough */
        ok = ok && abr.update(0, 0, 13200) == 1;         /* 10 s calm: up */
        ok = ok && abr.update(0, 0, 13300) == 1;         /* one step at a time */
        ok = ok && abr.update(0, 1, 14400) == 2;         /* new drops: down */
        check(ok, "stepped ABR: down on backlog or drops, up after a calm");
    }
    std::printf("%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}

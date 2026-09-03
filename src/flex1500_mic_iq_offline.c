// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_dsp.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FULL_DRIVE_COMPLEX_PEAK = 24890, FADE_SAMPLES = 480 };

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s INPUT.iq16le OUTPUT.iq16le usb|lsb DRIVE_PERCENT\n",
            program);
    fputs("Offline conversion only: this program performs no USB access.\n", stderr);
}

int main(int argc, char **argv)
{
    char *drive_end = NULL;
    long drive_percent = argc == 5 ? strtol(argv[4], &drive_end, 10) : 0;
    if (argc != 5 || (strcmp(argv[3], "usb") != 0 &&
                      strcmp(argv[3], "lsb") != 0) ||
        drive_end == argv[4] || *drive_end != '\0' ||
        drive_percent < 1 || drive_percent > 100) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    FILE *input = fopen(argv[1], "rb");
    if (input == NULL) { perror("open input"); return EXIT_FAILURE; }
    if (fseek(input, 0, SEEK_END) != 0) { perror("seek input"); fclose(input); return EXIT_FAILURE; }
    long bytes = ftell(input);
    rewind(input);
    if (bytes <= 0 || bytes % 4 != 0) {
        fputs("Input must contain interleaved stereo signed-16 samples.\n", stderr);
        fclose(input);
        return EXIT_FAILURE;
    }
    size_t frames = (size_t)bytes / 4;
    int16_t *raw = malloc((size_t)bytes);
    flex1500_iq_sample *iq = calloc(frames, sizeof(*iq));
    if (raw == NULL || iq == NULL || fread(raw, 4, frames, input) != frames) {
        fputs("Unable to read input.\n", stderr);
        free(raw); free(iq); fclose(input); return EXIT_FAILURE;
    }
    fclose(input);

    flex1500_tx_dsp dsp;
    flex1500_tx_dsp_init(&dsp, strcmp(argv[3], "usb") == 0 ?
                         FLEX1500_TX_USB : FLEX1500_TX_LSB);
    float peak = 0.0f;
    for (size_t n = 0; n < frames; ++n) {
        iq[n] = flex1500_tx_dsp_process(&dsp, (float)raw[2 * n]);
        float magnitude = hypotf(iq[n].i, iq[n].q);
        if (magnitude > peak) peak = magnitude;
    }
    free(raw);
    if (peak < 1.0f) { fputs("No usable microphone audio found.\n", stderr); free(iq); return EXIT_FAILURE; }

    FILE *output = fopen(argv[2], "wb");
    if (output == NULL) { perror("open output"); free(iq); return EXIT_FAILURE; }
    float target_peak = FULL_DRIVE_COMPLEX_PEAK * drive_percent / 100.0f;
    float scale = target_peak / peak;
    float written_peak = 0.0f;
    for (size_t n = 0; n < frames; ++n) {
        float fade = 1.0f;
        if (n < FADE_SAMPLES) fade = (float)n / FADE_SAMPLES;
        if (frames - n <= FADE_SAMPLES) fade = fminf(fade, (float)(frames - n - 1) / FADE_SAMPLES);
        int16_t sample[2] = {
            (int16_t)lrintf(iq[n].i * scale * fade),
            (int16_t)lrintf(iq[n].q * scale * fade),
        };
        float magnitude = hypotf((float)sample[0], (float)sample[1]);
        if (magnitude > written_peak) written_peak = magnitude;
        if (fwrite(sample, sizeof(sample), 1, output) != 1) {
            fputs("Unable to write output.\n", stderr);
            fclose(output); free(iq); return EXIT_FAILURE;
        }
    }
    fclose(output);
    free(iq);
    printf("frames: %zu\nsideband: %s\ndrive: %ld%%\n"
           "target complex peak: %.3f\nsource complex peak: %.3f\n"
           "scale: %.6f\noutput complex peak: %.3f\n",
           frames, argv[3], drive_percent, target_peak, peak, scale,
           written_peak);
    puts("No USB device was opened and no radio state was changed.");
    return EXIT_SUCCESS;
}

/*
 * ptc_verify.c
 * Executable 1 of 3: Camera sanity check.
 *
 * Run this first, every session, before touching the laser.
 * Confirms the camera is alive, ISP is off, and dark level
 * is sensible.
 *
 * Usage:
 *   ./ptc_verify
 *
 * Expected output:
 *   - Camera found and initialised
 *   - All ISP settings confirmed
 *   - N dark frames captured; mean and std printed for each
 *   - One light frame at mid-range exposure; mean printed
 *   - Pass/fail summary
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "CameraApi.h"
#include "camera_utils.h"
#include "camera_config.h"

/* Dark level is suspicious if mean exceeds this */
#define DARK_MEAN_WARN  10.0
/* Dark level is definitely wrong above this */
#define DARK_MEAN_FAIL  30.0
/* Mid-range verification exposure [us] */
#define VERIFY_EXPOSURE_US  5000.0

int main(void)
{
    printf("==============================================\n");
    printf(" HT-SUA33GM-T1V-C  —  Camera Verification\n");
    printf("==============================================\n\n");

    /* --- Initialise and configure camera --- */
    CameraHandle hCam = cam_init();
    cam_configure(hCam);

    uint8_t *buf = malloc(CAM_N_PIXELS);
    if (!buf) {
        fprintf(stderr, "Out of memory.\n");
        cam_teardown(hCam);
        return 1;
    }

    int all_ok = 1;

    /* --------------------------------------------------------------- *
     * Stage 1: Dark frames
     * --------------------------------------------------------------- */
    printf("-----------------------------------------------\n");
    printf(" Stage 1: Dark frames\n");
    printf(" Block the beam / put lens cap on, then press Enter.\n");
    printf("-----------------------------------------------\n");
    getchar();

    /* Use minimum exposure for dark frames */
    cam_set_exposure(hCam, DEFAULT_T_MIN_US);

    double dark_mean_sum = 0.0;
    int n_dark = 5;

    printf("  %-6s  %-10s  %-10s\n", "Frame", "Mean(ADU)", "Std(ADU)");
    printf("  %-6s  %-10s  %-10s\n", "-----", "---------", "--------");

    for (int i = 0; i < n_dark; i++) {
        if (cam_capture_frame(hCam, buf) != 0) {
            fprintf(stderr, "  Frame %d capture failed.\n", i);
            all_ok = 0;
            continue;
        }
        double mean, std;
        frame_stats_roi(buf, &mean, &std, NULL, NULL);
        printf("  %-6d  %-10.2f  %-10.2f\n", i, mean, std);
        dark_mean_sum += mean;
    }

    double dark_mean_avg = dark_mean_sum / n_dark;
    printf("\n  Average dark mean: %.2f ADU\n", dark_mean_avg);

    if (dark_mean_avg > DARK_MEAN_FAIL) {
        printf("  [FAIL] Dark level too high (%.1f ADU). "
               "Check that beam is blocked.\n", dark_mean_avg);
        all_ok = 0;
    } else if (dark_mean_avg > DARK_MEAN_WARN) {
        printf("  [WARN] Dark level slightly elevated (%.1f ADU). "
               "Verify light leaks.\n", dark_mean_avg);
    } else {
        printf("  [PASS] Dark level nominal.\n");
    }

    /* --------------------------------------------------------------- *
     * Stage 2: Light response check
     * --------------------------------------------------------------- */
    printf("\n-----------------------------------------------\n");
    printf(" Stage 2: Light response\n");
    printf(" Remove lens cap / unblock beam, then press Enter.\n");
    printf("-----------------------------------------------\n");
    getchar();

    cam_set_exposure(hCam, VERIFY_EXPOSURE_US);

    /* Capture and discard one frame to let exposure settle */
    cam_capture_frame(hCam, buf);

    if (cam_capture_frame(hCam, buf) != 0) {
        fprintf(stderr, "  Light frame capture failed.\n");
        all_ok = 0;
    } else {
        double mean, std, mn, mx;
        frame_stats_roi(buf, &mean, &std, &mn, &mx);
        printf("  Exposure: %.0f us\n", VERIFY_EXPOSURE_US);
        printf("  ROI mean: %.1f ADU   std: %.2f   "
               "min: %.0f   max: %.0f\n", mean, std, mn, mx);

        if (mean < 5.0) {
            printf("  [FAIL] No signal detected. "
                   "Check beam path and connections.\n");
            all_ok = 0;
        } else if (mean > 250.0) {
            printf("  [WARN] Already saturating at %.0f us. "
                   "Add ND attenuation before PTC sweep.\n",
                   VERIFY_EXPOSURE_US);
        } else {
            printf("  [PASS] Camera responding to light.\n");
        }
    }

    /* --------------------------------------------------------------- *
     * Summary
     * --------------------------------------------------------------- */
    printf("\n==============================================\n");
    if (all_ok) {
        printf(" RESULT: PASS — Camera ready for calibration.\n");
        printf(" Next step: run ptc_acquire\n");
    } else {
        printf(" RESULT: FAIL — See warnings above.\n");
    }
    printf("==============================================\n\n");

    free(buf);
    cam_teardown(hCam);
    return all_ok ? 0 : 1;
}

/*
 * ptc_acquire.c
 * Executable 2 of 3: PTC flat-pair acquisition sweep.
 *
 * Captures dark frames and logarithmically-spaced flat pairs
 * for the Photon Transfer Curve. Output is raw 8-bit binary
 * frames plus a metadata.txt file.
 *
 * Usage:
 *   ./ptc_acquire [options]
 *
 * Options:
 *   -n <steps>     Number of flat-pair steps (default: 20)
 *   -d <frames>    Number of dark frames     (default: 10)
 *   -t <us>        Minimum exposure [us]     (default: 10.0)
 *   -T <us>        Maximum exposure [us]     (default: 20000.0)
 *   -h             Show this help
 *
 * Output directory: data/ptc_YYYYMMDD_HHMMSS/
 *   dark_NN.bin        — dark frames
 *   flat_NN_A.bin      — flat pair A frames
 *   flat_NN_B.bin      — flat pair B frames
 *   metadata.txt       — all parameters and exposure times
 *
 * After acquisition, run analysis/ptc_analysis.py to extract
 * K (gain), sigma_read, FWC, DR, and SNR.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "CameraApi.h"
#include "camera_utils.h"
#include "file_utils.h"
#include "camera_config.h"

/* ------------------------------------------------------------------ */

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -n <steps>   Number of flat-pair steps "
           "(default: %d)\n", DEFAULT_N_STEPS);
    printf("  -d <frames>  Number of dark frames "
           "(default: %d)\n", DEFAULT_N_DARK);
    printf("  -t <us>      Minimum exposure in us "
           "(default: %.1f)\n", DEFAULT_T_MIN_US);
    printf("  -T <us>      Maximum exposure in us "
           "(default: %.1f)\n", DEFAULT_T_MAX_US);
    printf("  -h           Show this help\n\n");
}

/* ------------------------------------------------------------------ */

static void wait_for_enter(void)
{
    /* Flush any pending newline from previous input */
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    /* --- Parse command-line arguments --- */
    int    n_steps  = DEFAULT_N_STEPS;
    int    n_dark   = DEFAULT_N_DARK;
    double t_min_us = DEFAULT_T_MIN_US;
    double t_max_us = DEFAULT_T_MAX_US;

    int opt;
    while ((opt = getopt(argc, argv, "n:d:t:T:h")) != -1) {
        switch (opt) {
        case 'n': n_steps  = atoi(optarg); break;
        case 'd': n_dark   = atoi(optarg); break;
        case 't': t_min_us = atof(optarg); break;
        case 'T': t_max_us = atof(optarg); break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    if (n_steps < 2 || n_dark < 1) {
        fprintf(stderr, "Error: n_steps >= 2 and n_dark >= 1 required.\n");
        return 1;
    }
    if (t_min_us >= t_max_us) {
        fprintf(stderr, "Error: t_min must be less than t_max.\n");
        return 1;
    }

    printf("==============================================\n");
    printf(" HT-SUA33GM-T1V-C  —  PTC Acquisition\n");
    printf("==============================================\n");
    printf(" Dark frames : %d\n", n_dark);
    printf(" Flat steps  : %d\n", n_steps);
    printf(" t_min       : %.1f us\n", t_min_us);
    printf(" t_max       : %.1f us\n", t_max_us);
    printf("==============================================\n\n");

    /* --- Compute log-spaced exposure times --- */
    double *exposures = malloc(n_steps * sizeof(double));
    if (!exposures) { fprintf(stderr, "Out of memory.\n"); return 1; }
    logspace(t_min_us, t_max_us, n_steps, exposures);

    /* --- Allocate frame buffers --- */
    uint8_t *buf_A = malloc(CAM_N_PIXELS);
    uint8_t *buf_B = malloc(CAM_N_PIXELS);
    if (!buf_A || !buf_B) {
        fprintf(stderr, "Out of memory.\n");
        free(exposures);
        return 1;
    }

    /* --- Initialise camera --- */
    CameraHandle hCam = cam_init();
    cam_configure(hCam);

    /* --- Create output directory and metadata file --- */
    char out_dir[512];
    make_output_dir(PTC_DIR_PREFIX, out_dir, sizeof(out_dir));

    FILE *meta = open_metadata(out_dir);
    meta_writef(meta, "acquisition_type", "%s", "ptc");
    meta_writef(meta, "n_dark",           "%d", n_dark);
    meta_writef(meta, "n_steps",          "%d", n_steps);
    meta_writef(meta, "t_min_us",         "%.2f", t_min_us);
    meta_writef(meta, "t_max_us",         "%.2f", t_max_us);

    /* Write all exposure times as a comma-separated list */
    fprintf(meta, "%-30s = ", "exposures_us");
    for (int k = 0; k < n_steps; k++) {
        fprintf(meta, "%.2f%s", exposures[k],
                k < n_steps - 1 ? "," : "\n");
    }

    /* --------------------------------------------------------------- *
     * Stage 1: Dark frames
     * --------------------------------------------------------------- */
    printf("-----------------------------------------------\n");
    printf(" Stage 1: Dark frames (%d)\n", n_dark);
    printf(" Block the beam completely, then press Enter.\n");
    printf("-----------------------------------------------\n");
    wait_for_enter();

    cam_set_exposure(hCam, t_min_us);

    printf("  %-6s  %-12s\n", "Frame", "Mean (ADU)");
    printf("  %-6s  %-12s\n", "-----", "----------");

    for (int i = 0; i < n_dark; i++) {
        if (cam_capture_frame(hCam, buf_A) != 0) {
            fprintf(stderr, "  [ERROR] Dark frame %d failed.\n", i);
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/dark_%02d.bin", out_dir, i);
        save_frame(path, buf_A);

        double mean;
        frame_stats_roi(buf_A, &mean, NULL, NULL, NULL);
        printf("  %-6d  %-12.2f\n", i, mean);
    }

    /* --------------------------------------------------------------- *
     * Stage 2: Preview loop — dial in ND filters
     * --------------------------------------------------------------- */
    printf("\n-----------------------------------------------\n");
    printf(" Stage 2: Preview — Dial in ND filters\n");
    printf(" Unblock the beam, then press Enter for a preview.\n");
    printf(" Target: mean ADU in [%d, %d] at max exposure.\n",
           PREVIEW_TARGET_LOW, PREVIEW_TARGET_HIGH);
    printf("-----------------------------------------------\n");
    wait_for_enter();

    cam_set_exposure(hCam, t_max_us);

    while (1) {
        /* Discard one frame to let the new exposure settle */
        cam_capture_frame(hCam, buf_A);
        if (cam_capture_frame(hCam, buf_A) != 0) {
            fprintf(stderr, "  Preview capture failed. Try again.\n");
            wait_for_enter();
            continue;
        }

        double mean;
        frame_stats_roi(buf_A, &mean, NULL, NULL, NULL);

        printf("\n  Preview at t = %.0f us:  mean = %.1f ADU\n",
               t_max_us, mean);

        if (mean < PREVIEW_WARN_LOW) {
            printf("  [LOW]  Signal very faint. "
                   "Remove ND filters or increase laser power.\n");
        } else if (mean < PREVIEW_TARGET_LOW) {
            printf("  [LOW]  Signal below target. "
                   "Reduce ND attenuation slightly.\n");
        } else if (mean > PREVIEW_WARN_HIGH) {
            printf("  [HIGH] Already saturating. "
                   "Add more ND attenuation.\n");
        } else {
            printf("  [OK]   Signal in target range.\n");
        }

        printf("\n  Press Enter to re-check, or type 'y' then "
               "Enter to proceed: ");
        fflush(stdout);

        char line[16];
        if (fgets(line, sizeof(line), stdin) &&
            (line[0] == 'y' || line[0] == 'Y')) {
            break;
        }
    }

    /* --------------------------------------------------------------- *
     * Stage 3: Flat-pair sweep
     * --------------------------------------------------------------- */
    printf("\n-----------------------------------------------\n");
    printf(" Stage 3: Flat-pair sweep (%d steps)\n", n_steps);
    printf("-----------------------------------------------\n");
    printf("  %-8s  %-12s  %-12s  %-12s\n",
           "Step", "Exp (us)", "Mean A (ADU)", "Mean B (ADU)");
    printf("  %-8s  %-12s  %-12s  %-12s\n",
           "----", "--------", "-----------", "-----------");

    for (int k = 0; k < n_steps; k++) {
        cam_set_exposure(hCam, exposures[k]);

        /* Discard one frame so the exposure setting has taken effect */
        cam_capture_frame(hCam, buf_A);

        /* Frame A */
        if (cam_capture_frame(hCam, buf_A) != 0) {
            fprintf(stderr, "  [ERROR] Step %02d frame A failed.\n", k);
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/flat_%02d_A.bin", out_dir, k);
        save_frame(path, buf_A);

        /* Frame B — immediately after, no settings change */
        if (cam_capture_frame(hCam, buf_B) != 0) {
            fprintf(stderr, "  [ERROR] Step %02d frame B failed.\n", k);
            continue;
        }
        snprintf(path, sizeof(path), "%s/flat_%02d_B.bin", out_dir, k);
        save_frame(path, buf_B);

        /* Print one-line progress */
        double mean_A, mean_B;
        frame_stats_roi(buf_A, &mean_A, NULL, NULL, NULL);
        frame_stats_roi(buf_B, &mean_B, NULL, NULL, NULL);

        printf("  %02d/%-5d  %-12.1f  %-12.1f  %-12.1f\n",
               k + 1, n_steps, exposures[k], mean_A, mean_B);
    }

    /* --- Finalise --- */
    fclose(meta);
    free(buf_A);
    free(buf_B);
    free(exposures);
    cam_teardown(hCam);

    printf("\n==============================================\n");
    printf(" Acquisition complete.\n");
    printf(" Data written to: %s\n", out_dir);
    printf(" Next step: run analysis/ptc_analysis.py\n");
    printf("==============================================\n\n");

    return 0;
}

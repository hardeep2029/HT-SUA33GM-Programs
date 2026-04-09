/*
 * qe_acquire.c
 * Executable 3 of 3: QE measurement exposure sweep.
 *
 * Prompts for the power sensor reading, then sweeps exposure
 * from dark to saturation and prints a live QE estimate at each
 * step using the PTC-derived gain K.
 *
 * Usage:
 *   ./qe_acquire -K <gain_e_per_ADU> [options]
 *
 * Options:
 *   -K <val>     System gain from PTC fit [e-/ADU]  (required)
 *   -n <steps>   Number of sweep steps (default: 30)
 *   -d <frames>  Number of dark frames (default: 10)
 *   -t <us>      Minimum exposure [us] (default: 10.0)
 *   -T <us>      Maximum exposure [us] (default: 20000.0)
 *   -h           Show this help
 *
 * The program interactively prompts for:
 *   P_sensor  — power measured by power sensor [W]
 *   A_sensor  — active area of power sensor [m^2]
 *
 * Both values are saved in metadata.txt alongside the frames.
 *
 * Output directory: data/qe_YYYYMMDD_HHMMSS/
 *   dark_NN.bin     — dark frames
 *   flat_NN.bin     — single frames (no pairs needed for QE sweep)
 *   metadata.txt    — all parameters including power sensor values
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "CameraApi.h"
#include "camera_utils.h"
#include "file_utils.h"
#include "camera_config.h"

/* ------------------------------------------------------------------ */

static void print_usage(const char *prog)
{
    printf("Usage: %s -K <gain> [options]\n\n", prog);
    printf("  -K <val>     System gain from PTC [e-/ADU]  (required)\n");
    printf("  -n <steps>   Sweep steps     (default: %d)\n",
           DEFAULT_N_STEPS_QE);
    printf("  -d <frames>  Dark frames     (default: %d)\n",
           DEFAULT_N_DARK);
    printf("  -t <us>      Min exposure    (default: %.1f)\n",
           DEFAULT_T_MIN_US);
    printf("  -T <us>      Max exposure    (default: %.1f)\n",
           DEFAULT_T_MAX_US);
    printf("  -h           Show this help\n\n");
}

/* ------------------------------------------------------------------ */

static void wait_for_enter(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* ------------------------------------------------------------------ */

/*
 * prompt_double()
 * Interactively prompt the user for a positive double value.
 * Repeats until a valid positive number is entered.
 */
static double prompt_double(const char *prompt, const char *unit)
{
    double val = -1.0;
    while (val <= 0.0) {
        printf("  %s [%s]: ", prompt, unit);
        fflush(stdout);
        if (scanf("%lf", &val) != 1 || val <= 0.0) {
            printf("  Please enter a positive number.\n");
            val = -1.0;
            /* Clear input buffer */
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
        }
    }
    /* Consume trailing newline */
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    return val;
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    /* --- Parse arguments --- */
    int    n_steps  = DEFAULT_N_STEPS_QE;
    int    n_dark   = DEFAULT_N_DARK;
    double t_min_us = DEFAULT_T_MIN_US;
    double t_max_us = DEFAULT_T_MAX_US;
    double K_gain   = -1.0;   /* must be provided by user */

    int opt;
    while ((opt = getopt(argc, argv, "K:n:d:t:T:h")) != -1) {
        switch (opt) {
        case 'K': K_gain   = atof(optarg); break;
        case 'n': n_steps  = atoi(optarg); break;
        case 'd': n_dark   = atoi(optarg); break;
        case 't': t_min_us = atof(optarg); break;
        case 'T': t_max_us = atof(optarg); break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    if (K_gain <= 0.0) {
        fprintf(stderr,
                "Error: -K <gain> is required and must be positive.\n"
                "Run ptc_analysis.py first to obtain K.\n\n");
        print_usage(argv[0]);
        return 1;
    }

    printf("==============================================\n");
    printf(" HT-SUA33GM-T1V-C  —  QE Acquisition\n");
    printf("==============================================\n");
    printf(" K (from PTC) : %.3f e-/ADU\n", K_gain);
    printf(" Dark frames  : %d\n", n_dark);
    printf(" Sweep steps  : %d\n", n_steps);
    printf(" t_min        : %.1f us\n", t_min_us);
    printf(" t_max        : %.1f us\n", t_max_us);
    printf("==============================================\n\n");

    /* --- Power sensor measurement --- */
    printf("-----------------------------------------------\n");
    printf(" Power sensor measurement\n");
    printf(" Place the power sensor at the sensor plane\n");
    printf(" (camera removed). Read the values and enter below.\n");
    printf("-----------------------------------------------\n");

    double P_sensor = prompt_double("P_sensor (measured power)",
                                    "W, e.g. 1.23e-6");
    double A_sensor = prompt_double("A_sensor (active area of sensor)",
                                    "m^2, e.g. 7.07e-5");

    /* Pre-compute irradiance */
    double irradiance = P_sensor / A_sensor;   /* W/m^2 */
    double photon_energy = (H_JS * C_MS) / LAMBDA_M;

    printf("\n  Irradiance I = %.4e W/m^2\n", irradiance);
    printf("  Photon energy h*nu = %.4e J  (lambda = %.0f nm)\n\n",
           photon_energy, LAMBDA_M * 1e9);

    /* --- Compute log-spaced exposures --- */
    double *exposures = malloc(n_steps * sizeof(double));
    if (!exposures) { fprintf(stderr, "Out of memory.\n"); return 1; }
    logspace(t_min_us, t_max_us, n_steps, exposures);

    /* --- Allocate frame buffer --- */
    uint8_t *buf = malloc(CAM_N_PIXELS);
    if (!buf) {
        fprintf(stderr, "Out of memory.\n");
        free(exposures);
        return 1;
    }

    /* --- Initialise camera --- */
    printf("Reinstall the camera, then press Enter.\n");
    wait_for_enter();

    CameraHandle hCam = cam_init();
    cam_configure(hCam);

    /* --- Create output directory and metadata --- */
    char out_dir[512];
    make_output_dir(QE_DIR_PREFIX, out_dir, sizeof(out_dir));

    FILE *meta = open_metadata(out_dir);
    meta_writef(meta, "acquisition_type",  "%s",    "qe");
    meta_writef(meta, "K_gain_e_per_ADU",  "%.4f",  K_gain);
    meta_writef(meta, "P_sensor_W",        "%.6e",  P_sensor);
    meta_writef(meta, "A_sensor_m2",       "%.6e",  A_sensor);
    meta_writef(meta, "irradiance_W_m2",   "%.6e",  irradiance);
    meta_writef(meta, "lambda_m",          "%.4e",  LAMBDA_M);
    meta_writef(meta, "photon_energy_J",   "%.6e",  photon_energy);
    meta_writef(meta, "n_dark",            "%d",    n_dark);
    meta_writef(meta, "n_steps",           "%d",    n_steps);
    meta_writef(meta, "t_min_us",          "%.2f",  t_min_us);
    meta_writef(meta, "t_max_us",          "%.2f",  t_max_us);

    fprintf(meta, "%-30s = ", "exposures_us");
    for (int k = 0; k < n_steps; k++) {
        fprintf(meta, "%.2f%s", exposures[k],
                k < n_steps - 1 ? "," : "\n");
    }

    /* --------------------------------------------------------------- *
     * Stage 1: Dark frames
     * --------------------------------------------------------------- */
    printf("\n-----------------------------------------------\n");
    printf(" Stage 1: Dark frames (%d)\n", n_dark);
    printf(" Block the beam, then press Enter.\n");
    printf("-----------------------------------------------\n");
    wait_for_enter();

    cam_set_exposure(hCam, t_min_us);

    printf("  %-6s  %-12s\n", "Frame", "Mean (ADU)");
    printf("  %-6s  %-12s\n", "-----", "----------");

    for (int i = 0; i < n_dark; i++) {
        if (cam_capture_frame(hCam, buf) != 0) {
            fprintf(stderr, "  [ERROR] Dark frame %d failed.\n", i);
            continue;
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/dark_%02d.bin", out_dir, i);
        save_frame(path, buf);

        double mean;
        frame_stats_roi(buf, &mean, NULL, NULL, NULL);
        printf("  %-6d  %-12.2f\n", i, mean);
    }

    /* --------------------------------------------------------------- *
     * Stage 2: Preview loop
     * --------------------------------------------------------------- */
    printf("\n-----------------------------------------------\n");
    printf(" Stage 2: Preview\n");
    printf(" Unblock the beam, then press Enter.\n");
    printf("-----------------------------------------------\n");
    wait_for_enter();

    cam_set_exposure(hCam, t_max_us);

    while (1) {
        cam_capture_frame(hCam, buf);   /* discard one to settle */
        if (cam_capture_frame(hCam, buf) != 0) {
            fprintf(stderr, "  Preview capture failed. Try again.\n");
            wait_for_enter();
            continue;
        }

        double mean;
        frame_stats_roi(buf, &mean, NULL, NULL, NULL);
        printf("\n  Preview at t = %.0f us:  mean = %.1f ADU\n",
               t_max_us, mean);

        if (mean < PREVIEW_WARN_LOW)
            printf("  [LOW]  Add less ND attenuation.\n");
        else if (mean > PREVIEW_WARN_HIGH)
            printf("  [HIGH] Add more ND attenuation.\n");
        else
            printf("  [OK]   Signal in target range.\n");

        printf("  Press Enter to re-check, or type 'y' to proceed: ");
        fflush(stdout);
        char line[16];
        if (fgets(line, sizeof(line), stdin) &&
            (line[0] == 'y' || line[0] == 'Y'))
            break;
    }

    /* --------------------------------------------------------------- *
     * Stage 3: QE exposure sweep
     * --------------------------------------------------------------- */
    printf("\n-----------------------------------------------\n");
    printf(" Stage 3: QE sweep (%d steps)\n", n_steps);
    printf("-----------------------------------------------\n");
    printf("  %-8s  %-12s  %-12s  %-10s\n",
           "Step", "Exp (us)", "Mean (ADU)", "QE_eff (%%)");
    printf("  %-8s  %-12s  %-12s  %-10s\n",
           "----", "--------", "----------", "---------");

    for (int k = 0; k < n_steps; k++) {
        double t_s = exposures[k] * 1e-6;   /* convert us to s */
        cam_set_exposure(hCam, exposures[k]);

        /* Discard one frame for exposure to settle */
        cam_capture_frame(hCam, buf);

        if (cam_capture_frame(hCam, buf) != 0) {
            fprintf(stderr, "  [ERROR] Step %02d capture failed.\n", k);
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/flat_%02d.bin", out_dir, k);
        save_frame(path, buf);

        double mean;
        frame_stats_roi(buf, &mean, NULL, NULL, NULL);

        /*
         * Live QE estimate from Eq. (qe_measured) in the theory doc:
         *
         *   QE_eff = (mu_bar * A_sensor * K * h * c)
         *            / (P_sensor * lambda * A_px * t)
         *
         * Equivalently using irradiance I = P_sensor / A_sensor:
         *
         *   QE_eff = (mu_bar * K * h * c)
         *            / (I * lambda * A_px * t)
         */
        double qe = -1.0;
        if (mean > 5.0 && t_s > 0.0) {
            double numerator   = mean * K_gain * H_JS * C_MS;
            double denominator = irradiance * LAMBDA_M
                                 * PIXEL_AREA_M2 * t_s;
            qe = numerator / denominator;
        }

        /* Print one line — QE shown as percentage, or -- if invalid */
        if (qe > 0.0 && qe < 1.5) {
            printf("  %02d/%-5d  %-12.1f  %-12.1f  %-10.1f\n",
                   k + 1, n_steps, exposures[k], mean, qe * 100.0);
        } else {
            printf("  %02d/%-5d  %-12.1f  %-12.1f  %-10s\n",
                   k + 1, n_steps, exposures[k], mean,
                   mean <= 5.0 ? "(dark)" : "(sat)");
        }
    }

    /* --- Finalise --- */
    fclose(meta);
    free(buf);
    free(exposures);
    cam_teardown(hCam);

    printf("\n==============================================\n");
    printf(" QE acquisition complete.\n");
    printf(" Data written to: %s\n", out_dir);
    printf(" Run analysis/ptc_analysis.py for final QE value.\n");
    printf("==============================================\n\n");

    return 0;
}

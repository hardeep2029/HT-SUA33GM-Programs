/*
 * camera_utils.c
 * Camera initialisation, configuration, and frame capture.
 *
 * Camera:  HT-SUA33GM-T1V-C (HuaTeng Vision)
 * SDK:     linuxSDK_V2.1.0.49
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "CameraApi.h"
#include "camera_utils.h"
#include "camera_config.h"

/* ------------------------------------------------------------------ */

CameraHandle cam_init(void)
{
    tSdkCameraDevInfo dev_list[8];
    int n_cams = 8;
    CameraHandle hCam;
    CameraSdkStatus status;

    /* Enumerate connected cameras */
    status = CameraEnumerateDevice(dev_list, &n_cams);
    if (status != CAMERA_STATUS_SUCCESS) {
        fprintf(stderr, "[cam_init] CameraEnumerateDevice failed: %d\n",
                status);
        exit(1);
    }
    if (n_cams == 0) {
        fprintf(stderr, "[cam_init] No cameras found. "
                "Check USB connection and power.\n");
        exit(1);
    }
    if (n_cams > 1) {
        fprintf(stderr, "[cam_init] %d cameras found. "
                "This suite expects exactly one camera.\n", n_cams);
        exit(1);
    }

    /* Initialise the single found camera */
    status = CameraInit(&dev_list[0], -1, -1, &hCam);
    if (status != CAMERA_STATUS_SUCCESS) {
        fprintf(stderr, "[cam_init] CameraInit failed: %d\n", status);
        exit(1);
    }

    printf("[camera] Found:  %s  (SN: %s)\n",
           dev_list[0].acFriendlyName,
           dev_list[0].acSn);

    return hCam;
}

/* ------------------------------------------------------------------ */

/*
 * Helper macro: call an SDK function, print confirmation on success,
 * or print an error and exit on failure.
 */
#define SDK_CALL(fn, args, label)                                       \
    do {                                                                \
        CameraSdkStatus _s = fn args;                                   \
        if (_s != CAMERA_STATUS_SUCCESS) {                              \
            fprintf(stderr, "[cam_configure] " label                    \
                    " failed: %d\n", _s);                               \
            exit(1);                                                    \
        }                                                               \
        printf("[config]  %-40s OK\n", label);                         \
    } while (0)

void cam_configure(CameraHandle hCam)
{
    printf("\n[config] Applying ISP settings...\n");

    /* Manual exposure mode — must be set before exposure time */
    SDK_CALL(CameraSetAeState,
             (hCam, FALSE),
             "Manual exposure mode");

    /* Analogue gain: 1.0 = 0 dB */
    SDK_CALL(CameraSetAnalogGain,
             (hCam, (int)(CAM_ANALOG_GAIN)),
             "Analogue gain = 0 dB");

    /* Gamma = 1.0 (SDK unit 100) */
    SDK_CALL(CameraSetGamma,
             (hCam, CAM_GAMMA),
             "Gamma = 1.0");

    /* Contrast = neutral (SDK unit 100) */
    SDK_CALL(CameraSetContrast,
             (hCam, CAM_CONTRAST),
             "Contrast = neutral");

    /* Sharpness = 0 (disabled) */
    SDK_CALL(CameraSetSharpness,
             (hCam, CAM_SHARPNESS),
             "Sharpness = disabled");

    /* 8-bit raw output */
    SDK_CALL(CameraSetMediaType,
             (hCam, CAM_BIT_DEPTH),
             "Bit depth = 8-bit raw");

    /* Output format: 8-bit monochrome */
    SDK_CALL(CameraSetIspOutFormat,
             (hCam, CAMERA_MEDIA_TYPE_MONO8),
             "Output format = MONO8");

    /* Continuous (free-run) trigger mode */
    SDK_CALL(CameraSetTriggerMode,
             (hCam, 0),
             "Trigger mode = continuous");

    /* Start acquisition */
    SDK_CALL(CameraPlay,
             (hCam),
             "Acquisition started");

    printf("[config] All settings applied.\n\n");
}

/* ------------------------------------------------------------------ */

void cam_set_exposure(CameraHandle hCam, double t_us)
{
    CameraSdkStatus s = CameraSetExposureTime(hCam, t_us);
    if (s != CAMERA_STATUS_SUCCESS) {
        fprintf(stderr, "[cam_set_exposure] Failed to set %.1f us: %d\n",
                t_us, s);
    }
}

/* ------------------------------------------------------------------ */

int cam_capture_frame(CameraHandle hCam, uint8_t *buf)
{
    tSdkFrameHead frame_info;
    BYTE *p_raw = NULL;
    CameraSdkStatus status;

    /* Grab raw frame from camera buffer */
    status = CameraGetImageBuffer(hCam, &frame_info, &p_raw,
                                  CAM_CAPTURE_TIMEOUT_MS);
    if (status != CAMERA_STATUS_SUCCESS) {
        fprintf(stderr, "[cam_capture_frame] Timeout or error: %d\n",
                status);
        return -1;
    }

    /* Process raw frame into 8-bit mono output buffer */
    status = CameraImageProcess(hCam, p_raw, buf, &frame_info);
    if (status != CAMERA_STATUS_SUCCESS) {
        fprintf(stderr, "[cam_capture_frame] ImageProcess failed: %d\n",
                status);
        CameraReleaseImageBuffer(hCam, p_raw);
        return -1;
    }

    /* Release raw buffer back to SDK */
    CameraReleaseImageBuffer(hCam, p_raw);
    return 0;
}

/* ------------------------------------------------------------------ */

void cam_teardown(CameraHandle hCam)
{
    CameraStop(hCam);
    CameraUnInit(hCam);
    printf("[camera] Camera released.\n");
}

/* ------------------------------------------------------------------ */

void frame_stats_roi(const uint8_t *buf,
                     double *mean,
                     double *std_dev,
                     double *min_val,
                     double *max_val)
{
    double sum  = 0.0;
    double sum2 = 0.0;
    double mn   = 255.0;
    double mx   = 0.0;

    for (int row = ROI_Y0; row < ROI_Y0 + ROI_H; row++) {
        for (int col = ROI_X0; col < ROI_X0 + ROI_W; col++) {
            double v = (double)buf[row * CAM_WIDTH + col];
            sum  += v;
            sum2 += v * v;
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
    }

    double m = sum / ROI_N;
    double variance = (sum2 / ROI_N) - (m * m);

    if (mean)    *mean    = m;
    if (std_dev) *std_dev = sqrt(variance > 0.0 ? variance : 0.0);
    if (min_val) *min_val = mn;
    if (max_val) *max_val = mx;
}

/* ------------------------------------------------------------------ */

void logspace(double t_min, double t_max, int n, double *t)
{
    double log_min = log10(t_min);
    double log_max = log10(t_max);
    for (int i = 0; i < n; i++) {
        double frac = (n == 1) ? 0.0 : (double)i / (double)(n - 1);
        t[i] = pow(10.0, log_min + frac * (log_max - log_min));
    }
}

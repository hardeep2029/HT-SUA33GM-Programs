#ifndef CAMERA_UTILS_H
#define CAMERA_UTILS_H

#include <stdint.h>
#include "CameraApi.h"
#include "camera_config.h"

/* ============================================================
 * camera_utils.h
 * Camera initialisation, configuration, and frame capture.
 * ============================================================ */

/*
 * cam_init()
 * Enumerate USB cameras, verify exactly one is connected,
 * initialise it, and return the handle.
 * Prints the camera serial number and model on success.
 * Calls exit(1) on any failure — there is no point continuing
 * if the camera cannot be found or opened.
 */
CameraHandle cam_init(void);

/*
 * cam_configure()
 * Apply all ISP-disable settings from camera_config.h:
 *   - manual exposure mode
 *   - analogue gain = CAM_ANALOG_GAIN (0 dB)
 *   - gamma        = CAM_GAMMA        (1.0)
 *   - contrast     = CAM_CONTRAST     (neutral)
 *   - sharpness    = CAM_SHARPNESS    (disabled)
 *   - bit depth    = CAM_BIT_DEPTH    (8-bit raw)
 *   - output format = MONO8
 *   - trigger mode = continuous (software)
 * Prints one confirmation line per setting.
 * Calls exit(1) if any SDK call fails.
 */
void cam_configure(CameraHandle hCam);

/*
 * cam_set_exposure()
 * Set the exposure time in microseconds.
 * The SDK takes double; the camera clamps to its valid range.
 */
void cam_set_exposure(CameraHandle hCam, double t_us);

/*
 * cam_capture_frame()
 * Capture one processed 8-bit monochrome frame into buf.
 * buf must be at least CAM_N_PIXELS bytes (allocated by caller).
 * Waits up to CAM_CAPTURE_TIMEOUT_MS milliseconds.
 * Returns  0 on success.
 * Returns -1 on timeout or SDK error, prints a warning.
 */
int cam_capture_frame(CameraHandle hCam, uint8_t *buf);

/*
 * cam_teardown()
 * Stop acquisition, uninitialise the camera, and release handle.
 * Safe to call even if cam_init() was not fully completed.
 */
void cam_teardown(CameraHandle hCam);

/*
 * frame_stats_roi()
 * Compute statistics over the central ROI defined in camera_config.h.
 * All output pointers are optional — pass NULL to skip.
 *
 *   buf       : pointer to CAM_N_PIXELS bytes (full frame, row-major)
 *   mean      : mean pixel value over ROI [ADU]
 *   std_dev   : standard deviation over ROI [ADU]
 *   min_val   : minimum pixel value in ROI [ADU]
 *   max_val   : maximum pixel value in ROI [ADU]
 */
void frame_stats_roi(const uint8_t *buf,
                     double *mean,
                     double *std_dev,
                     double *min_val,
                     double *max_val);

/*
 * logspace()
 * Fill array t with n values logarithmically spaced
 * between t_min and t_max (inclusive).
 * Caller must allocate t[n].
 */
void logspace(double t_min, double t_max, int n, double *t);

#endif /* CAMERA_UTILS_H */

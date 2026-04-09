#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

/* ============================================================
 * camera_config.h
 * All tunable parameters for the HT-SUA33GM-T1V-C calibration
 * suite. This is the only file you should need to edit for
 * routine operation.
 *
 * Camera:  HT-SUA33GM-T1V-C (HuaTeng Vision)
 * Sensor:  SmartSens CMOS, Global Shutter, 1/5.6"
 * Project: Atom Interferometry with Strontium
 *          Northwestern University — Kovachy Group
 * ============================================================ */

/* --- Sensor geometry --- */
#define CAM_WIDTH           640
#define CAM_HEIGHT          480
#define CAM_N_PIXELS        (CAM_WIDTH * CAM_HEIGHT)

/* --- ISP settings (all set to neutral/disabled) ---
 * These values are passed directly to the HuaTeng SDK.
 * Do not change unless you understand the SDK documentation. */
#define CAM_ANALOG_GAIN     1.0     /* 1.0 = 0 dB, minimum gain      */
#define CAM_GAMMA           100     /* SDK units: 100 = gamma 1.0     */
#define CAM_CONTRAST        100     /* SDK units: 100 = neutral       */
#define CAM_SHARPNESS       0       /* 0 = disabled                   */
#define CAM_BIT_DEPTH       0       /* 0 = 8-bit raw output           */

/* --- Acquisition defaults (all overridable via command line) --- */
#define DEFAULT_N_DARK      10      /* number of dark frames          */
#define DEFAULT_N_STEPS     20      /* number of flat-pair steps      */
#define DEFAULT_N_STEPS_QE  30      /* finer sweep for QE linearity   */
#define DEFAULT_T_MIN_US    10.0    /* minimum exposure [us] = 0.01ms */
#define DEFAULT_T_MAX_US    20000.0 /* maximum exposure [us] = 20ms   */

/* --- Frame capture timeout --- */
#define CAM_CAPTURE_TIMEOUT_MS  2000

/* --- Preview thresholds (ADU) ---
 * Used to guide ND filter selection during the preview loop. */
#define PREVIEW_WARN_LOW    30      /* signal probably too faint      */
#define PREVIEW_TARGET_LOW  220     /* ideal range lower bound        */
#define PREVIEW_TARGET_HIGH 252     /* ideal range upper bound        */
#define PREVIEW_WARN_HIGH   254     /* already saturating             */

/* --- Central ROI for statistics ---
 * A 200x200 pixel region centred on the sensor.
 * Used for all mean/variance calculations.             */
#define ROI_X0   220    /* left edge:   (640 - 200) / 2              */
#define ROI_Y0   140    /* top edge:    (480 - 200) / 2              */
#define ROI_W    200
#define ROI_H    200
#define ROI_N    (ROI_W * ROI_H)   /* 40000 pixels                   */

/* --- Physical constants --- */
#define PIXEL_AREA_M2   (16e-12)    /* (4 um)^2 in m^2               */
#define LAMBDA_M        (688e-9)    /* calibration laser [m]          */
#define H_JS            (6.62607e-34)
#define C_MS            (2.99792e8)

/* --- Output directory prefix --- */
#define PTC_DIR_PREFIX  "data/ptc_"
#define QE_DIR_PREFIX   "data/qe_"

#endif /* CAMERA_CONFIG_H */

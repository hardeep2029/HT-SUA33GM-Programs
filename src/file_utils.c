/*
 * file_utils.c
 * Output directory creation, raw frame saving, metadata writing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#include "file_utils.h"
#include "camera_config.h"

/* ------------------------------------------------------------------ */

/*
 * mkdir_p()
 * Create directory at path (and any missing parents).
 * Equivalent to "mkdir -p path".
 * Returns 0 on success, -1 on error.
 */
static int mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);

    /* Strip trailing slash */
    if (len > 0 && tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    /* Walk the path and create each component */
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

/* ------------------------------------------------------------------ */

void make_output_dir(const char *prefix, char *out_dir, size_t len)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", tm_info);

    snprintf(out_dir, len, "%s%s", prefix, timestamp);

    if (mkdir_p(out_dir) != 0) {
        fprintf(stderr, "[make_output_dir] Cannot create '%s': %s\n",
                out_dir, strerror(errno));
        exit(1);
    }

    printf("[output] Directory: %s\n", out_dir);
}

/* ------------------------------------------------------------------ */

int save_frame(const char *path, const uint8_t *buf)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[save_frame] Cannot open '%s': %s\n",
                path, strerror(errno));
        return -1;
    }

    size_t written = fwrite(buf, 1, CAM_N_PIXELS, f);
    fclose(f);

    if (written != (size_t)CAM_N_PIXELS) {
        fprintf(stderr, "[save_frame] Short write to '%s' "
                "(%zu of %d bytes)\n", path, written, CAM_N_PIXELS);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */

FILE *open_metadata(const char *out_dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/metadata.txt", out_dir);

    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "[open_metadata] Cannot open '%s': %s\n",
                path, strerror(errno));
        exit(1);
    }

    /* Standard header */
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp),
             "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(f, "# HT-SUA33GM-T1V-C Calibration Metadata\n");
    fprintf(f, "# Generated: %s\n", timestamp);
    fprintf(f, "# Format: key = value\n");
    fprintf(f, "#\n");

    meta_writef(f, "timestamp",       "%s",  timestamp);
    meta_writef(f, "camera_model",    "%s",  "HT-SUA33GM-T1V-C");
    meta_writef(f, "sensor",          "%s",  "SmartSens CMOS Global Shutter");
    meta_writef(f, "width_px",        "%d",  CAM_WIDTH);
    meta_writef(f, "height_px",       "%d",  CAM_HEIGHT);
    meta_writef(f, "pixel_size_um",   "%s",  "4.0");
    meta_writef(f, "bit_depth",       "%s",  "8");
    meta_writef(f, "analog_gain_dB",  "%s",  "0");
    meta_writef(f, "gamma",           "%s",  "1.0");
    meta_writef(f, "roi_x0",          "%d",  ROI_X0);
    meta_writef(f, "roi_y0",          "%d",  ROI_Y0);
    meta_writef(f, "roi_w",           "%d",  ROI_W);
    meta_writef(f, "roi_h",           "%d",  ROI_H);
    meta_writef(f, "lambda_nm",       "%s",  "688");

    return f;
}

/* ------------------------------------------------------------------ */

void meta_write(FILE *f, const char *key, const char *value)
{
    fprintf(f, "%-30s = %s\n", key, value);
}

/* ------------------------------------------------------------------ */

void meta_writef(FILE *f, const char *key, const char *fmt, ...)
{
    char value[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(value, sizeof(value), fmt, args);
    va_end(args);
    meta_write(f, key, value);
}

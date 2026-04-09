#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <stdio.h>
#include <stdint.h>
#include "camera_config.h"

/* ============================================================
 * file_utils.h
 * Output directory creation, raw frame saving, metadata writing.
 * ============================================================ */

/*
 * make_output_dir()
 * Create a timestamped output directory of the form:
 *   prefix + "YYYYMMDD_HHMMSS"
 * e.g. "data/ptc_20250409_143022"
 *
 * The parent directory ("data/") is created if it does not exist.
 * The full path is written into out_dir (caller supplies buffer of
 * length len).
 * Calls exit(1) if the directory cannot be created.
 */
void make_output_dir(const char *prefix, char *out_dir, size_t len);

/*
 * save_frame()
 * Write CAM_N_PIXELS bytes from buf to the file at path.
 * Returns  0 on success.
 * Returns -1 on failure, prints an error message.
 */
int save_frame(const char *path, const uint8_t *buf);

/*
 * open_metadata()
 * Open (create) metadata.txt inside out_dir for writing.
 * Writes a standard header with timestamp and camera dimensions.
 * Returns the FILE* — caller writes additional key=value lines
 * then calls fclose().
 * Calls exit(1) if the file cannot be opened.
 */
FILE *open_metadata(const char *out_dir);

/*
 * meta_write()
 * Write a single "key = value\n" line to an open metadata FILE*.
 */
void meta_write(FILE *f, const char *key, const char *value);

/*
 * meta_writef()
 * Write a formatted value line: "key = <printf-style format>\n"
 * Convenience wrapper around meta_write for numeric values.
 */
void meta_writef(FILE *f, const char *key, const char *fmt, ...);

#endif /* FILE_UTILS_H */

#ifndef PICOPOP_PNG_WRITE_H
#define PICOPOP_PNG_WRITE_H

#include <stdlib.h>

/*
 * Write an 8bpp indexed image as a PNG. `pitch` is the source row stride in
 * bytes, which is not necessarily `w`. Returns 0 on success.
 */
int png_write_indexed(const char *path, const unsigned char *pixels,
                      int w, int h, int pitch,
                      const unsigned char palette[256][3]);

#endif /* PICOPOP_PNG_WRITE_H */

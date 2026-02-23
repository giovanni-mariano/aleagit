#ifndef ALEAGIT_BMP_WRITER_H
#define ALEAGIT_BMP_WRITER_H

#include <stdint.h>

/* Write RGB pixel data to a BMP file.
   pixels: width*height*3 bytes in RGB order (top-to-bottom).
   Returns 0 on success. */
int ag_write_bmp(const char* filename, const uint8_t* pixels,
                 int width, int height);

#endif /* ALEAGIT_BMP_WRITER_H */

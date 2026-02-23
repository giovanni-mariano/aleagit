// SPDX-FileCopyrightText: 2026 Giovanni MARIANO
//
// SPDX-License-Identifier: MPL-2.0

#include "bmp_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ag_write_bmp(const char* filename, const uint8_t* pixels,
                 int width, int height) {
    FILE* f = fopen(filename, "wb");
    if (!f) return -1;

    int row_size = ((width * 3 + 3) / 4) * 4;
    int data_size = row_size * height;
    int file_size = 54 + data_size;

    uint8_t header[54];
    memset(header, 0, 54);

    header[0] = 'B'; header[1] = 'M';
    header[2]  = file_size        & 0xFF;
    header[3]  = (file_size >> 8) & 0xFF;
    header[4]  = (file_size >> 16) & 0xFF;
    header[5]  = (file_size >> 24) & 0xFF;
    header[10] = 54;
    header[14] = 40;
    header[18] = width         & 0xFF;
    header[19] = (width >> 8)  & 0xFF;
    header[20] = (width >> 16) & 0xFF;
    header[21] = (width >> 24) & 0xFF;
    header[22] = height         & 0xFF;
    header[23] = (height >> 8)  & 0xFF;
    header[24] = (height >> 16) & 0xFF;
    header[25] = (height >> 24) & 0xFF;
    header[26] = 1;
    header[28] = 24;

    fwrite(header, 1, 54, f);

    uint8_t* row = malloc(row_size);
    for (int y = height - 1; y >= 0; y--) {
        memset(row, 0, row_size);
        for (int x = 0; x < width; x++) {
            int src = (y * width + x) * 3;
            int dst = x * 3;
            row[dst + 0] = pixels[src + 2]; /* B */
            row[dst + 1] = pixels[src + 1]; /* G */
            row[dst + 2] = pixels[src + 0]; /* R */
        }
        fwrite(row, 1, row_size, f);
    }

    free(row);
    fclose(f);
    return 0;
}

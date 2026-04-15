#ifndef PICMERGE_C99_IMAGE_IO_H
#define PICMERGE_C99_IMAGE_IO_H

#include <stdint.h>

enum { PICMERGE_CHANNELS = 3 };

typedef struct PicmergeImage {
    uint8_t* data;
    int width;
    int height;
} PicmergeImage;

int picmerge_probe_image(const char* path, int* width, int* height);
void picmerge_image_init(PicmergeImage* image);
void picmerge_image_reset(PicmergeImage* image);
int picmerge_image_load(PicmergeImage* image, const char* path);
const uint8_t* picmerge_image_row_const(const PicmergeImage* image, int y);
int picmerge_write_jpeg(const char* path, int width, int height, const uint8_t* rgb, int quality);

#endif

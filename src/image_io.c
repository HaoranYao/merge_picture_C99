#include "image_io.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stddef.h>

int picmerge_probe_image(const char* path, int* width, int* height) {
    int channels = 0;
    return stbi_info(path, width, height, &channels) != 0;
}

void picmerge_image_init(PicmergeImage* image) {
    if (!image) return;
    image->data = NULL;
    image->width = 0;
    image->height = 0;
}

void picmerge_image_reset(PicmergeImage* image) {
    if (!image) return;
    if (image->data) {
        stbi_image_free(image->data);
        image->data = NULL;
    }
    image->width = 0;
    image->height = 0;
}

int picmerge_image_load(PicmergeImage* image, const char* path) {
    int channels_in_file = 0;
    if (!image) return 0;
    picmerge_image_reset(image);
    image->data = stbi_load(path, &image->width, &image->height, &channels_in_file, PICMERGE_CHANNELS);
    if (!image->data) {
        image->width = 0;
        image->height = 0;
        return 0;
    }
    return 1;
}

const uint8_t* picmerge_image_row_const(const PicmergeImage* image, int y) {
    return image->data + (size_t)y * (size_t)image->width * PICMERGE_CHANNELS;
}

int picmerge_write_jpeg(const char* path, int width, int height, const uint8_t* rgb, int quality) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    return stbi_write_jpg(path, width, height, PICMERGE_CHANNELS, rgb, quality) != 0;
}

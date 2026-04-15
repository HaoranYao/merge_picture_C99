#include "row_signature.h"

#include <stddef.h>
#include <stdlib.h>

void picmerge_row_signatures_init(PicmergeRowSignatures* sigs) {
    if (!sigs) return;
    sigs->fp = NULL;
    sigs->height = 0;
}

void picmerge_row_signatures_reset(PicmergeRowSignatures* sigs) {
    if (!sigs) return;
    free(sigs->fp);
    sigs->fp = NULL;
    sigs->height = 0;
}

const uint8_t* picmerge_row_signature_row(const PicmergeRowSignatures* sigs, int y) {
    return sigs->fp + (size_t)y * PICMERGE_SIG_BINS;
}

int picmerge_compute_row_signatures(const PicmergeImage* image, PicmergeRowSignatures* out) {
    int y;
    int b;
    int height;
    int width;
    int x_begin;
    int x_end;
    int slice_pixels;
    int bin_pixels;
    int bin_bytes;
    size_t row_stride;
    size_t col_offset;
    const uint8_t* base;

    if (!image || !out) return 0;
    picmerge_row_signatures_reset(out);

    height = image->height;
    width = image->width;
    out->height = height;
    if (height <= 0 || width <= 0 || !image->data) return 1;

    x_begin = width / 4;
    x_end = width - width / 4;
    slice_pixels = x_end - x_begin;
    if (slice_pixels < PICMERGE_SIG_BINS) return 1;

    bin_pixels = slice_pixels / PICMERGE_SIG_BINS;
    bin_bytes = bin_pixels * PICMERGE_CHANNELS;
    row_stride = (size_t)width * PICMERGE_CHANNELS;
    col_offset = (size_t)x_begin * PICMERGE_CHANNELS;

    out->fp = (uint8_t*)calloc((size_t)height * PICMERGE_SIG_BINS, sizeof(uint8_t));
    if (!out->fp) {
        out->height = 0;
        return 0;
    }

    base = image->data;
    for (y = 0; y < height; ++y) {
        const uint8_t* rowp = base + (size_t)y * row_stride + col_offset;
        uint8_t* fp = out->fp + (size_t)y * PICMERGE_SIG_BINS;
        for (b = 0; b < PICMERGE_SIG_BINS; ++b) {
            const uint8_t* p = rowp + (size_t)b * bin_bytes;
            unsigned sum = 0;
            int k;
            for (k = 0; k < bin_bytes; ++k) sum += p[k];
            fp[b] = (uint8_t)(sum / (unsigned)bin_bytes);
        }
    }
    return 1;
}

int picmerge_rows_match(const uint8_t* a, const uint8_t* b, int tol) {
    int k;
    for (k = 0; k < PICMERGE_SIG_BINS; ++k) {
        int d = (int)a[k] - (int)b[k];
        if (d < 0) d = -d;
        if (d > tol) return 0;
    }
    return 1;
}

int picmerge_row_l1(const uint8_t* a, const uint8_t* b) {
    int k;
    int s = 0;
    for (k = 0; k < PICMERGE_SIG_BINS; ++k) {
        int d = (int)a[k] - (int)b[k];
        s += (d < 0) ? -d : d;
    }
    return s;
}

int picmerge_row_edge_l1(const uint8_t* a, const uint8_t* b) {
    int k;
    int s = 0;
    for (k = 0; k < 4; ++k) {
        int d = (int)a[k] - (int)b[k];
        s += (d < 0) ? -d : d;
    }
    for (k = PICMERGE_SIG_BINS - 4; k < PICMERGE_SIG_BINS; ++k) {
        int d = (int)a[k] - (int)b[k];
        s += (d < 0) ? -d : d;
    }
    return s;
}

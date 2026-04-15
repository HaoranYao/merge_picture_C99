#ifndef PICMERGE_C99_ROW_SIGNATURE_H
#define PICMERGE_C99_ROW_SIGNATURE_H

#include <stdint.h>

#include "image_io.h"

enum { PICMERGE_SIG_BINS = 16 };

typedef struct PicmergeRowSignatures {
    uint8_t* fp;
    int height;
} PicmergeRowSignatures;

void picmerge_row_signatures_init(PicmergeRowSignatures* sigs);
void picmerge_row_signatures_reset(PicmergeRowSignatures* sigs);
int picmerge_compute_row_signatures(const PicmergeImage* image, PicmergeRowSignatures* out);
const uint8_t* picmerge_row_signature_row(const PicmergeRowSignatures* sigs, int y);
int picmerge_rows_match(const uint8_t* a, const uint8_t* b, int tol);
int picmerge_row_l1(const uint8_t* a, const uint8_t* b);
int picmerge_row_edge_l1(const uint8_t* a, const uint8_t* b);

#endif

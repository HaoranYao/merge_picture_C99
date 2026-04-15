#ifndef PICMERGE_C99_BAR_DETECTOR_H
#define PICMERGE_C99_BAR_DETECTOR_H

#include "row_signature.h"

typedef struct PicmergeFixedBars {
    int top_height;
    int bottom_height;
    int top_ref;
    int bot_ref;
} PicmergeFixedBars;

PicmergeFixedBars picmerge_detect_fixed_bars(const PicmergeRowSignatures* sigs, int count, double max_fraction);

#endif

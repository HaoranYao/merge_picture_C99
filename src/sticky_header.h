#ifndef PICMERGE_C99_STICKY_HEADER_H
#define PICMERGE_C99_STICKY_HEADER_H

#include "row_signature.h"

int picmerge_detect_sticky_header(const PicmergeRowSignatures* prev,
                                  const PicmergeRowSignatures* next,
                                  int top_bar_height,
                                  int bottom_bar_height,
                                  int max_sticky);

#endif

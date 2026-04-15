#ifndef PICMERGE_C99_OVERLAP_FINDER_H
#define PICMERGE_C99_OVERLAP_FINDER_H

#include "row_signature.h"

typedef struct PicmergeOverlapResult {
    int ok;
    int offset_in_prev;
    int template_start_in_next;
    int template_length;
    int seam_in_prev;
    double best_cost;
    double second_best_cost;
} PicmergeOverlapResult;

void picmerge_overlap_result_init(PicmergeOverlapResult* result);
PicmergeOverlapResult picmerge_find_overlap(const PicmergeRowSignatures* prev,
                                            const PicmergeRowSignatures* next,
                                            int prev_search_begin,
                                            int prev_usable_end,
                                            int next_min_template_start,
                                            int next_usable_end);
void picmerge_refine_overlap_seam(const PicmergeRowSignatures* prev,
                                  const PicmergeRowSignatures* next,
                                  PicmergeOverlapResult* result,
                                  int usable_end);

#endif

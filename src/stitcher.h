#ifndef PICMERGE_C99_STITCHER_H
#define PICMERGE_C99_STITCHER_H

#include "overlap_finder.h"

typedef struct PicmergeContribution {
    int image_index;
    int y_begin;
    int y_end;
} PicmergeContribution;

typedef struct PicmergeStitchPlan {
    int width;
    int height;
    int top_bar;
    int bottom_bar;
    PicmergeContribution* parts;
    int count;
    int capacity;
} PicmergeStitchPlan;

void picmerge_stitch_plan_init(PicmergeStitchPlan* plan);
void picmerge_stitch_plan_reset(PicmergeStitchPlan* plan);
int picmerge_plan_push_span(PicmergeStitchPlan* plan, int image_index, int y_begin, int y_end);
int picmerge_plan_stitch(PicmergeStitchPlan* plan,
                         int width,
                         int image_height,
                         int num_images,
                         int top_bar,
                         int bottom_bar,
                         int bar_ref_image,
                         const int* self_sticky,
                         const int* fallback_skip,
                         const PicmergeOverlapResult* overlaps);
int picmerge_execute_stitch(const PicmergeStitchPlan* plan,
                            const char* const* input_paths,
                            int input_count,
                            const char* output_path,
                            int jpeg_quality);

#endif

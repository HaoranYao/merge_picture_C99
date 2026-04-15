#include "stitcher.h"

#include "image_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int picmerge_max_int(int a, int b) { return (a > b) ? a : b; }
static size_t picmerge_row_bytes(int width) { return (size_t)width * PICMERGE_CHANNELS; }

void picmerge_stitch_plan_init(PicmergeStitchPlan* plan) {
    if (!plan) return;
    plan->width = 0;
    plan->height = 0;
    plan->top_bar = 0;
    plan->bottom_bar = 0;
    plan->parts = NULL;
    plan->count = 0;
    plan->capacity = 0;
}

void picmerge_stitch_plan_reset(PicmergeStitchPlan* plan) {
    if (!plan) return;
    free(plan->parts);
    picmerge_stitch_plan_init(plan);
}

int picmerge_plan_push_span(PicmergeStitchPlan* plan, int image_index, int y_begin, int y_end) {
    PicmergeContribution* new_parts;
    if (!plan) return 0;
    if (y_end <= y_begin) return 1;
    if (plan->count > 0) {
        PicmergeContribution* last = &plan->parts[plan->count - 1];
        if (last->image_index == image_index && last->y_end == y_begin) {
            last->y_end = y_end;
            return 1;
        }
    }
    if (plan->count == plan->capacity) {
        int new_capacity = (plan->capacity > 0) ? plan->capacity * 2 : 16;
        new_parts = (PicmergeContribution*)realloc(plan->parts, (size_t)new_capacity * sizeof(PicmergeContribution));
        if (!new_parts) return 0;
        plan->parts = new_parts;
        plan->capacity = new_capacity;
    }
    plan->parts[plan->count].image_index = image_index;
    plan->parts[plan->count].y_begin = y_begin;
    plan->parts[plan->count].y_end = y_end;
    ++plan->count;
    return 1;
}

int picmerge_plan_stitch(PicmergeStitchPlan* plan,
                         int width,
                         int image_height,
                         int num_images,
                         int top_bar,
                         int bottom_bar,
                         int bar_ref_image,
                         const int* self_sticky,
                         const int* fallback_skip,
                         const PicmergeOverlapResult* overlaps) {
    int usable_end;
    int i;
    int total = 0;

    if (!plan) return 0;
    picmerge_stitch_plan_reset(plan);
    plan->width = width;
    plan->top_bar = top_bar;
    plan->bottom_bar = bottom_bar;

    if (num_images <= 0) return 1;
    usable_end = image_height - bottom_bar;

    if (top_bar > 0 && bar_ref_image != 0) {
        if (!picmerge_plan_push_span(plan, bar_ref_image, 0, top_bar)) return 0;
    }

    for (i = 0; i < num_images; ++i) {
        int content_begin;
        int content_end;

        if (i == 0) {
            content_begin = (top_bar > 0 && bar_ref_image != 0) ? top_bar : 0;
        } else {
            const PicmergeOverlapResult* prev_ov = &overlaps[i - 1];
            int prev_sticky = self_sticky[i - 1];
            int curr_sticky = self_sticky[i];

            if (top_bar > 0 && curr_sticky > prev_sticky && !prev_ov->ok) {
                if (!picmerge_plan_push_span(plan, i, top_bar + prev_sticky, top_bar + curr_sticky)) return 0;
            }

            if (prev_ov->ok) {
                int seam_offset = prev_ov->seam_in_prev - prev_ov->offset_in_prev;
                content_begin = prev_ov->template_start_in_next + seam_offset;
            } else if (i == num_images - 1) {
                content_begin = top_bar + curr_sticky;
            } else {
                int unified_skip = picmerge_max_int(fallback_skip[i - 1], fallback_skip[i]);
                content_begin = top_bar + unified_skip;
            }

            if (content_begin < top_bar + curr_sticky) content_begin = top_bar + curr_sticky;
            if (content_begin > usable_end) content_begin = usable_end;
        }

        if (i < num_images - 1 && overlaps[i].ok) {
            content_end = overlaps[i].seam_in_prev;
        } else if (i < num_images - 1) {
            if (i + 1 == num_images - 1) content_end = usable_end;
            else content_end = usable_end - fallback_skip[i + 1];
            if (content_end < content_begin) content_end = content_begin;
        } else {
            content_end = usable_end;
        }
        if (content_end < content_begin) content_end = content_begin;

        if (!picmerge_plan_push_span(plan, i, content_begin, content_end)) return 0;
    }

    if (!picmerge_plan_push_span(plan, bar_ref_image, usable_end, image_height)) return 0;
    for (i = 0; i < plan->count; ++i) total += plan->parts[i].y_end - plan->parts[i].y_begin;
    plan->height = total;
    return 1;
}

int picmerge_execute_stitch(const PicmergeStitchPlan* plan,
                            const char* const* input_paths,
                            int input_count,
                            const char* output_path,
                            int jpeg_quality) {
    size_t rb;
    size_t total_bytes;
    uint8_t* out;
    int i;

    if (!plan || !input_paths || !output_path) return 0;
    if (plan->width <= 0 || plan->height <= 0) {
        fprintf(stderr, "[error] invalid plan dimensions %dx%d\n", plan->width, plan->height);
        return 0;
    }

    rb = picmerge_row_bytes(plan->width);
    total_bytes = rb * (size_t)plan->height;
    out = (uint8_t*)malloc(total_bytes);
    if (!out) {
        fprintf(stderr, "[error] failed to allocate %zu bytes for output buffer\n", total_bytes);
        return 0;
    }

    for (i = 0; i < input_count; ++i) {
        int needed = 0;
        int out_y = 0;
        int p;
        PicmergeImage img;

        for (p = 0; p < plan->count; ++p) {
            if (plan->parts[p].image_index == i) {
                needed = 1;
                break;
            }
        }
        if (!needed) continue;

        picmerge_image_init(&img);
        if (!picmerge_image_load(&img, input_paths[i])) {
            fprintf(stderr, "[error] failed to decode %s\n", input_paths[i]);
            free(out);
            return 0;
        }
        if (img.width != plan->width) {
            fprintf(stderr, "[error] image %s width %d != plan width %d\n", input_paths[i], img.width, plan->width);
            picmerge_image_reset(&img);
            free(out);
            return 0;
        }

        for (p = 0; p < plan->count; ++p) {
            int span_rows = plan->parts[p].y_end - plan->parts[p].y_begin;
            if (plan->parts[p].image_index == i && span_rows > 0) {
                const uint8_t* src = picmerge_image_row_const(&img, plan->parts[p].y_begin);
                uint8_t* dst = out + (size_t)out_y * rb;
                memcpy(dst, src, rb * (size_t)span_rows);
            }
            out_y += span_rows;
        }

        picmerge_image_reset(&img);
    }

    if (!picmerge_write_jpeg(output_path, plan->width, plan->height, out, jpeg_quality)) {
        fprintf(stderr, "[error] failed to write %s\n", output_path);
        free(out);
        return 0;
    }

    free(out);
    return 1;
}

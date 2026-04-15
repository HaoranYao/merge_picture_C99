#include "overlap_finder.h"

#include <float.h>
#include <stdint.h>

static int picmerge_min_int(int a, int b) { return (a < b) ? a : b; }
static int picmerge_max_int(int a, int b) { return (a > b) ? a : b; }
static int picmerge_abs_int(int x) { return (x < 0) ? -x : x; }

void picmerge_overlap_result_init(PicmergeOverlapResult* result) {
    if (!result) return;
    result->ok = 0;
    result->offset_in_prev = 0;
    result->template_start_in_next = 0;
    result->template_length = 0;
    result->seam_in_prev = 0;
    result->best_cost = 0.0;
    result->second_best_cost = 0.0;
}

static PicmergeOverlapResult picmerge_match_at(const PicmergeRowSignatures* prev,
                                               const PicmergeRowSignatures* next,
                                               int prev_search_begin,
                                               int prev_usable_end,
                                               int next_template_start,
                                               int next_usable_end) {
    PicmergeOverlapResult r;
    int next_content_height;
    int L;
    int search_begin;
    int search_end;
    int64_t best_cost = INT64_MAX;
    int64_t second_cost = INT64_MAX;
    int best_d = -1;
    int d;

    picmerge_overlap_result_init(&r);
    r.template_start_in_next = next_template_start;
    next_content_height = next_usable_end - next_template_start;
    if (next_content_height <= 0) return r;

    L = picmerge_min_int(256, next_content_height / 3);
    if (L < 32) L = picmerge_min_int(next_content_height, 32);
    if (L <= 0) return r;
    r.template_length = L;

    search_begin = prev_search_begin;
    search_end = prev_usable_end - L;
    if (search_end < search_begin) return r;

    for (d = search_begin; d <= search_end; ++d) {
        int64_t cost = 0;
        int k;
        for (k = 0; k < L; ++k) {
            cost += picmerge_row_l1(picmerge_row_signature_row(prev, d + k),
                                    picmerge_row_signature_row(next, next_template_start + k));
        }
        if (cost < best_cost) {
            if (best_d < 0 || picmerge_abs_int(d - best_d) > L / 2) second_cost = best_cost;
            best_cost = cost;
            best_d = d;
        } else if (cost < second_cost && (best_d < 0 || picmerge_abs_int(d - best_d) > L / 2)) {
            second_cost = cost;
        }
    }

    r.best_cost = (double)best_cost;
    r.second_best_cost = (double)second_cost;
    if (best_d >= 0 && ((double)best_cost / (double)L) < 100.0) {
        r.ok = 1;
        r.offset_in_prev = best_d;
    }
    return r;
}

PicmergeOverlapResult picmerge_find_overlap(const PicmergeRowSignatures* prev,
                                            const PicmergeRowSignatures* next,
                                            int prev_search_begin,
                                            int prev_usable_end,
                                            int next_min_template_start,
                                            int next_usable_end) {
    PicmergeOverlapResult fallback;
    PicmergeOverlapResult best;
    int content_height;
    int approx_L;
    int step;
    int max_start;
    int ts;

    picmerge_overlap_result_init(&fallback);
    if (!prev || !next || prev->height <= 0 || next->height <= 0) return fallback;
    content_height = next_usable_end - next_min_template_start;
    if (content_height <= 0) return fallback;

    approx_L = picmerge_min_int(256, picmerge_max_int(32, content_height / 3));
    step = picmerge_max_int(approx_L, 128);
    max_start = next_usable_end - approx_L / 2;

    picmerge_overlap_result_init(&best);
    best.best_cost = DBL_MAX;

    for (ts = next_min_template_start; ts <= max_start; ts += step) {
        PicmergeOverlapResult r = picmerge_match_at(prev, next,
                                                    prev_search_begin, prev_usable_end,
                                                    ts, next_usable_end);
        if (r.ok) return r;
        if (r.template_length > 0) {
            double mean = r.best_cost / (double)r.template_length;
            double bmean = (best.template_length > 0) ? best.best_cost / (double)best.template_length : DBL_MAX;
            if (mean < bmean) best = r;
        }
    }
    return best;
}

void picmerge_refine_overlap_seam(const PicmergeRowSignatures* prev,
                                  const PicmergeRowSignatures* next,
                                  PicmergeOverlapResult* result,
                                  int usable_end) {
    int ov_begin;
    int ov_end;
    int ov_height;
    int dirty_in_bottom = 0;
    int check;
    int y;
    int clean_run = 0;
    const int dirty_l1 = 150;
    const int min_clean = 3;
    const int seam_margin = 10;

    if (!result) return;
    result->seam_in_prev = usable_end;
    if (!result->ok || !prev || !next) return;

    ov_begin = result->offset_in_prev;
    ov_end = usable_end;
    ov_height = ov_end - ov_begin;
    if (ov_height < 20) return;

    check = picmerge_min_int(8, ov_height);
    for (y = ov_end - 1; y >= ov_end - check; --y) {
        int ny = result->template_start_in_next + (y - ov_begin);
        if (picmerge_row_l1(picmerge_row_signature_row(prev, y),
                            picmerge_row_signature_row(next, ny)) > dirty_l1) {
            ++dirty_in_bottom;
        }
    }
    if (dirty_in_bottom < check / 2) return;

    for (y = ov_end - 1; y >= ov_begin; --y) {
        int ny = result->template_start_in_next + (y - ov_begin);
        int l1 = picmerge_row_l1(picmerge_row_signature_row(prev, y),
                                 picmerge_row_signature_row(next, ny));
        if (l1 <= dirty_l1) {
            ++clean_run;
            if (clean_run >= min_clean) {
                int seam = y + clean_run;
                result->seam_in_prev = (seam - seam_margin > ov_begin) ? (seam - seam_margin) : ov_begin;
                return;
            }
        } else {
            clean_run = 0;
        }
    }
}

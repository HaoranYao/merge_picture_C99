#include "bar_detector.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    PICMERGE_BAR_L1_THRESH = 300,
    PICMERGE_BAR_EDGE_L1_THRESH = 80
};

static int picmerge_row_is_bar(const PicmergeRowSignatures* sigs, int count, int y, int max_outliers, int allow_dynamic_center) {
    int ref;
    for (ref = 0; ref < count; ++ref) {
        int disagree = 0;
        const uint8_t* rrow = picmerge_row_signature_row(&sigs[ref], y);
        int i;
        for (i = 0; i < count; ++i) {
            int full_match;
            int edge_match;
            if (i == ref) continue;
            full_match = picmerge_row_l1(rrow, picmerge_row_signature_row(&sigs[i], y)) <= PICMERGE_BAR_L1_THRESH;
            edge_match = allow_dynamic_center &&
                picmerge_row_edge_l1(rrow, picmerge_row_signature_row(&sigs[i], y)) <= PICMERGE_BAR_EDGE_L1_THRESH;
            if (!full_match && !edge_match) {
                ++disagree;
                if (disagree > max_outliers) break;
            }
        }
        if (disagree <= max_outliers) return 1;
    }
    return 0;
}

static int picmerge_find_best_ref(const PicmergeRowSignatures* sigs, int count, int y_begin, int y_end) {
    int best_img = 0;
    int64_t best_total = INT64_MAX;
    int ref;
    if (count <= 1) return 0;
    for (ref = 0; ref < count; ++ref) {
        int64_t total = 0;
        int y;
        for (y = y_begin; y < y_end; ++y) {
            const uint8_t* rrow = picmerge_row_signature_row(&sigs[ref], y);
            int i;
            for (i = 0; i < count; ++i) {
                if (i == ref) continue;
                total += picmerge_row_l1(rrow, picmerge_row_signature_row(&sigs[i], y));
            }
        }
        if (total < best_total) {
            best_total = total;
            best_img = ref;
        }
    }
    return best_img;
}

PicmergeFixedBars picmerge_detect_fixed_bars(const PicmergeRowSignatures* sigs, int count, double max_fraction) {
    PicmergeFixedBars bars;
    int height;
    int cap;
    int top;
    int bot;
    int max_outliers;
    int i;

    bars.top_height = 0;
    bars.bottom_height = 0;
    bars.top_ref = 0;
    bars.bot_ref = 0;

    if (!sigs || count <= 0) return bars;
    height = sigs[0].height;
    if (height <= 0) return bars;
    for (i = 1; i < count; ++i) {
        if (sigs[i].height != height) return bars;
    }

    cap = (int)(height * max_fraction);
    top = 0;
    while (top < cap) {
        if (!picmerge_row_is_bar(sigs, count, top, 0, 0)) break;
        ++top;
    }

    max_outliers = (count <= 2) ? 0 : count / 3;
    bot = 0;
    while (bot < cap) {
        int y = height - 1 - bot;
        if (y < top) break;
        if (!picmerge_row_is_bar(sigs, count, y, max_outliers, 1)) break;
        ++bot;
    }

    bars.top_height = top;
    bars.bottom_height = bot;
    if (top > 0) bars.top_ref = picmerge_find_best_ref(sigs, count, 0, top);
    if (bot > 0) bars.bot_ref = picmerge_find_best_ref(sigs, count, height - bot, height);

    {
        const char* env = getenv("PICMERGE_DEBUG_BARS");
        if (env && *env) {
            int start = height - bot - 10;
            int end = height - bot + 5;
            if (start < 0) start = 0;
            if (end > height) end = height;
            for (i = start; i < end; ++i) {
                int j;
                fprintf(stderr, "  row %4d (from_bot %4d):", i, height - 1 - i);
                for (j = 1; j < count; ++j) {
                    int l1 = picmerge_row_l1(picmerge_row_signature_row(&sigs[0], i),
                                             picmerge_row_signature_row(&sigs[j], i));
                    fprintf(stderr, " L1[0-%d]=%d", j, l1);
                }
                fprintf(stderr, "%s\n", (i >= height - bot) ? "  *bar*" : "");
            }
            if (bot > 0) fprintf(stderr, "  bot_ref = img[%d]\n", bars.bot_ref);
        }
    }

    return bars;
}

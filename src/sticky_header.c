#include "sticky_header.h"

static int picmerge_min_int(int a, int b) {
    return (a < b) ? a : b;
}

int picmerge_detect_sticky_header(const PicmergeRowSignatures* prev,
                                  const PicmergeRowSignatures* next,
                                  int top_bar_height,
                                  int bottom_bar_height,
                                  int max_sticky) {
    int height;
    int limit;
    int s;
    const int sticky_tol = 4;

    if (!prev || !next) return 0;
    if (prev->height != next->height) return 0;
    height = prev->height;
    limit = picmerge_min_int(max_sticky, height - top_bar_height - bottom_bar_height);
    if (limit <= 0) return 0;

    s = 0;
    while (s < limit) {
        int y = top_bar_height + s;
        if (!picmerge_rows_match(picmerge_row_signature_row(prev, y),
                                 picmerge_row_signature_row(next, y),
                                 sticky_tol)) {
            break;
        }
        ++s;
    }
    return s;
}

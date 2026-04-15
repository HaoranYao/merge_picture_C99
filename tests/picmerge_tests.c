#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "bar_detector.h"
#include "image_io.h"
#include "overlap_finder.h"
#include "row_signature.h"
#include "sticky_header.h"
#include "stitcher.h"

typedef struct StringArray {
    char** items;
    int count;
    int capacity;
} StringArray;

typedef struct DatasetMetrics {
    char name[128];
    int num_images;
    int width;
    int height;
    int top_bar;
    int bottom_bar;
    int sticky_images;
    int overlap_pairs;
    int overlap_ok;
    int seam_trimmed_pairs;
    int output_height;
    double sample_retention;
    int max_duplicate_rows;
} DatasetMetrics;

#ifdef _WIN32
static const char PICMERGE_PATH_SEP = '\\';
#else
static const char PICMERGE_PATH_SEP = '/';
#endif

static void fail(const char* message) {
    fprintf(stderr, "[FAIL] %s\n", message);
    exit(1);
}

static void expect(int condition, const char* message) {
    if (!condition) fail(message);
}

static char* dupstr_local(const char* s) {
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, s, n + 1);
    return out;
}

static void string_array_init(StringArray* arr) { arr->items = NULL; arr->count = 0; arr->capacity = 0; }
static void string_array_reset(StringArray* arr) {
    int i;
    for (i = 0; i < arr->count; ++i) free(arr->items[i]);
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}
static int string_array_push(StringArray* arr, const char* s) {
    char** new_items;
    char* copy;
    int new_capacity;
    if (arr->count == arr->capacity) {
        new_capacity = arr->capacity ? arr->capacity * 2 : 16;
        new_items = (char**)realloc(arr->items, (size_t)new_capacity * sizeof(char*));
        if (!new_items) return 0;
        arr->items = new_items;
        arr->capacity = new_capacity;
    }
    copy = dupstr_local(s);
    if (!copy) return 0;
    arr->items[arr->count++] = copy;
    return 1;
}

static const char* basename_ptr(const char* path) {
    const char* a = strrchr(path, '/');
    const char* b = strrchr(path, '\\');
    if (a && (!b || a > b)) return a + 1;
    if (b) return b + 1;
    return path;
}

static int natural_less(const char* a, const char* b) {
    size_t i = 0, j = 0;
    while (a[i] && b[j]) {
        int da = isdigit((unsigned char)a[i]) != 0;
        int db = isdigit((unsigned char)b[j]) != 0;
        if (da && db) {
            size_t ia = i, jb = j, la_start, lb_start, la, lb, k;
            while (a[ia] && isdigit((unsigned char)a[ia])) ++ia;
            while (b[jb] && isdigit((unsigned char)b[jb])) ++jb;
            la_start = i;
            lb_start = j;
            while (la_start + 1 < ia && a[la_start] == '0') ++la_start;
            while (lb_start + 1 < jb && b[lb_start] == '0') ++lb_start;
            la = ia - la_start;
            lb = jb - lb_start;
            if (la != lb) return la < lb;
            for (k = 0; k < la; ++k) {
                if (a[la_start + k] != b[lb_start + k]) return a[la_start + k] < b[lb_start + k];
            }
            i = ia;
            j = jb;
        } else {
            char ca = (char)tolower((unsigned char)a[i]);
            char cb = (char)tolower((unsigned char)b[j]);
            if (ca != cb) return ca < cb;
            ++i;
            ++j;
        }
    }
    return a[i] == '\0' && b[j] != '\0';
}

static int qsort_natural_compare(const void* lhs, const void* rhs) {
    const char* a = *(const char* const*)lhs;
    const char* b = *(const char* const*)rhs;
    return natural_less(basename_ptr(a), basename_ptr(b)) ? -1 :
           natural_less(basename_ptr(b), basename_ptr(a)) ? 1 : 0;
}

static int has_image_extension(const char* path) {
    const char* dot = strrchr(path, '.');
    char ext[16];
    size_t i;
    if (!dot) return 0;
    for (i = 0; dot[i] && i + 1 < sizeof(ext); ++i) ext[i] = (char)tolower((unsigned char)dot[i]);
    ext[i] = '\0';
    return strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".png") == 0;
}

static int is_merge_output(const char* path) {
    return strncmp(basename_ptr(path), "merge_", 6) == 0;
}

static char* join_path2(const char* a, const char* b) {
    size_t na = strlen(a);
    size_t nb = strlen(b);
    int need_sep = (na > 0 && a[na - 1] != '/' && a[na - 1] != '\\');
    char* out = (char*)malloc(na + nb + (size_t)need_sep + 1);
    if (!out) return NULL;
    memcpy(out, a, na);
    if (need_sep) out[na++] = PICMERGE_PATH_SEP;
    memcpy(out + na, b, nb + 1);
    return out;
}

#ifdef _WIN32
static int path_is_directory(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
static int path_is_regular_file(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
static int collect_directory_entries(const char* dir, StringArray* files, StringArray* subdirs) {
    char* pattern = join_path2(dir, "*");
    WIN32_FIND_DATAA data;
    HANDLE handle;
    if (!pattern) return 0;
    handle = FindFirstFileA(pattern, &data);
    free(pattern);
    if (handle == INVALID_HANDLE_VALUE) return 1;
    do {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        {
            char* full = join_path2(dir, data.cFileName);
            if (!full) { FindClose(handle); return 0; }
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (subdirs && !string_array_push(subdirs, full)) { free(full); FindClose(handle); return 0; }
            } else {
                if (files && !string_array_push(files, full)) { free(full); FindClose(handle); return 0; }
            }
            free(full);
        }
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
    return 1;
}
#else
static int path_is_directory(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}
static int path_is_regular_file(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}
static int collect_directory_entries(const char* dir, StringArray* files, StringArray* subdirs) {
    DIR* d = opendir(dir);
    struct dirent* ent;
    if (!d) return 0;
    while ((ent = readdir(d)) != NULL) {
        char* full;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        full = join_path2(dir, ent->d_name);
        if (!full) { closedir(d); return 0; }
        if (path_is_directory(full)) {
            if (subdirs && !string_array_push(subdirs, full)) { free(full); closedir(d); return 0; }
        } else if (path_is_regular_file(full)) {
            if (files && !string_array_push(files, full)) { free(full); closedir(d); return 0; }
        }
        free(full);
    }
    closedir(d);
    return 1;
}
#endif

static void make_signatures(const int* values, int count, PicmergeRowSignatures* sigs) {
    int y, k;
    picmerge_row_signatures_init(sigs);
    sigs->height = count;
    sigs->fp = (uint8_t*)malloc((size_t)count * PICMERGE_SIG_BINS);
    expect(sigs->fp != NULL, "alloc failed");
    for (y = 0; y < count; ++y) {
        for (k = 0; k < PICMERGE_SIG_BINS; ++k) sigs->fp[(size_t)y * PICMERGE_SIG_BINS + k] = (uint8_t)values[y];
    }
}

static void collect_images(const char* dir, StringArray* paths) {
    StringArray entries;
    int i;
    string_array_init(&entries);
    if (!collect_directory_entries(dir, &entries, NULL)) fail("collect images failed");
    for (i = 0; i < entries.count; ++i) {
        if (!path_is_regular_file(entries.items[i])) continue;
        if (!has_image_extension(entries.items[i])) continue;
        if (is_merge_output(entries.items[i])) continue;
        if (!string_array_push(paths, entries.items[i])) fail("push image failed");
    }
    qsort(paths->items, (size_t)paths->count, sizeof(char*), qsort_natural_compare);
    string_array_reset(&entries);
}

static void collect_demo_dirs(const char* demo_root, StringArray* dirs) {
    StringArray subdirs;
    int i;
    string_array_init(&subdirs);
    if (!collect_directory_entries(demo_root, NULL, &subdirs)) fail("collect demo dirs failed");
    for (i = 0; i < subdirs.count; ++i) if (!string_array_push(dirs, subdirs.items[i])) fail("push demo dir failed");
    qsort(dirs->items, (size_t)dirs->count, sizeof(char*), qsort_natural_compare);
    string_array_reset(&subdirs);
}

static PicmergeRowSignatures* load_signatures(const StringArray* paths, int* width, int* height) {
    PicmergeRowSignatures* sigs = (PicmergeRowSignatures*)calloc((size_t)paths->count, sizeof(PicmergeRowSignatures));
    int i;
    expect(sigs != NULL, "alloc signatures failed");
    for (i = 0; i < paths->count; ++i) {
        PicmergeImage img;
        picmerge_image_init(&img);
        picmerge_row_signatures_init(&sigs[i]);
        expect(picmerge_image_load(&img, paths->items[i]), "failed to load image");
        if (i == 0) {
            *width = img.width;
            *height = img.height;
        } else {
            expect(img.width == *width && img.height == *height, "dimension mismatch in dataset");
        }
        expect(picmerge_compute_row_signatures(&img, &sigs[i]), "compute row signatures failed");
        picmerge_image_reset(&img);
    }
    return sigs;
}

static void free_signatures(PicmergeRowSignatures* sigs, int count) {
    int i;
    if (!sigs) return;
    for (i = 0; i < count; ++i) picmerge_row_signatures_reset(&sigs[i]);
    free(sigs);
}

static DatasetMetrics analyze_dataset(const char* dir) {
    DatasetMetrics metrics;
    StringArray paths;
    PicmergeRowSignatures* sigs;
    PicmergeFixedBars bars;
    int usable_end;
    int max_sticky;
    int* sticky_pair;
    int* self_sticky;
    PicmergeOverlapResult* overlaps;
    int* chrome_pair;
    int* fallback_skip;
    int bar_ref;
    PicmergeStitchPlan plan;
    char* out_path;
    PicmergeImage out_img;
    PicmergeRowSignatures out_sigs;
    int sample_total = 0, sample_matches = 0, out_y = 0, i;

    memset(&metrics, 0, sizeof(metrics));
    strncpy(metrics.name, basename_ptr(dir), sizeof(metrics.name) - 1);
    string_array_init(&paths);
    collect_images(dir, &paths);
    metrics.num_images = paths.count;
    expect(metrics.num_images >= 2, "dataset needs at least 2 images");

    sigs = load_signatures(&paths, &metrics.width, &metrics.height);
    bars = picmerge_detect_fixed_bars(sigs, paths.count, 0.20);
    metrics.top_bar = bars.top_height;
    metrics.bottom_bar = bars.bottom_height;
    usable_end = metrics.height - bars.bottom_height;
    max_sticky = (int)((metrics.height - bars.top_height - bars.bottom_height) * 0.40);

    sticky_pair = (int*)calloc((size_t)paths.count, sizeof(int));
    self_sticky = (int*)calloc((size_t)paths.count, sizeof(int));
    overlaps = (PicmergeOverlapResult*)calloc((size_t)(paths.count - 1), sizeof(PicmergeOverlapResult));
    chrome_pair = (int*)calloc((size_t)paths.count, sizeof(int));
    fallback_skip = (int*)calloc((size_t)paths.count, sizeof(int));
    expect(sticky_pair && self_sticky && overlaps && chrome_pair && fallback_skip, "alloc analysis arrays failed");

    for (i = 1; i < paths.count; ++i) {
        sticky_pair[i] = picmerge_detect_sticky_header(&sigs[i - 1], &sigs[i], bars.top_height, bars.bottom_height, max_sticky);
    }
    for (i = 0; i < paths.count; ++i) {
        int s = 0;
        if (i >= 1 && sticky_pair[i] > s) s = sticky_pair[i];
        if (i + 1 < paths.count && sticky_pair[i + 1] > s) s = sticky_pair[i + 1];
        self_sticky[i] = s;
        if (s > 0) ++metrics.sticky_images;
    }
    for (i = 0; i + 1 < paths.count; ++i) {
        int prev_sticky = self_sticky[i];
        int next_sticky = self_sticky[i + 1];
        int shared = (prev_sticky > next_sticky) ? prev_sticky : next_sticky;
        int next_template_start = bars.top_height + shared;
        int prev_search_begin = bars.top_height + prev_sticky;
        overlaps[i] = picmerge_find_overlap(&sigs[i], &sigs[i + 1],
                                            prev_search_begin, usable_end,
                                            next_template_start, usable_end);
        ++metrics.overlap_pairs;
        if (overlaps[i].ok) {
            picmerge_refine_overlap_seam(&sigs[i], &sigs[i + 1], &overlaps[i], usable_end);
            ++metrics.overlap_ok;
            if (overlaps[i].seam_in_prev < usable_end) ++metrics.seam_trimmed_pairs;
        }
    }
    for (i = 1; i < paths.count; ++i) {
        int s = 0;
        while (s < max_sticky) {
            int y = bars.top_height + s;
            if (picmerge_row_l1(picmerge_row_signature_row(&sigs[i - 1], y),
                                picmerge_row_signature_row(&sigs[i], y)) > 300) break;
            ++s;
        }
        chrome_pair[i] = s;
    }
    for (i = 0; i < paths.count; ++i) {
        int c = self_sticky[i];
        if (i >= 1 && chrome_pair[i] > c) c = chrome_pair[i];
        if (i + 1 < paths.count && chrome_pair[i + 1] > c) c = chrome_pair[i + 1];
        fallback_skip[i] = c;
    }

    bar_ref = (bars.bottom_height > 0) ? bars.bot_ref : bars.top_ref;
    picmerge_stitch_plan_init(&plan);
    expect(picmerge_plan_stitch(&plan, metrics.width, metrics.height, paths.count,
                                bars.top_height, bars.bottom_height, bar_ref,
                                self_sticky, fallback_skip, overlaps),
           "plan_stitch failed");

    out_path = join_path2(PICMERGE_SOURCE_DIR, "picmerge_c99_test_output.jpg");
    expect(out_path != NULL, "alloc output path failed");
    expect(picmerge_execute_stitch(&plan, (const char* const*)paths.items, paths.count, out_path, 90), "execute_stitch failed");

    picmerge_image_init(&out_img);
    expect(picmerge_image_load(&out_img, out_path), "failed to load stitched output");
    expect(out_img.width == plan.width && out_img.height == plan.height, "stitched output dimensions mismatch");
    metrics.output_height = out_img.height;

    picmerge_row_signatures_init(&out_sigs);
    expect(picmerge_compute_row_signatures(&out_img, &out_sigs), "compute output signatures failed");

    for (i = 0; i < plan.count; ++i) {
        int rows = plan.parts[i].y_end - plan.parts[i].y_begin;
        int step;
        int local;
        if (rows <= 0) continue;
        step = (rows / 8 > 0) ? rows / 8 : 1;
        for (local = 0; local < rows; local += step) {
            int src_y = plan.parts[i].y_begin + local;
            int dst_y = out_y + local;
            ++sample_total;
            if (picmerge_row_l1(picmerge_row_signature_row(&sigs[plan.parts[i].image_index], src_y),
                                picmerge_row_signature_row(&out_sigs, dst_y)) <= 200) {
                ++sample_matches;
            }
        }
        if ((rows - 1) % step != 0) {
            int src_y = plan.parts[i].y_end - 1;
            int dst_y = out_y + rows - 1;
            ++sample_total;
            if (picmerge_row_l1(picmerge_row_signature_row(&sigs[plan.parts[i].image_index], src_y),
                                picmerge_row_signature_row(&out_sigs, dst_y)) <= 200) {
                ++sample_matches;
            }
        }
        out_y += rows;
    }
    metrics.sample_retention = (sample_total > 0) ? (double)sample_matches / (double)sample_total : 0.0;

    {
        int seam_y = 0;
        for (i = 0; i + 1 < plan.count; ++i) {
            int max_check = 128;
            int longest = 0;
            int len;
            seam_y += plan.parts[i].y_end - plan.parts[i].y_begin;
            if (seam_y < max_check) max_check = seam_y;
            if (plan.height - seam_y < max_check) max_check = plan.height - seam_y;
            for (len = 1; len <= max_check; ++len) {
                int same = 1, j;
                for (j = 0; j < len; ++j) {
                    if (picmerge_row_l1(picmerge_row_signature_row(&out_sigs, seam_y - len + j),
                                        picmerge_row_signature_row(&out_sigs, seam_y + j)) > 80) {
                        same = 0;
                        break;
                    }
                }
                if (!same) break;
                longest = len;
            }
            if (longest > metrics.max_duplicate_rows) metrics.max_duplicate_rows = longest;
        }
    }

    picmerge_row_signatures_reset(&out_sigs);
    picmerge_image_reset(&out_img);
    remove(out_path);
    free(out_path);
    picmerge_stitch_plan_reset(&plan);
    free(sticky_pair);
    free(self_sticky);
    free(overlaps);
    free(chrome_pair);
    free(fallback_skip);
    free_signatures(sigs, paths.count);
    string_array_reset(&paths);
    return metrics;
}

static void test_detect_sticky_header(void) {
    PicmergeRowSignatures prev, next;
    int prev_vals[] = {1, 2, 10, 10, 10, 20, 21, 22, 23, 24};
    int next_vals[] = {1, 2, 10, 10, 10, 30, 31, 32, 33, 34};
    make_signatures(prev_vals, 10, &prev);
    make_signatures(next_vals, 10, &next);
    expect(picmerge_detect_sticky_header(&prev, &next, 2, 0, 5) == 3, "sticky header height should be 3");
    picmerge_row_signatures_reset(&prev);
    picmerge_row_signatures_reset(&next);
}

static void test_overlap_finder_with_dynamic_header(void) {
    int prev_vals[400];
    int next_vals[368];
    PicmergeRowSignatures prev, next;
    PicmergeOverlapResult r;
    int i;
    for (i = 0; i < 400; ++i) prev_vals[i] = i % 256;
    for (i = 0; i < 128; ++i) next_vals[i] = 250;
    for (i = 128; i < 368; ++i) next_vals[i] = (i + 32) % 256;
    make_signatures(prev_vals, 400, &prev);
    make_signatures(next_vals, 368, &next);
    r = picmerge_find_overlap(&prev, &next, 0, prev.height, 0, next.height);
    expect(r.ok, "overlap should succeed after skipping dynamic header");
    expect(r.template_start_in_next >= 128, "template should move beyond dynamic header");
    expect(r.offset_in_prev == 160, "overlap offset should align to the later clean template");
    picmerge_row_signatures_reset(&prev);
    picmerge_row_signatures_reset(&next);
}

static void test_refine_overlap_seam_trims_dirty_tail(void) {
    int prev_vals[90];
    int next_vals[90];
    PicmergeRowSignatures prev, next;
    PicmergeOverlapResult r;
    int i;
    for (i = 0; i < 80; ++i) { prev_vals[i] = i; next_vals[i] = i; }
    for (i = 80; i < 90; ++i) { prev_vals[i] = 250; next_vals[i] = i; }
    make_signatures(prev_vals, 90, &prev);
    make_signatures(next_vals, 90, &next);
    picmerge_overlap_result_init(&r);
    r.ok = 1;
    r.offset_in_prev = 0;
    r.template_start_in_next = 0;
    r.template_length = 32;
    picmerge_refine_overlap_seam(&prev, &next, &r, prev.height);
    expect(r.seam_in_prev < prev.height, "dirty tail should move seam upward");
    picmerge_row_signatures_reset(&prev);
    picmerge_row_signatures_reset(&next);
}

static void test_plan_stitch_avoids_overlap_duplication(void) {
    int sticky[2] = {0, 0};
    int fallback[2] = {0, 0};
    PicmergeOverlapResult overlaps[1];
    PicmergeStitchPlan plan;
    picmerge_overlap_result_init(&overlaps[0]);
    overlaps[0].ok = 1;
    overlaps[0].offset_in_prev = 60;
    overlaps[0].template_start_in_next = 0;
    overlaps[0].template_length = 32;
    overlaps[0].seam_in_prev = 100;
    picmerge_stitch_plan_init(&plan);
    expect(picmerge_plan_stitch(&plan, 100, 120, 2, 5, 10, 0, sticky, fallback, overlaps), "plan stitch failed");
    expect(plan.height == 180, "plan height should remove overlap and duplicate bottom bar");
    expect(plan.count == 3, "plan should contain content+content+bottom bar spans");
    expect(plan.parts[0].image_index == 0 && plan.parts[0].y_begin == 0 && plan.parts[0].y_end == 100, "first image span mismatch");
    expect(plan.parts[1].image_index == 1 && plan.parts[1].y_begin == 40 && plan.parts[1].y_end == 110, "second image span mismatch");
    picmerge_stitch_plan_reset(&plan);
}

static void test_demo_datasets(void) {
    StringArray dirs;
    char* demo_root;
    int total_pairs = 0, total_overlap_ok = 0, total_trimmed = 0, datasets_with_sticky = 0, worst_duplicate_rows = 0, pdd3_bottom_bar = 0, i;
    double retention_sum = 0.0, retention_min = 1.0;
    string_array_init(&dirs);
    demo_root = join_path2(PICMERGE_SOURCE_DIR, "demo_pic");
    expect(demo_root != NULL, "alloc demo_root failed");
    collect_demo_dirs(demo_root, &dirs);
    expect(dirs.count > 0, "demo_pic is empty");

    for (i = 0; i < dirs.count; ++i) {
        DatasetMetrics m = analyze_dataset(dirs.items[i]);
        total_pairs += m.overlap_pairs;
        total_overlap_ok += m.overlap_ok;
        total_trimmed += m.seam_trimmed_pairs;
        if (m.sticky_images > 0) ++datasets_with_sticky;
        retention_sum += m.sample_retention;
        if (m.sample_retention < retention_min) retention_min = m.sample_retention;
        if (m.max_duplicate_rows > worst_duplicate_rows) worst_duplicate_rows = m.max_duplicate_rows;
        if (strcmp(m.name, "pdd3") == 0) pdd3_bottom_bar = m.bottom_bar;

        printf("[dataset] %s images=%d bars(top=%d, bottom=%d) sticky_images=%d overlap=%d/%d seam_trimmed=%d retention=%.6f max_duplicate_rows=%d output_height=%d\n",
               m.name, m.num_images, m.top_bar, m.bottom_bar, m.sticky_images, m.overlap_ok, m.overlap_pairs,
               m.seam_trimmed_pairs, m.sample_retention, m.max_duplicate_rows, m.output_height);

        expect(m.output_height > m.height, "stitched output should exceed one input image");
        expect(m.output_height < m.height * m.num_images, "stitched output should be shorter than raw concat");
        expect(m.sample_retention >= 0.95, "retention too low");
        expect(m.max_duplicate_rows <= 64, "duplicate overlap leak too large");
    }

    printf("[summary] datasets=%d overlap_ratio=%.6f datasets_with_sticky=%d seam_trimmed_pairs=%d avg_retention=%.6f min_retention=%.6f worst_duplicate_rows=%d\n",
           dirs.count,
           total_pairs > 0 ? (double)total_overlap_ok / (double)total_pairs : 0.0,
           datasets_with_sticky, total_trimmed,
           retention_sum / (double)dirs.count, retention_min, worst_duplicate_rows);

    expect(total_pairs > 0 && (double)total_overlap_ok / (double)total_pairs >= 0.80, "overall overlap success ratio is too low");
    expect(datasets_with_sticky >= 3, "sticky-header coverage is too small");
    expect(total_trimmed >= 1, "dirty seam refinement never triggered");
    expect(retention_sum / (double)dirs.count >= 0.98, "average retention is too low");
    expect(retention_min >= 0.95, "minimum retention is too low");
    expect(worst_duplicate_rows <= 64, "worst duplicate leakage is too large");
    expect(pdd3_bottom_bar >= 240, "pdd3 bottom bar regression");

    string_array_reset(&dirs);
    free(demo_root);
}

int main(void) {
    test_detect_sticky_header();
    test_overlap_finder_with_dynamic_header();
    test_refine_overlap_seam_trims_dirty_tail();
    test_plan_stitch_avoids_overlap_duplication();
    test_demo_datasets();
    printf("[PASS] picmerge_c99 tests\n");
    return 0;
}

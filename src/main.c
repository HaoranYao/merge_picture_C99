#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

enum { PICMERGE_JPEG_QUALITY = 90 };
static const char* PICMERGE_OUTPUT_PREFIX = "merge_";
#ifdef _WIN32
static const char PICMERGE_PATH_SEP = '\\';
#else
static const char PICMERGE_PATH_SEP = '/';
#endif

typedef struct StringArray {
    char** items;
    int count;
    int capacity;
} StringArray;

static void string_array_init(StringArray* arr) { arr->items = NULL; arr->count = 0; arr->capacity = 0; }
static void string_array_reset(StringArray* arr) {
    int i;
    for (i = 0; i < arr->count; ++i) free(arr->items[i]);
    free(arr->items);
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
}
static char* dupstr_local(const char* s) {
    size_t n = strlen(s);
    char* copy = (char*)malloc(n + 1);
    if (!copy) return NULL;
    memcpy(copy, s, n + 1);
    return copy;
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
    return strncmp(basename_ptr(path), PICMERGE_OUTPUT_PREFIX, strlen(PICMERGE_OUTPUT_PREFIX)) == 0;
}
static int natural_less(const char* a, const char* b) {
    size_t i = 0;
    size_t j = 0;
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
static void make_output_name(char* buffer, size_t buffer_size) {
    long long now = (long long)time(NULL);
    snprintf(buffer, buffer_size, "%s%lld.jpg", PICMERGE_OUTPUT_PREFIX, now);
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

static int usage(void) {
    fprintf(stderr, "Usage: picmerge_c99 <input_dir>\n");
    fprintf(stderr,
            "  Reads all .jpg/.jpeg/.png files in <input_dir>, sorts\n"
            "  them in natural order, and writes merge_<timestamp>.jpg\n"
            "  alongside the input images. If <input_dir> contains\n"
            "  subdirectories, each subdirectory is merged independently.\n");
    return 1;
}

static int merge_directory(const char* dir);

int main(int argc, char** argv) {
    StringArray subdirs, root_entries;
    int root_has_images = 0;
    int exit_code = 0;
    int i;

    if (argc != 2) return usage();
    if (!path_is_directory(argv[1])) {
        fprintf(stderr, "[error] not a directory: %s\n", argv[1]);
        return 2;
    }

    string_array_init(&subdirs);
    string_array_init(&root_entries);
    if (!collect_directory_entries(argv[1], &root_entries, &subdirs)) {
        fprintf(stderr, "[error] directory iteration failed: %s\n", argv[1]);
        string_array_reset(&subdirs);
        string_array_reset(&root_entries);
        return 2;
    }

    for (i = 0; i < root_entries.count; ++i) {
        if (path_is_regular_file(root_entries.items[i]) &&
            has_image_extension(root_entries.items[i]) &&
            !is_merge_output(root_entries.items[i])) {
            root_has_images = 1;
            break;
        }
    }
    qsort(subdirs.items, (size_t)subdirs.count, sizeof(char*), qsort_natural_compare);

    if (!root_has_images && subdirs.count == 0) {
        fprintf(stderr, "[error] no image files found in %s or its subdirectories\n", argv[1]);
        string_array_reset(&subdirs);
        string_array_reset(&root_entries);
        return 2;
    }

    if (root_has_images && !merge_directory(argv[1])) exit_code = 2;
    for (i = 0; i < subdirs.count; ++i) if (!merge_directory(subdirs.items[i])) exit_code = 2;

    string_array_reset(&subdirs);
    string_array_reset(&root_entries);
    return exit_code;
}

static int merge_directory(const char* dir) {
    StringArray entries, paths;
    PicmergeRowSignatures* sigs = NULL;
    int* sticky_pair = NULL;
    int* self_sticky = NULL;
    PicmergeOverlapResult* overlaps = NULL;
    int* chrome_pair = NULL;
    int* fallback_skip = NULL;
    PicmergeStitchPlan plan;
    int ref_w = 0, ref_h = 0, usable_end = 0, bar_ref = 0, i;
    char out_name[64];
    char* out_path = NULL;
    int ok = 0;

    string_array_init(&entries);
    string_array_init(&paths);
    picmerge_stitch_plan_init(&plan);

    if (!collect_directory_entries(dir, &entries, NULL)) {
        fprintf(stderr, "[error] directory iteration failed in %s\n", dir);
        goto cleanup;
    }
    for (i = 0; i < entries.count; ++i) {
        if (!path_is_regular_file(entries.items[i])) continue;
        if (!has_image_extension(entries.items[i])) continue;
        if (is_merge_output(entries.items[i])) continue;
        if (!string_array_push(&paths, entries.items[i])) goto cleanup;
    }
    if (paths.count == 0) {
        ok = 1;
        goto cleanup;
    }
    qsort(paths.items, (size_t)paths.count, sizeof(char*), qsort_natural_compare);

    make_output_name(out_name, sizeof(out_name));
    out_path = join_path2(dir, out_name);
    if (!out_path) goto cleanup;

    fprintf(stdout, "\n[dir] %s - %d image(s)\n", dir, paths.count);
    for (i = 0; i < paths.count; ++i) fprintf(stdout, "         %s\n", paths.items[i]);

    if (!picmerge_probe_image(paths.items[0], &ref_w, &ref_h) || ref_w <= 0 || ref_h <= 0) {
        fprintf(stderr, "[error] cannot read image metadata: %s\n", paths.items[0]);
        goto cleanup;
    }
    for (i = 1; i < paths.count; ++i) {
        int w = 0, h = 0;
        if (!picmerge_probe_image(paths.items[i], &w, &h)) {
            fprintf(stderr, "[error] cannot read image metadata: %s\n", paths.items[i]);
            goto cleanup;
        }
        if (w != ref_w || h != ref_h) {
            fprintf(stderr, "[error] dimension mismatch: %s is %dx%d, expected %dx%d\n",
                    paths.items[i], w, h, ref_w, ref_h);
            goto cleanup;
        }
    }
    fprintf(stdout, "[info] all images are %dx%d\n", ref_w, ref_h);

    if (paths.count == 1) {
        PicmergeImage img;
        picmerge_image_init(&img);
        if (!picmerge_image_load(&img, paths.items[0])) {
            fprintf(stderr, "[error] failed to decode %s\n", paths.items[0]);
            goto cleanup;
        }
        if (!picmerge_write_jpeg(out_path, img.width, img.height, img.data, PICMERGE_JPEG_QUALITY)) {
            picmerge_image_reset(&img);
            goto cleanup;
        }
        fprintf(stdout, "[info] wrote %s (%dx%d, single image)\n", out_path, img.width, img.height);
        picmerge_image_reset(&img);
        ok = 1;
        goto cleanup;
    }

    sigs = (PicmergeRowSignatures*)calloc((size_t)paths.count, sizeof(PicmergeRowSignatures));
    if (!sigs) goto cleanup;
    for (i = 0; i < paths.count; ++i) picmerge_row_signatures_init(&sigs[i]);
    for (i = 0; i < paths.count; ++i) {
        PicmergeImage img;
        picmerge_image_init(&img);
        if (!picmerge_image_load(&img, paths.items[i])) {
            fprintf(stderr, "[error] failed to decode %s\n", paths.items[i]);
            picmerge_image_reset(&img);
            goto cleanup;
        }
        if (!picmerge_compute_row_signatures(&img, &sigs[i])) {
            fprintf(stderr, "[error] failed to compute signatures for %s\n", paths.items[i]);
            picmerge_image_reset(&img);
            goto cleanup;
        }
        picmerge_image_reset(&img);
    }

    {
        PicmergeFixedBars bars = picmerge_detect_fixed_bars(sigs, paths.count, 0.20);
        int max_sticky = (int)((ref_h - bars.top_height - bars.bottom_height) * 0.40);
        usable_end = ref_h - bars.bottom_height;

        fprintf(stdout, "[info] fixed top bar = %d rows, bottom bar = %d rows", bars.top_height, bars.bottom_height);
        if (bars.bot_ref != 0) fprintf(stdout, " (bar ref = img[%d])", bars.bot_ref);
        fprintf(stdout, "\n");

        sticky_pair = (int*)calloc((size_t)paths.count, sizeof(int));
        self_sticky = (int*)calloc((size_t)paths.count, sizeof(int));
        overlaps = (PicmergeOverlapResult*)calloc((size_t)(paths.count - 1), sizeof(PicmergeOverlapResult));
        chrome_pair = (int*)calloc((size_t)paths.count, sizeof(int));
        fallback_skip = (int*)calloc((size_t)paths.count, sizeof(int));
        if (!sticky_pair || !self_sticky || !overlaps || !chrome_pair || !fallback_skip) goto cleanup;

        for (i = 1; i < paths.count; ++i) {
            sticky_pair[i] = picmerge_detect_sticky_header(&sigs[i - 1], &sigs[i],
                                                           bars.top_height, bars.bottom_height, max_sticky);
        }
        for (i = 0; i < paths.count; ++i) {
            int s = 0;
            if (i >= 1 && sticky_pair[i] > s) s = sticky_pair[i];
            if (i + 1 < paths.count && sticky_pair[i + 1] > s) s = sticky_pair[i + 1];
            self_sticky[i] = s;
            if (s > 0) fprintf(stdout, "[info] img[%d] self sticky header = %d rows\n", i, s);
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
            if (overlaps[i].ok) {
                int overlap_h;
                int seam_trim;
                picmerge_refine_overlap_seam(&sigs[i], &sigs[i + 1], &overlaps[i], usable_end);
                overlap_h = usable_end - overlaps[i].offset_in_prev;
                seam_trim = usable_end - overlaps[i].seam_in_prev;
                fprintf(stdout,
                        "[info] pair %d->%d: overlap=%d rows, offset_in_prev=%d, cost=%.0f (runner-up=%.0f)",
                        i, i + 1, overlap_h, overlaps[i].offset_in_prev,
                        overlaps[i].best_cost, overlaps[i].second_best_cost);
                if (seam_trim > 0) fprintf(stdout, ", seam_trim=%d", seam_trim);
                fprintf(stdout, "\n");
            } else {
                fprintf(stderr,
                        "[warn] overlap detection failed between img[%d] and img[%d]; falling back to direct concat (best cost=%.0f, runner-up=%.0f)\n",
                        i, i + 1, overlaps[i].best_cost, overlaps[i].second_best_cost);
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
        if (!picmerge_plan_stitch(&plan, ref_w, ref_h, paths.count,
                                  bars.top_height, bars.bottom_height, bar_ref,
                                  self_sticky, fallback_skip, overlaps)) {
            goto cleanup;
        }
        fprintf(stdout, "[info] output dimensions: %dx%d, %d span(s)\n", plan.width, plan.height, plan.count);

        if (!picmerge_execute_stitch(&plan, (const char* const*)paths.items, paths.count, out_path, PICMERGE_JPEG_QUALITY)) {
            goto cleanup;
        }
        fprintf(stdout, "[info] wrote %s (%dx%d)\n", out_path, plan.width, plan.height);
    }

    ok = 1;

cleanup:
    if (sigs) for (i = 0; i < paths.count; ++i) picmerge_row_signatures_reset(&sigs[i]);
    free(sigs);
    free(sticky_pair);
    free(self_sticky);
    free(overlaps);
    free(chrome_pair);
    free(fallback_skip);
    picmerge_stitch_plan_reset(&plan);
    string_array_reset(&entries);
    string_array_reset(&paths);
    free(out_path);
    return ok;
}

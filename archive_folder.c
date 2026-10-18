/*
 * Virtual folder navigation for archive listings, including synthetic folders.
 * Based on file path components; extraction continues using original file indexes.
 */
#include <string.h>
#include <stdlib.h>
#include "archive_folder.h"

ArchiveFolderItem archive_folder_items[MAX_ARCHIVE_FILES];
int archive_folder_count = 0;
char archive_folder_prefix[256] = {0};

#define ARCHIVE_FOLDER_MAX_DEPTH 64
static int folder_depth = 0;
static int folder_saved_selected[ARCHIVE_FOLDER_MAX_DEPTH];
static int folder_saved_scroll[ARCHIVE_FOLDER_MAX_DEPTH];

static int item_compare(const void *a, const void *b) {
    const ArchiveFolderItem *x = (const ArchiveFolderItem *)a;
    const ArchiveFolderItem *y = (const ArchiveFolderItem *)b;
    if (x->is_directory != y->is_directory)
        return y->is_directory - x->is_directory;
    return strcmp(x->name, y->name);
}

static void archive_folder_rebuild(const ArchiveInfo *info) {
    archive_folder_count = 0;
    if (!info) return;
    size_t prefix_len = strlen(archive_folder_prefix);

    for (int i = 0; i < info->file_count; ++i) {
        const char *full = info->files[i].filename;
        if (strncmp(full, archive_folder_prefix, prefix_len) != 0)
            continue;
        const char *relative = full + prefix_len;
        if (!*relative) continue;

        const char *slash = strchr(relative, '/');
        int is_dir = slash != NULL || info->files[i].is_directory;
        size_t len = slash ? (size_t)(slash - relative) : strlen(relative);
        if (!len || len >= sizeof(archive_folder_items[0].name))
            continue;

        int existing = -1;
        for (int j = 0; j < archive_folder_count; j++) {
            if (archive_folder_items[j].is_directory == is_dir &&
                strlen(archive_folder_items[j].name) == len &&
                strncmp(archive_folder_items[j].name, relative, len) == 0) {
                existing = j;
                break;
            }
        }
        if (existing >= 0) continue;
        if (archive_folder_count >= MAX_ARCHIVE_FILES) break;

        ArchiveFolderItem *item = &archive_folder_items[archive_folder_count++];
        memcpy(item->name, relative, len);
        item->name[len] = '\0';
        item->is_directory = is_dir;
        item->file_index = is_dir ? -1 : i;
    }
    qsort(archive_folder_items, archive_folder_count,
          sizeof(archive_folder_items[0]), item_compare);
}

void archive_folder_reset(const ArchiveInfo *info, int *selected, int *scroll) {
    archive_folder_prefix[0] = '\0';
    folder_depth = 0;
    archive_folder_rebuild(info);
    if (selected) *selected = 0;
    if (scroll) *scroll = 0;
}

int archive_folder_file_index(int view_index) {
    if (view_index < 0 || view_index >= archive_folder_count)
        return -1;
    return archive_folder_items[view_index].file_index;
}

int archive_folder_is_directory(int view_index) {
    return view_index >= 0 && view_index < archive_folder_count &&
           archive_folder_items[view_index].is_directory;
}

int archive_folder_enter(int view_index, const ArchiveInfo *info,
                         int *selected, int *scroll) {
    if (!archive_folder_is_directory(view_index) ||
        folder_depth >= ARCHIVE_FOLDER_MAX_DEPTH) return 0;

    size_t prefix_len = strlen(archive_folder_prefix);
    size_t name_len = strlen(archive_folder_items[view_index].name);
    if (prefix_len + name_len + 1 >= sizeof(archive_folder_prefix)) return 0;
    folder_saved_selected[folder_depth] = selected ? *selected : 0;
    folder_saved_scroll[folder_depth] = scroll ? *scroll : 0;
    folder_depth++;

    memcpy(archive_folder_prefix + prefix_len,
           archive_folder_items[view_index].name, name_len);
    archive_folder_prefix[prefix_len + name_len] = '/';
    archive_folder_prefix[prefix_len + name_len + 1] = '\0';
    archive_folder_rebuild(info);
    if (selected) *selected = 0;
    if (scroll) *scroll = 0;
    return 1;
}

int archive_folder_up(const ArchiveInfo *info, int *selected, int *scroll) {
    if (!folder_depth) return 0;
    size_t n = strlen(archive_folder_prefix);
    if (n && archive_folder_prefix[n - 1] == '/') n--;
    while (n && archive_folder_prefix[n - 1] != '/') n--;
    archive_folder_prefix[n] = '\0';

    folder_depth--;
    archive_folder_rebuild(info);
    if (selected) *selected = folder_saved_selected[folder_depth];
    if (scroll) *scroll = folder_saved_scroll[folder_depth];
    return 1;
}

static int archive_folder_contains(int view_index, const char *full) {
    if (!archive_folder_is_directory(view_index) || !full) return 0;
    char child[256];
    size_t plen = strlen(archive_folder_prefix);
    size_t nlen = strlen(archive_folder_items[view_index].name);
    if (plen + nlen + 1 >= sizeof(child)) return 0;
    memcpy(child, archive_folder_prefix, plen);
    memcpy(child + plen, archive_folder_items[view_index].name, nlen);
    child[plen + nlen] = '/';
    child[plen + nlen + 1] = '\0';
    return strncmp(full, child, plen + nlen + 1) == 0;
}

int archive_folder_is_selected(int view_index, const ArchiveInfo *info,
                               const int *mask) {
    if (!info || !mask || view_index < 0 ||
        view_index >= archive_folder_count) return 0;

    int file_index = archive_folder_file_index(view_index);
    if (file_index >= 0) return mask[file_index] != 0;

    int found = 0;
    for (int i = 0; i < info->file_count; i++) {
        if (!info->files[i].is_directory &&
            archive_folder_contains(view_index, info->files[i].filename)) {
            found = 1;
            if (!mask[i]) return 0;
        }
    }
    return found;
}

void archive_folder_toggle_selection(int view_index, const ArchiveInfo *info,
                                     int *mask) {
    if (!info || !mask || view_index < 0 ||
        view_index >= archive_folder_count) return;
    int file_index = archive_folder_file_index(view_index);
    if (file_index >= 0) {
        mask[file_index] = !mask[file_index];
        return;
    }

    int target = !archive_folder_is_selected(view_index, info, mask);
    for (int i = 0; i < info->file_count; ++i) {
        if (!info->files[i].is_directory &&
            archive_folder_contains(view_index, info->files[i].filename))
            mask[i] = target;
    }
}

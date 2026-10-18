/*
 * Virtual folder navigation for VitaArchive archive listings.
 * No archive contents are modified: displayed entries retain original indexes.
 */
#ifndef ARCHIVE_FOLDER_H
#define ARCHIVE_FOLDER_H

#include "zip.h"

typedef struct {
    char name[256];          /* Name relative to displayed folder */
    int file_index;          /* Original ArchiveInfo index; -1 for folder */
    int is_directory;        /* Includes synthetic folders */
} ArchiveFolderItem;

extern ArchiveFolderItem archive_folder_items[MAX_ARCHIVE_FILES];
extern int archive_folder_count;
extern char archive_folder_prefix[256];

void archive_folder_reset(const ArchiveInfo *info, int *selected, int *scroll);
int archive_folder_enter(int view_index, const ArchiveInfo *info, int *selected, int *scroll);
int archive_folder_up(const ArchiveInfo *info, int *selected, int *scroll);
int archive_folder_file_index(int view_index);
int archive_folder_is_directory(int view_index);
void archive_folder_toggle_selection(int view_index, const ArchiveInfo *info, int *mask);
int archive_folder_is_selected(int view_index, const ArchiveInfo *info, const int *mask);

#endif

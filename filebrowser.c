/*
 * VitaArchive - File Archiver & Browser for PS Vita
 * Created by theheroGAC.
 * Special thanks to TheFloW, Rinnegatamante, SKGleba, and all developers, hackers,
 * and contributors of the PlayStation Vita homebrew scene.
 */

#include <stdio.h>
#include <string.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <ctype.h>
#include "filebrowser.h"

static char *stristr(const char *str1, const char *str2) {
    if (!str1 || !str2) return NULL;
    while (*str1) {
        const char *h = str1;
        const char *n = str2;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if (!*n) return (char *)str1;
        str1++;
    }
    return NULL;
}

int filebrowser_init(FileBrowser *fb, const char *start_path) {
    memset(fb, 0, sizeof(FileBrowser));
    memset(fb->selection_mask, 0, sizeof(fb->selection_mask));
    fb->search_query[0] = '\0';
    strncpy(fb->current_path, start_path, MAX_PATH - 1);
    fb->selected_index = 0;
    return filebrowser_refresh(fb);
}

int filebrowser_refresh(FileBrowser *fb) {
    fb->file_count = 0;
    memset(fb->selection_mask, 0, sizeof(fb->selection_mask));

    if (strcmp(fb->current_path, "/") == 0) {
        const char *partitions[] = {"app0:", "gro0:", "grw0:", "sd0:", "uma0:", "ur0:", "ux0:", "vd0:", "vs0:"};
        int num_partitions = sizeof(partitions) / sizeof(partitions[0]);

        for (int i = 0; i < num_partitions; i++) {
            SceIoStat stat;
            if (sceIoGetstat(partitions[i], &stat) >= 0 && SCE_S_ISDIR(stat.st_mode)) {
                if (fb->file_count < MAX_FILES) {
                    strncpy(fb->files[fb->file_count].name, partitions[i], 255);
                    fb->files[fb->file_count].is_directory = 1;
                    fb->files[fb->file_count].size = 0;
                    fb->file_count++;
                }
            }
        }
        
        if (fb->selected_index >= fb->file_count) {
            fb->selected_index = fb->file_count > 0 ? fb->file_count - 1 : 0;
        }
        return 0;
    }
    
    SceUID dfd = sceIoDopen(fb->current_path);
    if (dfd < 0) return -1;
    
    SceIoDirent dir;
    while (sceIoDread(dfd, &dir) > 0 && fb->file_count < MAX_FILES) {
        if (strcmp(dir.d_name, ".") == 0 || strcmp(dir.d_name, "..") == 0) continue;
        if (fb->search_query[0] && !stristr(dir.d_name, fb->search_query)) continue;
        
        strncpy(fb->files[fb->file_count].name, dir.d_name, 255);
        fb->files[fb->file_count].is_directory = SCE_S_ISDIR(dir.d_stat.st_mode);
        fb->files[fb->file_count].size = dir.d_stat.st_size;
        fb->file_count++;
    }
    
    sceIoDclose(dfd);
    
    for (int i = 0; i < fb->file_count - 1; i++) {
        for (int j = i + 1; j < fb->file_count; j++) {
            int should_swap = 0;
            if (fb->files[i].is_directory < fb->files[j].is_directory) should_swap = 1;
            else if (fb->files[i].is_directory == fb->files[j].is_directory && strcasecmp(fb->files[i].name, fb->files[j].name) > 0) should_swap = 1;
            if (should_swap) {
                FileInfo tmp = fb->files[i];
                fb->files[i] = fb->files[j];
                fb->files[j] = tmp;
            }
        }
    }
    
    if (fb->selected_index >= fb->file_count) {
        fb->selected_index = fb->file_count > 0 ? fb->file_count - 1 : 0;
    }
    
    return 0;
}

void filebrowser_navigate_up(FileBrowser *fb) {
    if (fb->selected_index > 0) {
        fb->selected_index--;
    } else if (fb->file_count > 0) {
        fb->selected_index = fb->file_count - 1;
    }
}

int filebrowser_navigate_down(FileBrowser *fb) {
    if (fb->selected_index < fb->file_count - 1) {
        fb->selected_index++;
        return 1;
    } else if (fb->file_count > 0) {
        fb->selected_index = 0;
        return 1;
    }
    return 0;
}

int filebrowser_enter(FileBrowser *fb) {
    if (fb->file_count == 0) return -1;
    
    FileInfo *selected = &fb->files[fb->selected_index];
    
    if (selected->is_directory) {
        if (strlen(fb->current_path) + strlen(selected->name) + 2 > MAX_PATH) {
            return -1;
        }

        if (strcmp(fb->current_path, "/") == 0) {
            snprintf(fb->current_path, MAX_PATH, "%s/", selected->name);
        } else {
            char new_path[MAX_PATH];
            strncpy(new_path, fb->current_path, MAX_PATH - 1);
            new_path[MAX_PATH - 1] = '\0';
            strncat(new_path, selected->name, MAX_PATH - strlen(new_path) - 2);
            strncat(new_path, "/", MAX_PATH - strlen(new_path) - 1);
            strncpy(fb->current_path, new_path, MAX_PATH - 1);
        }
        fb->current_path[MAX_PATH - 1] = '\0';

        fb->selected_index = 0;
        return filebrowser_refresh(fb);
    }
    
    return 1;
}

void filebrowser_navigate_back(FileBrowser *fb) {
    size_t len = strlen(fb->current_path);
    if (len <= 1) return;

    if (len > 2 && fb->current_path[len - 1] == '/' && strchr(fb->current_path, '/') == fb->current_path + len - 1) {
        strcpy(fb->current_path, "/");
        fb->selected_index = 0;
        filebrowser_refresh(fb);
    } else if (len > 1) {
        fb->current_path[len - 1] = '\0';
        char *last_slash = strrchr(fb->current_path, '/');
        if (last_slash) *(last_slash + 1) = '\0';
        fb->selected_index = 0;
        filebrowser_refresh(fb);
    }
}

const char *filebrowser_get_selected_path(FileBrowser *fb) {
    static char full_path[MAX_PATH];
    if (fb->file_count == 0) return NULL;
    
    if (strlen(fb->current_path) + strlen(fb->files[fb->selected_index].name) + 1 > MAX_PATH) {
        return NULL;
    }

    if (strcmp(fb->current_path, "/") == 0) {
        snprintf(full_path, sizeof(full_path), "%s", fb->files[fb->selected_index].name);
    } else {
        strncpy(full_path, fb->current_path, MAX_PATH - 1);
        full_path[MAX_PATH - 1] = '\0';
        strncat(full_path, fb->files[fb->selected_index].name, MAX_PATH - strlen(full_path) - 1);
    }

    return full_path;
}

static void search_recursive(const char *root_path, const char *query, FileBrowser *fb) {
    if (fb->file_count >= MAX_FILES) return;

    SceUID dfd = sceIoDopen(root_path);
    if (dfd < 0) return;

    SceIoDirent dir;
    char path[MAX_PATH];

    while (sceIoDread(dfd, &dir) > 0 && fb->file_count < MAX_FILES) {
        if (strcmp(dir.d_name, ".") == 0 || strcmp(dir.d_name, "..") == 0) continue;

        snprintf(path, sizeof(path), "%s%s", root_path, dir.d_name);

        int match = (stristr(dir.d_name, query) != NULL);
        if (match) {
            snprintf(fb->files[fb->file_count].name, sizeof(fb->files[fb->file_count].name), "%s%s", root_path + strlen(fb->current_path), dir.d_name);
            fb->files[fb->file_count].is_directory = SCE_S_ISDIR(dir.d_stat.st_mode);
            fb->files[fb->file_count].size = dir.d_stat.st_size;
            fb->file_count++;
        }

        if (SCE_S_ISDIR(dir.d_stat.st_mode)) {
            char next_root[MAX_PATH];
            snprintf(next_root, sizeof(next_root), "%s%s/", root_path, dir.d_name);
            search_recursive(next_root, query, fb);
        }
    }
    sceIoDclose(dfd);
}

int filebrowser_search(FileBrowser *fb, const char *query, int global_search) {
    if (!query || strlen(query) == 0) {
        fb->search_query[0] = '\0';
        return filebrowser_refresh(fb);
    }

    fb->search_query[0] = '\0';
    fb->file_count = 0;
    memset(fb->selection_mask, 0, sizeof(fb->selection_mask));

    if (global_search) {
        if (strcmp(fb->current_path, "/") == 0) {
            const char *partitions[] = {"ux0:", "ur0:", "uma0:", "sd0:", "gro0:", "grw0:"};
            int num_partitions = sizeof(partitions) / sizeof(partitions[0]);
            for (int i = 0; i < num_partitions; i++) {
                char root_with_slash[64];
                snprintf(root_with_slash, sizeof(root_with_slash), "%s/", partitions[i]);
                SceIoStat stat;
                if (sceIoGetstat(partitions[i], &stat) >= 0 && SCE_S_ISDIR(stat.st_mode)) {
                    search_recursive(root_with_slash, query, fb);
                }
            }
        } else {
            search_recursive(fb->current_path, query, fb);
        }
    } else {
        char start_path[MAX_PATH];
        if (fb->selected_index >= 0 && fb->selected_index < fb->file_count && fb->files[fb->selected_index].is_directory) {
            if (strcmp(fb->current_path, "/") == 0) {
                snprintf(start_path, sizeof(start_path), "%s/", fb->files[fb->selected_index].name);
            } else {
                strncpy(start_path, fb->current_path, sizeof(start_path) - 1);
                start_path[sizeof(start_path) - 1] = '\0';
                int len = strlen(start_path);
                if (len > 0 && start_path[len - 1] != '/') {
                    strncat(start_path, "/", sizeof(start_path) - len - 1);
                }
                strncat(start_path, fb->files[fb->selected_index].name, sizeof(start_path) - strlen(start_path) - 1);
                strncat(start_path, "/", sizeof(start_path) - strlen(start_path) - 1);
            }
        } else {
            strncpy(start_path, fb->current_path, sizeof(start_path) - 1);
            start_path[sizeof(start_path) - 1] = '\0';
            int len = strlen(start_path);
            if (len > 0 && start_path[len - 1] != '/') {
                strncat(start_path, "/", sizeof(start_path) - len - 1);
            }
        }
        search_recursive(start_path, query, fb);
    }

    fb->selected_index = 0;
    return 0;
}

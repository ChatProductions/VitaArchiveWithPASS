/*
 * VitaArchiveWithPASS - native UnRAR backend for PlayStation Vita.
 * Based on VitaArchive by theheroGAC.
 *
 * Uses VitaSDK's existing UnRAR extraction library (libunrar.a, unrar.h).
 * UnRAR is distributed under its own license; this project only calls its API.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <psp2/io/fcntl.h>

#ifndef _UNIX
#define _UNIX
#endif
#include <unrar.h>

#include "rar.h"
#include "zip.h"

extern volatile uint64_t worker_processed_bytes;

/* Keep VitaArchive's existing negative result convention. */
#define VA_RAR_ERROR (-1)
#define VA_RAR_DIRECTORY (-2)
#define VA_RAR_WRITE_ERROR (-3)
#define VA_RAR_PASSWORD (-5)
#define VA_RAR_UNSAFE_PATH (-6)

static int rar_result(int code) {
    if (code == ERAR_BAD_PASSWORD || code == ERAR_MISSING_PASSWORD)
        return VA_RAR_PASSWORD;
    return VA_RAR_ERROR;
}

static int CALLBACK vita_rar_callback(UINT msg, LPARAM user, LPARAM p1, LPARAM p2) {
    ArchiveInfo *info = (ArchiveInfo *)(uintptr_t)user;
    if (!info) return -1;

    if (msg == UCM_NEEDPASSWORD) {
        size_t length = strlen(info->password);
        if (!length || p2 <= 0 || length >= (size_t)p2) return -1;
        memcpy((void *)(uintptr_t)p1, info->password, length + 1);
        return 1;
    }
    if (msg == UCM_NEEDPASSWORDW) {
        /* The Vita IME normally supplies ASCII or UTF-8. UnRAR's wide
           callback is needed for archives with encrypted file headers. */
        size_t length = strlen(info->password);
        if (!length || p2 <= 0 || length >= (size_t)p2) return -1;
        wchar_t *dst = (wchar_t *)(uintptr_t)p1;
        size_t i;
        for (i = 0; i < length; i++)
            dst[i] = (unsigned char)info->password[i];
        dst[length] = 0;
        return 1;
    }
    if (msg == UCM_PROCESSDATA) {
        if (info->cancel_flag) return -1;
        if (p2 > 0) worker_processed_bytes += (uint64_t)p2;
    }
    if (msg == UCM_CHANGEVOLUME || msg == UCM_CHANGEVOLUMEW ||
        msg == UCM_LARGEDICT) {
        /* No interactive volume chooser or large-dictionary override. */
        return -1;
    }
    return 1;
}

static HANDLE open_unrar(ArchiveInfo *info, int mode, int *result, unsigned int *flags) {
    struct RAROpenArchiveDataEx data;
    memset(&data, 0, sizeof(data));
    data.ArcName = info->archive_path;
    data.OpenMode = (unsigned int)mode;
    data.Callback = vita_rar_callback;
    data.UserData = (LPARAM)(uintptr_t)info;

    HANDLE archive = RAROpenArchiveEx(&data);
    if (!archive || data.OpenResult != ERAR_SUCCESS) {
        *result = rar_result((int)data.OpenResult);
        if (archive) RARCloseArchive(archive);
        return NULL;
    }
    if (flags) *flags = data.Flags;
    if (info->password[0]) RARSetPassword(archive, info->password);
    RARSetCallback(archive, vita_rar_callback, (LPARAM)(uintptr_t)info);
    *result = 0;
    return archive;
}

/* Reject untrusted archive paths that could escape the destination.
   Normalize DOS separators because RAR filenames can contain them. */
static int safe_member_name(const char *name, char *out, size_t capacity, int is_directory) {
    size_t length, start = 0, i;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return -1;
    length = strlen(name);
    if (length >= capacity || length >= sizeof(((ArchiveFile *)0)->filename))
        return -1;
    for (i = 0; i < length; i++) {
        char c = name[i] == '\\' ? '/' : name[i];
        if (c == ':' || (unsigned char)c < 32) return -1;
        out[i] = c;
    }
    if (is_directory && length && out[length - 1] == '/') length--;
    if (!length) return -1;
    out[length] = '\0';
    for (i = 0; i <= length; i++) {
        if (i == length || out[i] == '/') {
            size_t n = i - start;
            if (n == 0 || (n == 1 && out[start] == '.') ||
                (n == 2 && out[start] == '.' && out[start + 1] == '.'))
                return -1;
            start = i + 1;
        }
    }
    return 0;
}

static int make_output_path(char *out, size_t size, const char *dest, const char *member) {
    size_t dlen;
    int needed;
    if (!dest || !dest[0] || !member) return -1;
    dlen = strlen(dest);
    needed = snprintf(out, size, "%s%s%s", dest,
                      dest[dlen - 1] == '/' ? "" : "/", member);
    return needed >= 0 && (size_t)needed < size ? 0 : -1;
}

static void ensure_parent_dirs(const char *path) {
    char tmp[512];
    size_t n = strlen(path);
    if (n >= sizeof(tmp)) return;
    memcpy(tmp, path, n + 1);
    char *p = strchr(tmp, ':');
    if (!p) return;
    for (p = strchr(p + 1, '/'); p; p = strchr(p + 1, '/')) {
        *p = '\0';
        sceIoMkdir(tmp, 0777);
        *p = '/';
    }
}

int rar_open(const char *rar_path, ArchiveInfo *info) {
    if (!rar_path || !info || strlen(rar_path) >= sizeof(info->archive_path))
        return VA_RAR_ERROR;

    char saved_password[sizeof(info->password)];
    memcpy(saved_password, info->password, sizeof(saved_password));
    saved_password[sizeof(saved_password) - 1] = '\0';
    memset(info, 0, sizeof(*info));
    strncpy(info->archive_path, rar_path, sizeof(info->archive_path) - 1);
    memcpy(info->password, saved_password, sizeof(saved_password));

    int count = rar_list_files(info);
    if (count < 0) return count;
    info->is_open = 1;
    return count;
}

int rar_list_files(ArchiveInfo *info) {
    if (!info) return VA_RAR_ERROR;
    info->file_count = 0;
    info->total_size = 0;
    info->total_compressed_size = 0;
    info->is_encrypted = 0;
    memset(info->files, 0, sizeof(info->files));

    int status;
    unsigned int flags = 0;
    HANDLE arc = open_unrar(info, RAR_OM_LIST, &status, &flags);
    if (!arc) return status;

    if (flags & ROADF_ENCHEADERS) {
        info->is_encrypted = 1;
        if (!info->password[0]) {
            RARCloseArchive(arc);
            return VA_RAR_PASSWORD;
        }
    }

    struct RARHeaderDataEx hdr;
    int count = 0, code;
    while (1) {
        memset(&hdr, 0, sizeof(hdr));
        code = RARReadHeaderEx(arc, &hdr);
        if (code == ERAR_END_ARCHIVE) break;
        if (code != ERAR_SUCCESS) {
            status = rar_result(code);
            goto fail;
        }
        if (info->cancel_flag) {
            status = VA_RAR_ERROR;
            goto fail;
        }
        if (hdr.Flags & RHDF_ENCRYPTED) {
            info->is_encrypted = 1;
            if (!info->password[0]) {
                status = VA_RAR_PASSWORD;
                goto fail;
            }
        }
        if (count == MAX_ARCHIVE_FILES) {
            status = VA_RAR_ERROR;
            goto fail;
        }

        char normalized[256];
        if (safe_member_name(hdr.FileName, normalized, sizeof(normalized), (hdr.Flags & RHDF_DIRECTORY) != 0) < 0) {
            status = VA_RAR_UNSAFE_PATH;
            goto fail;
        }

        ArchiveFile *f = &info->files[count];
        strcpy(f->filename, normalized);
        f->is_directory = (hdr.Flags & RHDF_DIRECTORY) != 0;

        uint64_t unpacked = ((uint64_t)hdr.UnpSizeHigh << 32) | hdr.UnpSize;
        uint64_t packed = ((uint64_t)hdr.PackSizeHigh << 32) | hdr.PackSize;
        f->uncompressed_size = unpacked > SIZE_MAX ? SIZE_MAX : (size_t)unpacked;
        f->compressed_size = packed > SIZE_MAX ? SIZE_MAX : (size_t)packed;
        if (SIZE_MAX - info->total_size < f->uncompressed_size)
            info->total_size = SIZE_MAX;
        else
            info->total_size += f->uncompressed_size;
        if (SIZE_MAX - info->total_compressed_size < f->compressed_size)
            info->total_compressed_size = SIZE_MAX;
        else
            info->total_compressed_size += f->compressed_size;
        count++;

        code = RARProcessFile(arc, RAR_SKIP, NULL, NULL);
        if (code != ERAR_SUCCESS) {
            status = rar_result(code);
            goto fail;
        }
    }
    RARCloseArchive(arc);
    info->file_count = count;
    return count;

fail:
    RARCloseArchive(arc);
    info->file_count = 0;
    return status;
}

static int process_rar(const char *dest, ArchiveInfo *info, int file_index,
                       int test_only, int *progress) {
    if (!info || (file_index >= info->file_count) ||
        (!test_only && (!dest || !dest[0]))) return VA_RAR_ERROR;

    int status;
    HANDLE arc = open_unrar(info, RAR_OM_EXTRACT, &status, NULL);
    if (!arc) return status;

    struct RARHeaderDataEx hdr;
    int index = 0;
    status = 0;
    while (1) {
        memset(&hdr, 0, sizeof(hdr));
        int code = RARReadHeaderEx(arc, &hdr);
        if (code == ERAR_END_ARCHIVE) break;
        if (code != ERAR_SUCCESS) {
            status = rar_result(code);
            break;
        }
        if (info->cancel_flag) {
            status = VA_RAR_ERROR;
            break;
        }
        /* The index must correspond to the list displayed in the UI. */
        if (index >= info->file_count) break;
        char normalized[256];
        if (safe_member_name(hdr.FileName, normalized, sizeof(normalized)) < 0) {
            status = VA_RAR_UNSAFE_PATH;
            break;
        }
        int selected = file_index < 0 || file_index == index;
        if (!selected) {
            code = RARProcessFile(arc, RAR_SKIP, NULL, NULL);
        } else if (test_only) {
            code = RARProcessFile(arc, RAR_TEST, NULL, NULL);
        } else {
            char path[512];
            if (make_output_path(path, sizeof(path), dest, normalized) < 0) {
                status = VA_RAR_UNSAFE_PATH;
                break;
            }
            if (hdr.Flags & RHDF_DIRECTORY) {
                ensure_parent_dirs(path);
                sceIoMkdir(path, 0777);
                code = RARProcessFile(arc, RAR_SKIP, NULL, NULL);
            } else {
                ensure_parent_dirs(path);
                /* Supply an explicit, validated output path. Never let
                   UnRAR choose a destination from the raw archive name. */
                code = RARProcessFile(arc, RAR_EXTRACT, NULL, path);
            }
        }
        if (code != ERAR_SUCCESS) {
            status = rar_result(code);
            break;
        }
        index++;
        if (progress && info->file_count)
            *progress = (index * 100) / info->file_count;
        if (file_index >= 0 && file_index == index - 1) break;
    }
    RARCloseArchive(arc);
    return status;
}

int rar_extract_all(const char *dest, ArchiveInfo *info, int *progress) {
    return process_rar(dest, info, -1, 0, progress);
}

int rar_extract_file(const char *dest, int file_index, ArchiveInfo *info) {
    if (!info || file_index < 0 || file_index >= info->file_count)
        return VA_RAR_ERROR;
    if (info->files[file_index].is_directory) return VA_RAR_DIRECTORY;
    return process_rar(dest, info, file_index, 0, NULL);
}

int rar_test_integrity(ArchiveInfo *info, int *progress) {
    return process_rar(NULL, info, -1, 1, progress);
}

void rar_cancel(ArchiveInfo *info) {
    if (info) info->cancel_flag = 1;
}

void rar_set_password(ArchiveInfo *info, const char *password) {
    if (!info || !password) return;
    strncpy(info->password, password, sizeof(info->password) - 1);
    info->password[sizeof(info->password) - 1] = '\0';
}

void rar_close(ArchiveInfo *info) {
    if (info) info->is_open = 0;
}

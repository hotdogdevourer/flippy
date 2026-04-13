/*
 * MIT License
 * 
 * Copyright (c) 2026 Flippy Contributors
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * */

/*
 * Flippy - A command-line tool to manage and/or create FAT12 floppy disks & ISO 9660 CD/DVD images!
 * main.c - Create and manage FAT12 floppy and ISO 9660 CD/DVD images.
 *
 * This program provides a command-line interface to:
 *  - Create standard 160KB, 180KB, 320KB, 360KB, 720KB, 1.44MB, or 2.88MB FAT12 floppy disk images.
 *  - Add, extract, list, delete files and directories from a FAT12 image.
 *  - Create an ISO 9660 Level 1 filesystem image from a local directory.
 *  - List, extract, and delete files and directories from an ISO 9660 image.
 *
 * Written to be mostly C89 compatible, with the use of 'stdint.h'
 * being the primary C99 feature.
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define strcasecmp _stricmp
#define MKDIR(path) _mkdir(path)
#define STAT _stat
#define STAT_STRUCT struct _stat
#else
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#define MKDIR(path) mkdir(path, 0755)
#define STAT stat
#define STAT_STRUCT struct stat
#endif

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef signed int     int32_t;
#else
#include <stdint.h>
#endif

#ifdef _MSC_VER
#pragma pack(push, 1)
#define PACKED
#else
#define PACKED __attribute__((packed))
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf _snprintf
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32
struct dirent {
    char d_name[260];
    int d_type;
};
typedef struct {
    HANDLE hFind;
    WIN32_FIND_DATAA findData;
    struct dirent entry;
    int first;
    char dirname[PATH_MAX];
} DIR;

static DIR *opendir(const char *name) {
    DIR *dir;
    char pattern[PATH_MAX + 3];
    snprintf(pattern, sizeof(pattern), "%s\\*", name);
    dir = (DIR*)malloc(sizeof(DIR));
    if (!dir) return NULL;
    dir->hFind = FindFirstFileA(pattern, &dir->findData);
    if (dir->hFind == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }
    dir->first = 1;
    strncpy(dir->dirname, name, PATH_MAX - 1);
    dir->dirname[PATH_MAX - 1] = '\0';
    return dir;
}

static struct dirent *readdir(DIR *dir) {
    if (!dir) return NULL;
    if (dir->first) {
        dir->first = 0;
    } else {
        if (!FindNextFileA(dir->hFind, &dir->findData)) return NULL;
    }
    strcpy(dir->entry.d_name, dir->findData.cFileName);
    dir->entry.d_type = (dir->findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 4 : 8;
    return &dir->entry;
}

static void rewinddir(DIR *dir) {
    if (!dir) return;
    FindClose(dir->hFind);
    char pattern[PATH_MAX+3];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir->dirname);
    dir->hFind = FindFirstFileA(pattern, &dir->findData);
    dir->first = 1;
}

static int closedir(DIR *dir) {
    if (dir) { FindClose(dir->hFind); free(dir); }
    return 0;
}
#endif

#define HOST_TO_LE16(x) (x)
#define HOST_TO_LE32(x) (x)
#define LE16_TO_HOST(x) (x)
#define LE32_TO_HOST(x) (x)

#define SECTOR_SIZE         512
#define FAT12_RESERVED      1
#define FAT12_NUM_FATS      2
#define ISO_BLOCK_SIZE      2048
#define ISO_SYSTEM_AREA     16
#define ISO_VOLUME_DESC_LEN 1

#define FAT12_ATTR_READONLY 0x01
#define FAT12_ATTR_HIDDEN   0x02
#define FAT12_ATTR_SYSTEM   0x04
#define FAT12_ATTR_VOLUME   0x08
#define FAT12_ATTR_DIRECTORY 0x10
#define FAT12_ATTR_ARCHIVE  0x20

typedef int floppy_size_t;
#define FLOPPY_160KB   160
#define FLOPPY_180KB   180
#define FLOPPY_320KB   320
#define FLOPPY_360KB   360
#define FLOPPY_720KB   720
#define FLOPPY_1440KB 1440
#define FLOPPY_2880KB 2880

typedef struct {
    floppy_size_t size_kb;
    uint16_t total_sectors;
    uint8_t  sectors_per_cluster;
    uint16_t sectors_per_fat;
    uint16_t root_entries;
    uint8_t  media_descriptor;
    uint8_t  sectors_per_track;
    uint8_t  num_heads;
} floppy_params_t;

static const floppy_params_t floppy_formats[] = {
    { FLOPPY_160KB,   320, 1, 1,  64, 0xFE,  8, 1 },
    { FLOPPY_180KB,   360, 1, 2,  64, 0xFC,  9, 1 },
    { FLOPPY_320KB,   640, 2, 1, 112, 0xFF,  8, 2 },
    { FLOPPY_360KB,   720, 2, 2, 112, 0xFD,  9, 2 },
    { FLOPPY_720KB,  1440, 2, 3, 112, 0xF9,  9, 2 },
    { FLOPPY_1440KB, 2880, 1, 9, 224, 0xF0, 18, 2 },
    { FLOPPY_2880KB, 5760, 2, 9, 240, 0xF0, 36, 2 }
};

typedef struct {
    uint8_t  jump[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors;
    uint8_t  media_descriptor;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t large_sectors;
    uint8_t  drive_number;
    uint8_t  reserved;
    uint8_t  signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  filesystem_type[8];
} PACKED fat12_boot_t;

typedef struct {
    uint8_t  filename[11];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  creation_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t last_write_time;
    uint16_t last_write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} PACKED fat12_dirent_t;

typedef struct {
    uint8_t  record_len;
    uint8_t  ext_attr_len;
    uint8_t  extent_loc[8];
    uint8_t  data_len[8];
    uint8_t  rec_date[7];
    uint8_t  flags;
    uint8_t  file_unit_size;
    uint8_t  interleave_gap;
    uint8_t  vol_seq_num[4];
    uint8_t  name_len;
    uint8_t  name[1];
} PACKED iso_dir_rec_t;

typedef struct {
    uint8_t  type;
    uint8_t  id[5];
    uint8_t  version;
    uint8_t  data[2041];
} PACKED iso_pvd_t;

typedef struct {
    uint8_t year[4];
    uint8_t month[2];
    uint8_t day[2];
    uint8_t hour[2];
    uint8_t minute[2];
    uint8_t second[2];
    uint8_t hundredths[2];
    uint8_t gmt_offset;
} PACKED iso_datetime_t;

#define READ_ISO_32LE(ptr) LE32_TO_HOST( (uint32_t)(ptr)[0] | ((uint32_t)(ptr)[1] << 8) | \
                                         ((uint32_t)(ptr)[2] << 16) | ((uint32_t)(ptr)[3] << 24) )
#define WRITE_ISO_32LE(ptr, val) do { \
    uint32_t v = HOST_TO_LE32(val); \
    (ptr)[0] = (v) & 0xFF; \
    (ptr)[1] = ((v) >> 8) & 0xFF; \
    (ptr)[2] = ((v) >> 16) & 0xFF; \
    (ptr)[3] = ((v) >> 24) & 0xFF; \
} while(0)

static void fill_digits(uint8_t *dest, int value, int width) {
    char buf[12];
    int i, len;
    snprintf(buf, sizeof(buf), "%0*d", width, value);
    len = (int)strlen(buf);
    for (i = 0; i < width; i++)
        dest[i] = (i < len) ? (uint8_t)buf[i] : '0';
}

static void make_fat12_name(const char *src, char *dest) {
    int i, j;
    const char *dot = strrchr(src, '.');
    if (!dot) dot = src + strlen(src);
    for (i = 0; i < 8 && src < dot; i++, src++)
        dest[i] = (uint8_t)toupper((unsigned char)*src);
    for (; i < 8; i++) dest[i] = ' ';
    if (dot && *dot == '.') dot++;
    for (j = 0; j < 3; j++)
        dest[8 + j] = (dot && *dot) ? (uint8_t)toupper((unsigned char)*(dot++)) : ' ';
}

static void fat12_name_to_str(const uint8_t fatname[11], char *out) {
    int i, j;
    for (i = 0; i < 8 && fatname[i] != ' '; i++)
        out[i] = fatname[i];
    if (fatname[8] != ' ') {
        out[i++] = '.';
        for (j = 8; j < 11 && fatname[j] != ' '; j++)
            out[i++] = fatname[j];
    }
    out[i] = '\0';
}

static void get_fat_time(uint16_t *fat_time, uint16_t *fat_date) {
    time_t t = time(NULL);
    struct tm *tm;
#ifdef _MSC_VER
    struct tm tm_buf;
    localtime_s(&tm_buf, &t);
    tm = &tm_buf;
#else
    tm = localtime(&t);
#endif
    *fat_time = (uint16_t)((tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec / 2));
    *fat_date = (uint16_t)(((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday);
}

static void fat12_set_entry(uint8_t *fat, uint16_t cluster, uint16_t value) {
    uint32_t offset = cluster * 3 / 2;
    if (cluster % 2 == 0) {
        fat[offset] = value & 0xFF;
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F);
    } else {
        fat[offset] = (fat[offset] & 0x0F) | ((value & 0x0F) << 4);
        fat[offset + 1] = (value >> 4) & 0xFF;
    }
}

static uint16_t fat12_get_entry(const uint8_t *fat, uint16_t cluster) {
    uint32_t offset = cluster * 3 / 2;
    if (cluster % 2 == 0)
        return (uint16_t)(fat[offset] | ((fat[offset + 1] & 0x0F) << 8));
    else
        return (uint16_t)(((fat[offset] & 0xF0) >> 4) | (fat[offset + 1] << 4));
}

static const floppy_params_t* get_floppy_params(floppy_size_t size) {
    int i;
    int n = (int)(sizeof(floppy_formats) / sizeof(floppy_formats[0]));
    for (i = 0; i < n; i++)
        if (floppy_formats[i].size_kb == size) return &floppy_formats[i];
    return NULL;
}

static void fat12_init_boot_sector(fat12_boot_t *boot, const floppy_params_t *params) {
    memset(boot, 0, sizeof(fat12_boot_t));
    boot->jump[0] = 0xEB; boot->jump[1] = 0x3C; boot->jump[2] = 0x90;
    memcpy(boot->oem_name, "MSDOS5.0", 8);
    boot->bytes_per_sector = HOST_TO_LE16(SECTOR_SIZE);
    boot->sectors_per_cluster = params->sectors_per_cluster;
    boot->reserved_sectors = HOST_TO_LE16(FAT12_RESERVED);
    boot->num_fats = FAT12_NUM_FATS;
    boot->root_entries = HOST_TO_LE16(params->root_entries);
    boot->total_sectors = HOST_TO_LE16(params->total_sectors);
    boot->media_descriptor = params->media_descriptor;
    boot->sectors_per_fat = HOST_TO_LE16(params->sectors_per_fat);
    boot->sectors_per_track = HOST_TO_LE16(params->sectors_per_track);
    boot->num_heads = HOST_TO_LE16(params->num_heads);
    boot->signature = 0x29;
    boot->volume_id = HOST_TO_LE32((uint32_t)time(NULL));
    memcpy(boot->volume_label, "NO NAME    ", 11);
    memcpy(boot->filesystem_type, "FAT12   ", 8);
    boot->drive_number = 0x00;
}

static int fat12_read_geometry(FILE *fp, floppy_params_t *params,
                               uint32_t *fat_offset, uint32_t *root_offset,
                               uint32_t *data_offset, uint16_t *data_clusters) {
    fat12_boot_t boot;
    rewind(fp);
    if (fread(&boot, 1, sizeof(boot), fp) != sizeof(boot)) return -1;
    params->total_sectors = LE16_TO_HOST(boot.total_sectors);
    params->sectors_per_cluster = boot.sectors_per_cluster;
    params->sectors_per_fat = LE16_TO_HOST(boot.sectors_per_fat);
    params->root_entries = LE16_TO_HOST(boot.root_entries);
    params->media_descriptor = boot.media_descriptor;
    params->sectors_per_track = LE16_TO_HOST(boot.sectors_per_track);
    params->num_heads = LE16_TO_HOST(boot.num_heads);
    *fat_offset = LE16_TO_HOST(boot.reserved_sectors) * SECTOR_SIZE;
    *root_offset = *fat_offset + (boot.num_fats * params->sectors_per_fat * SECTOR_SIZE);
    *data_offset = *root_offset + (params->root_entries * 32);
    *data_clusters = (uint16_t)((params->total_sectors - (*data_offset / SECTOR_SIZE)) / params->sectors_per_cluster);
    return 0;
}

static int fat12_create(const char *filename, floppy_size_t size_kb) {
    FILE *fp;
    uint8_t *image;
    fat12_boot_t *boot;
    uint8_t *fat;
    size_t image_size, i;
    const floppy_params_t *params = get_floppy_params(size_kb);
    uint16_t data_clusters;
    uint32_t fat_offset, root_offset, data_offset;

    if (!params) {
        fprintf(stderr, "Unsupported floppy size: %d KB\n", size_kb);
        return -1;
    }

    image_size = params->total_sectors * SECTOR_SIZE;
    image = (uint8_t*)calloc(1, image_size);
    if (!image) {
        fprintf(stderr, "Failed to allocate memory.\n");
        return -1;
    }

    fat_offset = FAT12_RESERVED * SECTOR_SIZE;
    root_offset = fat_offset + (FAT12_NUM_FATS * params->sectors_per_fat * SECTOR_SIZE);
    data_offset = root_offset + (params->root_entries * 32);
    data_clusters = (uint16_t)((params->total_sectors - (data_offset / SECTOR_SIZE)) / params->sectors_per_cluster);

    boot = (fat12_boot_t*)image;
    fat12_init_boot_sector(boot, params);

    fat = image + fat_offset;
    fat12_set_entry(fat, 0, (uint16_t)(0xFF0 | params->media_descriptor));
    fat12_set_entry(fat, 1, 0xFFF);
    for (i = 2; i < (size_t)data_clusters + 2; i++)
        fat12_set_entry(fat, (uint16_t)i, 0x000);
    memcpy(image + fat_offset + (params->sectors_per_fat * SECTOR_SIZE), fat,
           params->sectors_per_fat * SECTOR_SIZE);

    image[510] = 0x55;
    image[511] = 0xAA;

    fp = fopen(filename, "wb");
    if (!fp) { perror("fopen"); free(image); return -1; }
    if (fwrite(image, 1, image_size, fp) != image_size) {
        perror("fwrite"); fclose(fp); free(image); return -1;
    }
    fclose(fp); free(image);
    printf("Created %dKB FAT12 image: %s\n", size_kb, filename);
    return 0;
}

static uint16_t fat12_find_free_cluster(const uint8_t *fat, uint16_t data_clusters) {
    uint16_t i;
    for (i = 2; i < data_clusters + 2; i++)
        if (fat12_get_entry(fat, i) == 0x000) return i;
    return 0xFFFF;
}

static uint8_t* fat12_load_directory(FILE *fp, const floppy_params_t *params,
                                     uint32_t fat_offset,
                                     uint32_t root_offset,
                                     uint32_t data_offset, const uint8_t *fat,
                                     uint16_t dir_cluster, size_t *dir_size_out) {
    (void)fat_offset;
    uint8_t *buffer;
    size_t dir_size;
    if (dir_cluster == 0) {
        dir_size = params->root_entries * 32;
        buffer = (uint8_t*)malloc(dir_size);
        fseek(fp, root_offset, SEEK_SET);
        fread(buffer, 1, dir_size, fp);
    } else {
        dir_size = 0;
        uint16_t clus = dir_cluster;
        uint32_t cluster_bytes = params->sectors_per_cluster * SECTOR_SIZE;
        while (clus < 0xFF0) {
            dir_size += cluster_bytes;
            clus = fat12_get_entry(fat, clus);
        }
        buffer = (uint8_t*)malloc(dir_size);
        uint8_t *ptr = buffer;
        clus = dir_cluster;
        while (clus < 0xFF0) {
            fseek(fp, data_offset + (clus - 2) * cluster_bytes, SEEK_SET);
            fread(ptr, 1, cluster_bytes, fp);
            ptr += cluster_bytes;
            clus = fat12_get_entry(fat, clus);
        }
    }
    *dir_size_out = dir_size;
    return buffer;
}

static void fat12_save_directory(FILE *fp, const floppy_params_t *params,
                                 uint32_t root_offset, uint32_t data_offset,
                                 uint8_t *fat, uint16_t dir_cluster,
                                 const uint8_t *buffer, size_t dir_size) {
    if (dir_cluster == 0) {
        fseek(fp, root_offset, SEEK_SET);
        fwrite(buffer, 1, dir_size, fp);
    } else {
        const uint8_t *ptr = buffer;
        uint16_t clus = dir_cluster;
        uint32_t cluster_bytes = params->sectors_per_cluster * SECTOR_SIZE;
        size_t remaining = dir_size;
        while (clus < 0xFF0) {
            size_t to_write = (remaining < cluster_bytes) ? remaining : cluster_bytes;
            fseek(fp, data_offset + (clus - 2) * cluster_bytes, SEEK_SET);
            fwrite(ptr, 1, to_write, fp);
            ptr += cluster_bytes;
            remaining -= to_write;
            clus = fat12_get_entry(fat, clus);
        }
    }
}

static fat12_dirent_t* fat12_find_free_entry(uint8_t *dir_buffer, size_t dir_size) {
    for (size_t off = 0; off < dir_size; off += 32) {
        fat12_dirent_t *e = (fat12_dirent_t*)(dir_buffer + off);
        if (e->filename[0] == 0x00 || e->filename[0] == 0xE5)
            return e;
    }
    return NULL;
}

static uint16_t fat12_find_path(FILE *fp, const floppy_params_t *params,
                                uint32_t fat_offset, uint32_t root_offset,
                                uint32_t data_offset, const uint8_t *fat,
                                const char *path, fat12_dirent_t *entry_out) {
    const char *p = path;
    uint16_t current_cluster = 0;                
    uint8_t *dir_buffer = NULL;
    size_t dir_size;

    if (path == NULL || *path == '\0') return 0;

    while (*p) {
        while (*p == '/' || *p == '\\') p++;
        if (*p == '\0') break;

        char component[256];
        int ci = 0;
        while (*p && *p != '/' && *p != '\\' && ci < 255) {
            component[ci++] = *p++;
        }
        component[ci] = '\0';

        char fat_component[11];
        make_fat12_name(component, fat_component);

        dir_buffer = fat12_load_directory(fp, params, fat_offset, root_offset,
                                          data_offset, fat, current_cluster, &dir_size);

        fat12_dirent_t *entry = NULL;
        int found = 0;
        for (size_t off = 0; off < dir_size; off += 32) {
            entry = (fat12_dirent_t*)(dir_buffer + off);
            if (entry->filename[0] == 0x00) break;
            if (entry->filename[0] == 0xE5) continue;
            if (memcmp(entry->filename, fat_component, 11) == 0) {
                found = 1;
                break;
            }
        }

        if (!found) {
            free(dir_buffer);
            return 0xFFFF;
        }

        if ((entry->attributes & FAT12_ATTR_DIRECTORY) && *p != '\0') {
            current_cluster = LE16_TO_HOST(entry->first_cluster_lo);
            free(dir_buffer);
        } else {
            if (entry_out) memcpy(entry_out, entry, sizeof(fat12_dirent_t));
            free(dir_buffer);
            return (entry->attributes & FAT12_ATTR_DIRECTORY)
                       ? LE16_TO_HOST(entry->first_cluster_lo)
                       : 0xFFFE;
        }
    }
    return current_cluster;
}

static int fat12_create_subdir(FILE *fp, const floppy_params_t *params,
                               uint32_t fat_offset, uint32_t root_offset,
                               uint32_t data_offset, uint8_t *fat,
                               uint16_t parent_cluster, const char *name,
                               uint16_t data_clusters) {
    size_t dir_size;
    uint8_t *dir_buffer = fat12_load_directory(fp, params, fat_offset, root_offset,
                                               data_offset, fat, parent_cluster, &dir_size);
    fat12_dirent_t *free_entry = fat12_find_free_entry(dir_buffer, dir_size);
    if (!free_entry) { free(dir_buffer); return -1; }

    uint16_t new_cluster = fat12_find_free_cluster(fat, data_clusters);
    if (new_cluster == 0xFFFF) { free(dir_buffer); return -1; }
    fat12_set_entry(fat, new_cluster, 0xFFF);

    memset(free_entry, 0, sizeof(fat12_dirent_t));
    memcpy(free_entry->filename, name, 11);
    free_entry->attributes = FAT12_ATTR_DIRECTORY;
    uint16_t ftime, fdate;
    get_fat_time(&ftime, &fdate);
    free_entry->last_write_time = HOST_TO_LE16(ftime);
    free_entry->last_write_date = HOST_TO_LE16(fdate);
    free_entry->creation_time = free_entry->last_write_time;
    free_entry->creation_date = free_entry->last_write_date;
    free_entry->first_cluster_lo = HOST_TO_LE16(new_cluster);
    uint32_t zero = 0;
    memcpy(&free_entry->file_size, &zero, sizeof(uint32_t));

    fat12_save_directory(fp, params, root_offset, data_offset, fat,
                         parent_cluster, dir_buffer, dir_size);
    free(dir_buffer);

    uint32_t cluster_bytes = params->sectors_per_cluster * SECTOR_SIZE;
    uint8_t *cluster_buf = (uint8_t*)calloc(1, cluster_bytes);
    if (!cluster_buf) return -1;
    fat12_dirent_t *dot = (fat12_dirent_t*)cluster_buf;
    fat12_dirent_t *dotdot = (fat12_dirent_t*)(cluster_buf + 32);

    memset(dot->filename, ' ', 11);
    dot->filename[0] = '.';
    dot->attributes = FAT12_ATTR_DIRECTORY;
    dot->first_cluster_lo = HOST_TO_LE16(new_cluster);
    get_fat_time(&ftime, &fdate);
    dot->last_write_time = HOST_TO_LE16(ftime);
    dot->last_write_date = HOST_TO_LE16(fdate);

    memset(dotdot->filename, ' ', 11);
    dotdot->filename[0] = '.'; dotdot->filename[1] = '.';
    dotdot->attributes = FAT12_ATTR_DIRECTORY;
    dotdot->first_cluster_lo = HOST_TO_LE16(parent_cluster);
    dotdot->last_write_time = dot->last_write_time;
    dotdot->last_write_date = dot->last_write_date;

    fseek(fp, data_offset + (new_cluster - 2) * cluster_bytes, SEEK_SET);
    fwrite(cluster_buf, 1, cluster_bytes, fp);
    free(cluster_buf);
    return 0;
}

static int fat12_add_file_to_dir(FILE *img_fp, const floppy_params_t *params,
                                 uint32_t fat_offset, uint32_t root_offset,
                                 uint32_t data_offset, uint8_t *fat,
                                 uint16_t data_clusters, uint16_t dir_cluster,
                                 const char *src_file, const char *dest_name);

static int fat12_add_directory_recursive(FILE *img_fp, const floppy_params_t *params,
                                         uint32_t fat_offset, uint32_t root_offset,
                                         uint32_t data_offset, uint8_t *fat,
                                         uint16_t data_clusters, uint16_t parent_cluster,
                                         const char *local_path, const char *dest_path) {
    DIR *dir = opendir(local_path);
    if (!dir) return -1;

    struct dirent *de;
    char full_local[PATH_MAX];
    char fat_name[12];
    uint16_t subdir_cluster;

    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        int written = snprintf(full_local, sizeof(full_local), "%s/%s", local_path, de->d_name);
        if (written >= (int)sizeof(full_local)) {
            fprintf(stderr, "Path too long, skipping: %s/%s\n", local_path, de->d_name);
            continue;
        }
        STAT_STRUCT st;
        if (STAT(full_local, &st) != 0) continue;

        make_fat12_name(de->d_name, fat_name);

        if (st.st_mode & _S_IFDIR) {
            if (fat12_create_subdir(img_fp, params, fat_offset, root_offset,
                                    data_offset, fat, parent_cluster,
                                    fat_name, data_clusters) == 0) {
                size_t dir_size;
                uint8_t *parent_buf = fat12_load_directory(img_fp, params, fat_offset,
                                            root_offset, data_offset, fat,
                                            parent_cluster, &dir_size);
                subdir_cluster = 0xFFFF;
                for (size_t off = 0; off < dir_size; off += 32) {
                    fat12_dirent_t *e = (fat12_dirent_t*)(parent_buf + off);
                    if (memcmp(e->filename, fat_name, 11) == 0) {
                        subdir_cluster = LE16_TO_HOST(e->first_cluster_lo);
                        break;
                    }
                }
                free(parent_buf);
                if (subdir_cluster != 0xFFFF) {
                    char sub_dest[PATH_MAX*2];
                    if (dest_path && *dest_path)
                        snprintf(sub_dest, sizeof(sub_dest), "%s/%s", dest_path, de->d_name);
                    else
                        snprintf(sub_dest, sizeof(sub_dest), "%s", de->d_name);
                    fat12_add_directory_recursive(img_fp, params, fat_offset, root_offset,
                                                  data_offset, fat, data_clusters,
                                                  subdir_cluster, full_local, sub_dest);
                }
            }
        } else if (st.st_mode & _S_IFREG) {
            fat12_add_file_to_dir(img_fp, params, fat_offset, root_offset,
                                  data_offset, fat, data_clusters,
                                  parent_cluster, full_local, fat_name);
        }
    }
    closedir(dir);
    return 0;
}

static int fat12_add_directory(const char *image_path, const char *local_dir,
                               const char *dest_path) {
    FILE *img_fp = fopen(image_path, "r+b");
    if (!img_fp) { perror("fopen image"); return -1; }

    floppy_params_t params;
    uint32_t fat_offset, root_offset, data_offset;
    uint16_t data_clusters;
    if (fat12_read_geometry(img_fp, &params, &fat_offset, &root_offset,
                            &data_offset, &data_clusters) != 0) {
        fprintf(stderr, "Failed to read geometry.\n");
        fclose(img_fp); return -1;
    }

    uint8_t *fat = (uint8_t*)malloc(params.sectors_per_fat * SECTOR_SIZE);
    fseek(img_fp, fat_offset, SEEK_SET);
    fread(fat, 1, params.sectors_per_fat * SECTOR_SIZE, img_fp);

    uint16_t parent_cluster = 0;
    char parent_path[PATH_MAX];
    const char *target_dir = dest_path;
    if (target_dir && *target_dir) {
        strncpy(parent_path, target_dir, PATH_MAX - 1);
        parent_path[PATH_MAX - 1] = '\0';
        char *last_slash = strrchr(parent_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            parent_cluster = fat12_find_path(img_fp, &params, fat_offset, root_offset,
                                             data_offset, fat, parent_path, NULL);
            if (parent_cluster == 0xFFFF) {
                fprintf(stderr, "Destination path '%s' not found.\n", parent_path);
                free(fat); fclose(img_fp); return -1;
            }
            target_dir = last_slash + 1;
        } else {
            target_dir = parent_path;
        }
    }

    char fat_name[12];
    const char *dir_name = target_dir && *target_dir ? target_dir : local_dir;
    if (!target_dir || !*target_dir) {
        const char *slash = strrchr(local_dir, '/');
        if (slash) dir_name = slash + 1;
#ifdef _WIN32
        slash = strrchr(local_dir, '\\');
        if (slash) dir_name = slash + 1;
#endif
    }
    make_fat12_name(dir_name, fat_name);

    if (fat12_create_subdir(img_fp, &params, fat_offset, root_offset, data_offset,
                            fat, parent_cluster, fat_name, data_clusters) != 0) {
        fprintf(stderr, "Failed to create directory '%s'.\n", dir_name);
        free(fat); fclose(img_fp); return -1;
    }

    size_t dir_size;
    uint8_t *parent_buf = fat12_load_directory(img_fp, &params, fat_offset,
                                root_offset, data_offset, fat, parent_cluster, &dir_size);
    uint16_t new_cluster = 0xFFFF;
    for (size_t off = 0; off < dir_size; off += 32) {
        fat12_dirent_t *e = (fat12_dirent_t*)(parent_buf + off);
        if (memcmp(e->filename, fat_name, 11) == 0) {
            new_cluster = LE16_TO_HOST(e->first_cluster_lo);
            break;
        }
    }
    free(parent_buf);
    if (new_cluster == 0xFFFF) {
        fprintf(stderr, "Internal error: cannot find newly created directory.\n");
        free(fat); fclose(img_fp); return -1;
    }

    int ret = fat12_add_directory_recursive(img_fp, &params, fat_offset, root_offset,
                                            data_offset, fat, data_clusters,
                                            new_cluster, local_dir, "");
    fseek(img_fp, fat_offset, SEEK_SET);
    fwrite(fat, 1, params.sectors_per_fat * SECTOR_SIZE, img_fp);
    fseek(img_fp, fat_offset + params.sectors_per_fat * SECTOR_SIZE, SEEK_SET);
    fwrite(fat, 1, params.sectors_per_fat * SECTOR_SIZE, img_fp);

    free(fat);
    fclose(img_fp);
    if (ret == 0)
        printf("Added directory '%s' to image.\n", local_dir);
    return ret;
}

static int fat12_add_file_to_dir(FILE *img_fp, const floppy_params_t *params,
                                 uint32_t fat_offset, uint32_t root_offset,
                                 uint32_t data_offset, uint8_t *fat,
                                 uint16_t data_clusters, uint16_t dir_cluster,
                                 const char *src_file, const char *dest_name) {
    FILE *src_fp = fopen(src_file, "rb");
    if (!src_fp) { perror("fopen src_file"); return -1; }
    fseek(src_fp, 0, SEEK_END);
    long src_size = ftell(src_fp);
    fseek(src_fp, 0, SEEK_SET);

    size_t dir_size;
    uint8_t *dir_buffer = fat12_load_directory(img_fp, params, fat_offset,
                                               root_offset, data_offset, fat,
                                               dir_cluster, &dir_size);
    fat12_dirent_t *free_entry = fat12_find_free_entry(dir_buffer, dir_size);
    if (!free_entry) {
        fprintf(stderr, "Directory full.\n");
        fclose(src_fp); free(dir_buffer); return -1;
    }

    uint32_t cluster_bytes = params->sectors_per_cluster * SECTOR_SIZE;
    uint16_t clusters_needed = (uint16_t)((src_size + cluster_bytes - 1) / cluster_bytes);
    uint16_t first_cluster = fat12_find_free_cluster(fat, data_clusters);
    if (first_cluster == 0xFFFF) {
        fprintf(stderr, "Disk full.\n");
        fclose(src_fp); free(dir_buffer); return -1;
    }
    uint16_t current_cluster = first_cluster;
    fat12_set_entry(fat, current_cluster, 0xFFF);
    for (uint16_t i = 1; i < clusters_needed; i++) {
        uint16_t next_cluster = fat12_find_free_cluster(fat, data_clusters);
        if (next_cluster == 0xFFFF) {
            fprintf(stderr, "Disk full.\n");
            fclose(src_fp); free(dir_buffer); return -1;
        }
        fat12_set_entry(fat, current_cluster, next_cluster);
        current_cluster = next_cluster;
        fat12_set_entry(fat, current_cluster, 0xFFF);
    }

    memset(free_entry, 0, sizeof(fat12_dirent_t));
    memcpy(free_entry->filename, dest_name, 11);
    free_entry->attributes = FAT12_ATTR_ARCHIVE;
    uint16_t ftime, fdate;
    get_fat_time(&ftime, &fdate);
    free_entry->last_write_time = HOST_TO_LE16(ftime);
    free_entry->last_write_date = HOST_TO_LE16(fdate);
    free_entry->creation_time = free_entry->last_write_time;
    free_entry->creation_date = free_entry->last_write_date;
    free_entry->first_cluster_lo = HOST_TO_LE16(first_cluster);
    memcpy(&free_entry->file_size, &src_size, sizeof(uint32_t));

    fat12_save_directory(img_fp, params, root_offset, data_offset, fat,
                         dir_cluster, dir_buffer, dir_size);
    free(dir_buffer);

    uint8_t *buffer = (uint8_t*)malloc(cluster_bytes);
    current_cluster = first_cluster;
    while (src_size > 0) {
        size_t bytes_to_read = (src_size < (long)cluster_bytes) ? (size_t)src_size : cluster_bytes;
        size_t bytes_read = fread(buffer, 1, bytes_to_read, src_fp);
        if (bytes_read == 0) break;
        memset(buffer + bytes_read, 0, cluster_bytes - bytes_read);
        fseek(img_fp, data_offset + (current_cluster - 2) * cluster_bytes, SEEK_SET);
        fwrite(buffer, 1, cluster_bytes, img_fp);
        src_size -= (long)bytes_read;
        if (src_size > 0)
            current_cluster = fat12_get_entry(fat, current_cluster);
    }
    free(buffer);
    fclose(src_fp);
    return 0;
}

static int fat12_add_file(const char *image_path, const char *src_file, const char *dest_name) {
    FILE *img_fp = fopen(image_path, "r+b");
    if (!img_fp) { perror("fopen image"); return -1; }

    floppy_params_t params;
    uint32_t fat_offset, root_offset, data_offset;
    uint16_t data_clusters;
    if (fat12_read_geometry(img_fp, &params, &fat_offset, &root_offset,
                            &data_offset, &data_clusters) != 0) {
        fprintf(stderr, "Failed to read geometry.\n");
        fclose(img_fp); return -1;
    }

    uint8_t *fat = (uint8_t*)malloc(params.sectors_per_fat * SECTOR_SIZE);
    fseek(img_fp, fat_offset, SEEK_SET);
    fread(fat, 1, params.sectors_per_fat * SECTOR_SIZE, img_fp);

    char fat_name[11];
    make_fat12_name(dest_name ? dest_name : src_file, fat_name);
    int ret = fat12_add_file_to_dir(img_fp, &params, fat_offset, root_offset,
                                    data_offset, fat, data_clusters, 0, src_file, fat_name);
    if (ret == 0) {
        fseek(img_fp, fat_offset, SEEK_SET);
        fwrite(fat, 1, params.sectors_per_fat * SECTOR_SIZE, img_fp);
        fseek(img_fp, fat_offset + params.sectors_per_fat * SECTOR_SIZE, SEEK_SET);
        fwrite(fat, 1, params.sectors_per_fat * SECTOR_SIZE, img_fp);
        printf("Added '%s' as '%11.11s'\n", src_file, fat_name);
    }
    free(fat);
    fclose(img_fp);
    return ret;
}

static void fat12_list_recursive(FILE *fp, const floppy_params_t *params,
                                 uint32_t fat_offset, uint32_t root_offset,
                                 uint32_t data_offset, const uint8_t *fat,
                                 uint16_t dir_cluster, const char *prefix) {
    size_t dir_size;
    uint8_t *dir_buffer = fat12_load_directory(fp, params, fat_offset,
                                               root_offset, data_offset, fat,
                                               dir_cluster, &dir_size);
    for (size_t off = 0; off < dir_size; off += 32) {
        fat12_dirent_t *e = (fat12_dirent_t*)(dir_buffer + off);
        if (e->filename[0] == 0x00) break;
        if (e->filename[0] == 0xE5) continue;
        if (e->attributes == 0x0F || (e->attributes & FAT12_ATTR_VOLUME)) continue;

        char name[13];
        fat12_name_to_str(e->filename, name);
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

        char full_name[PATH_MAX];
        if (prefix && *prefix)
            snprintf(full_name, sizeof(full_name), "%s/%s", prefix, name);
        else
            snprintf(full_name, sizeof(full_name), "%s", name);

        if (e->attributes & FAT12_ATTR_DIRECTORY) {
            printf("%-40s <DIR>\n", full_name);
            fat12_list_recursive(fp, params, fat_offset, root_offset, data_offset,
                                 fat, LE16_TO_HOST(e->first_cluster_lo), full_name);
        } else {
            printf("%-40s %10u\n", full_name, (unsigned)LE32_TO_HOST(e->file_size));
        }
    }
    free(dir_buffer);
}

static int fat12_list(const char *image_path, int recursive) {
    FILE *fp = fopen(image_path, "rb");
    if (!fp) { perror("fopen"); return -1; }

    floppy_params_t params;
    uint32_t fat_offset, root_offset, data_offset;
    uint16_t data_clusters;
    if (fat12_read_geometry(fp, &params, &fat_offset, &root_offset,
                            &data_offset, &data_clusters) != 0) {
        fprintf(stderr, "Failed to read geometry.\n");
        fclose(fp); return -1;
    }

    uint8_t *fat = (uint8_t*)malloc(params.sectors_per_fat * SECTOR_SIZE);
    fseek(fp, fat_offset, SEEK_SET);
    fread(fat, 1, params.sectors_per_fat * SECTOR_SIZE, fp);

    printf("Listing of %s:\n", image_path);
    if (recursive) {
        fat12_list_recursive(fp, &params, fat_offset, root_offset, data_offset,
                             fat, 0, "");
    } else {
        size_t dir_size;
        uint8_t *root = fat12_load_directory(fp, &params, fat_offset, root_offset,
                                             data_offset, fat, 0, &dir_size);
        for (size_t off = 0; off < dir_size; off += 32) {
            fat12_dirent_t *e = (fat12_dirent_t*)(root + off);
            if (e->filename[0] == 0x00) break;
            if (e->filename[0] == 0xE5) continue;
            if (e->attributes == 0x0F || (e->attributes & FAT12_ATTR_VOLUME)) continue;
            char name[13];
            fat12_name_to_str(e->filename, name);
            printf("%-11s %10u  %c%c%c%c%c\n", name,
                   (unsigned)LE32_TO_HOST(e->file_size),
                   (e->attributes & FAT12_ATTR_READONLY) ? 'R' : '-',
                   (e->attributes & FAT12_ATTR_HIDDEN)   ? 'H' : '-',
                   (e->attributes & FAT12_ATTR_SYSTEM)   ? 'S' : '-',
                   (e->attributes & FAT12_ATTR_DIRECTORY)? 'D' : '-',
                   (e->attributes & FAT12_ATTR_ARCHIVE)  ? 'A' : '-');
        }
        free(root);
    }
    free(fat);
    fclose(fp);
    return 0;
}

static int fat12_extract_directory_recursive(FILE *fp, const floppy_params_t *params,
                                             uint32_t fat_offset, uint32_t root_offset,
                                             uint32_t data_offset, const uint8_t *fat,
                                             uint16_t dir_cluster, const char *dest_local) {
    MKDIR(dest_local);
    size_t dir_size;
    uint8_t *dir_buffer = fat12_load_directory(fp, params, fat_offset,
                                               root_offset, data_offset, fat,
                                               dir_cluster, &dir_size);
    uint32_t cluster_bytes = params->sectors_per_cluster * SECTOR_SIZE;

    for (size_t off = 0; off < dir_size; off += 32) {
        fat12_dirent_t *e = (fat12_dirent_t*)(dir_buffer + off);
        if (e->filename[0] == 0x00) break;
        if (e->filename[0] == 0xE5) continue;
        if (e->attributes == 0x0F) continue;

        char name[13];
        fat12_name_to_str(e->filename, name);
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

        char out_path[PATH_MAX];
        snprintf(out_path, sizeof(out_path), "%s/%s", dest_local, name);

        if (e->attributes & FAT12_ATTR_DIRECTORY) {
            fat12_extract_directory_recursive(fp, params, fat_offset, root_offset,
                                              data_offset, fat,
                                              LE16_TO_HOST(e->first_cluster_lo), out_path);
        } else {
            FILE *out_fp = fopen(out_path, "wb");
            if (out_fp) {
                uint32_t remaining = LE32_TO_HOST(e->file_size);
                uint16_t cluster = LE16_TO_HOST(e->first_cluster_lo);
                uint8_t *buf = (uint8_t*)malloc(cluster_bytes);
                while (remaining > 0 && cluster < 0xFF0) {
                    size_t to_read = (remaining > cluster_bytes) ? cluster_bytes : remaining;
                    fseek(fp, data_offset + (cluster - 2) * cluster_bytes, SEEK_SET);
                    fread(buf, 1, cluster_bytes, fp);
                    fwrite(buf, 1, to_read, out_fp);
                    remaining -= (uint32_t)to_read;
                    cluster = fat12_get_entry(fat, cluster);
                }
                free(buf);
                fclose(out_fp);
            }
        }
    }
    free(dir_buffer);
    return 0;
}

static int fat12_extract_directory(const char *image_path, const char *src_path,
                                   const char *dest_local) {
    FILE *fp = fopen(image_path, "rb");
    if (!fp) { perror("fopen"); return -1; }

    floppy_params_t params;
    uint32_t fat_offset, root_offset, data_offset;
    uint16_t data_clusters;
    if (fat12_read_geometry(fp, &params, &fat_offset, &root_offset,
                            &data_offset, &data_clusters) != 0) {
        fprintf(stderr, "Failed to read geometry.\n");
        fclose(fp); return -1;
    }

    uint8_t *fat = (uint8_t*)malloc(params.sectors_per_fat * SECTOR_SIZE);
    fseek(fp, fat_offset, SEEK_SET);
    fread(fat, 1, params.sectors_per_fat * SECTOR_SIZE, fp);

    fat12_dirent_t entry;
    uint16_t cluster = fat12_find_path(fp, &params, fat_offset, root_offset,
                                       data_offset, fat, src_path, &entry);
    if (cluster == 0xFFFF) {
        fprintf(stderr, "Path '%s' not found.\n", src_path);
        free(fat); fclose(fp); return -1;
    }
    if (!(entry.attributes & FAT12_ATTR_DIRECTORY)) {
        fprintf(stderr, "'%s' is not a directory.\n", src_path);
        free(fat); fclose(fp); return -1;
    }

    int ret = fat12_extract_directory_recursive(fp, &params, fat_offset, root_offset,
                                                data_offset, fat, cluster, dest_local);
    free(fat);
    fclose(fp);
    if (ret == 0)
        printf("Extracted directory '%s' to '%s'\n", src_path, dest_local);
    return ret;
}

static int fat12_extract_file(const char *image_path, const char *src_path,
                              const char *dest_file) {
    FILE *fp = fopen(image_path, "rb");
    if (!fp) { perror("fopen"); return -1; }

    floppy_params_t params;
    uint32_t fat_offset, root_offset, data_offset;
    uint16_t data_clusters;
    if (fat12_read_geometry(fp, &params, &fat_offset, &root_offset,
                            &data_offset, &data_clusters) != 0) {
        fclose(fp); return -1;
    }

    uint8_t *fat = (uint8_t*)malloc(params.sectors_per_fat * SECTOR_SIZE);
    fseek(fp, fat_offset, SEEK_SET);
    fread(fat, 1, params.sectors_per_fat * SECTOR_SIZE, fp);

    fat12_dirent_t entry;
    uint16_t cluster = fat12_find_path(fp, &params, fat_offset, root_offset,
                                       data_offset, fat, src_path, &entry);
    if (cluster == 0xFFFF) {
        fprintf(stderr, "Path '%s' not found.\n", src_path);
        free(fat); fclose(fp); return -1;
    }
    if (entry.attributes & FAT12_ATTR_DIRECTORY) {
        fprintf(stderr, "'%s' is a directory, use extract-dir-fd.\n", src_path);
        free(fat); fclose(fp); return -1;
    }

    FILE *out_fp = fopen(dest_file, "wb");
    if (!out_fp) {
        perror("fopen dest");
        free(fat); fclose(fp); return -1;
    }

    uint16_t file_cluster = LE16_TO_HOST(entry.first_cluster_lo);
    uint32_t remaining = LE32_TO_HOST(entry.file_size);
    uint32_t cluster_bytes = params.sectors_per_cluster * SECTOR_SIZE;
    uint8_t *buf = (uint8_t*)malloc(cluster_bytes);
    while (remaining > 0 && file_cluster < 0xFF0) {
        size_t to_read = (remaining > cluster_bytes) ? cluster_bytes : remaining;
        fseek(fp, data_offset + (file_cluster - 2) * cluster_bytes, SEEK_SET);
        fread(buf, 1, cluster_bytes, fp);
        fwrite(buf, 1, to_read, out_fp);
        remaining -= (uint32_t)to_read;
        file_cluster = fat12_get_entry(fat, file_cluster);
    }
    free(buf);
    fclose(out_fp);
    free(fat);
    fclose(fp);
    printf("Extracted '%s' to '%s'\n", src_path, dest_file);
    return 0;
}

static void fat12_delete_dir_recursive(FILE *fp, const floppy_params_t *params,
                                       uint32_t fat_offset, uint32_t root_offset,
                                       uint32_t data_offset, uint8_t *fat,
                                       uint16_t dir_cluster) {
    size_t dir_size;
    uint8_t *dir_buffer = fat12_load_directory(fp, params, fat_offset, root_offset,
                                               data_offset, fat, dir_cluster, &dir_size);
    size_t off;
    for (off = 0; off < dir_size; off += 32) {
        fat12_dirent_t *e = (fat12_dirent_t*)(dir_buffer + off);
        if (e->filename[0] == 0x00) break;
        if (e->filename[0] == 0xE5) continue;
        if (e->attributes == 0x0F || (e->attributes & FAT12_ATTR_VOLUME)) continue;

        if (e->filename[0] == '.' &&
            (e->filename[1] == ' ' || (e->filename[1] == '.' && e->filename[2] == ' ')))
            continue;

        uint16_t child_cluster = LE16_TO_HOST(e->first_cluster_lo);

        if (e->attributes & FAT12_ATTR_DIRECTORY) {
            fat12_delete_dir_recursive(fp, params, fat_offset, root_offset,
                                       data_offset, fat, child_cluster);
        }

        uint16_t cluster = child_cluster;
        while (cluster >= 2 && cluster < 0xFF0) {
            uint16_t next = fat12_get_entry(fat, cluster);
            fat12_set_entry(fat, cluster, 0x000);
            cluster = next;
        }
    }
    free(dir_buffer);

    uint16_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < 0xFF0) {
        uint16_t next = fat12_get_entry(fat, cluster);
        fat12_set_entry(fat, cluster, 0x000);
        cluster = next;
    }
}

static int fat12_delete_directory(const char *image_path, const char *path) {
    FILE *fp = fopen(image_path, "r+b");
    if (!fp) { perror("fopen"); return -1; }

    floppy_params_t params;
    uint32_t fat_offset, root_offset, data_offset;
    uint16_t data_clusters;
    if (fat12_read_geometry(fp, &params, &fat_offset, &root_offset,
                            &data_offset, &data_clusters) != 0) {
        fclose(fp); return -1;
    }

    uint8_t *fat = (uint8_t*)malloc(params.sectors_per_fat * SECTOR_SIZE);
    fseek(fp, fat_offset, SEEK_SET);
    fread(fat, 1, params.sectors_per_fat * SECTOR_SIZE, fp);

    fat12_dirent_t entry;
    uint16_t result = fat12_find_path(fp, &params, fat_offset, root_offset,
                                      data_offset, fat, path, &entry);
    if (result == 0xFFFF) {
        fprintf(stderr, "Path '%s' not found.\n", path);
        free(fat); fclose(fp); return -1;
    }
    if (!(entry.attributes & FAT12_ATTR_DIRECTORY)) {
        fprintf(stderr, "'%s' is not a directory, use delete-fd instead.\n", path);
        free(fat); fclose(fp); return -1;
    }

    uint16_t dir_cluster = LE16_TO_HOST(entry.first_cluster_lo);

    fat12_delete_dir_recursive(fp, &params, fat_offset, root_offset,
                               data_offset, fat, dir_cluster);

    char parent_path[PATH_MAX];
    const char *last_slash = strrchr(path, '/');
    const char *last_back  = strrchr(path, '\\');
    const char *sep = (last_slash > last_back) ? last_slash : last_back;
    uint16_t parent_cluster = 0;
    if (sep) {
        size_t len = (size_t)(sep - path);
        if (len >= PATH_MAX) len = PATH_MAX - 1;
        memcpy(parent_path, path, len);
        parent_path[len] = '\0';
        parent_cluster = fat12_find_path(fp, &params, fat_offset, root_offset,
                                         data_offset, fat, parent_path, NULL);
        if (parent_cluster == 0xFFFF) {
            fprintf(stderr, "Parent directory not found.\n");
            free(fat); fclose(fp); return -1;
        }
    }

    size_t dir_size;
    uint8_t *dir_buffer = fat12_load_directory(fp, &params, fat_offset, root_offset,
                                               data_offset, fat, parent_cluster, &dir_size);
    fat12_dirent_t *dir_entry = NULL;
    size_t off;
    for (off = 0; off < dir_size; off += 32) {
        fat12_dirent_t *e = (fat12_dirent_t*)(dir_buffer + off);
        if (e->filename[0] == 0x00) break;
        if (e->filename[0] == 0xE5) continue;
        if (LE16_TO_HOST(e->first_cluster_lo) == dir_cluster &&
            memcmp(e->filename, entry.filename, 11) == 0) {
            dir_entry = e;
            break;
        }
    }
    if (!dir_entry) {
        fprintf(stderr, "Directory entry not found in parent.\n");
        free(dir_buffer); free(fat); fclose(fp); return -1;
    }

    dir_entry->filename[0] = 0xE5;

    fat12_save_directory(fp, &params, root_offset, data_offset, fat,
                         parent_cluster, dir_buffer, dir_size);
    fseek(fp, fat_offset, SEEK_SET);
    fwrite(fat, 1, params.sectors_per_fat * SECTOR_SIZE, fp);
    fseek(fp, fat_offset + params.sectors_per_fat * SECTOR_SIZE, SEEK_SET);
    fwrite(fat, 1, params.sectors_per_fat * SECTOR_SIZE, fp);

    free(dir_buffer);
    free(fat);
    fclose(fp);
    printf("Deleted directory '%s'\n", path);
    return 0;
}

static int fat12_delete_file(const char *image_path, const char *path) {
    FILE *fp = fopen(image_path, "r+b");
    if (!fp) { perror("fopen"); return -1; }

    floppy_params_t params;
    uint32_t fat_offset, root_offset, data_offset;
    uint16_t data_clusters;
    if (fat12_read_geometry(fp, &params, &fat_offset, &root_offset,
                            &data_offset, &data_clusters) != 0) {
        fclose(fp); return -1;
    }

    uint8_t *fat = (uint8_t*)malloc(params.sectors_per_fat * SECTOR_SIZE);
    fseek(fp, fat_offset, SEEK_SET);
    fread(fat, 1, params.sectors_per_fat * SECTOR_SIZE, fp);

    fat12_dirent_t entry;
    uint16_t result = fat12_find_path(fp, &params, fat_offset, root_offset,
                                      data_offset, fat, path, &entry);
    if (result == 0xFFFF) {
        fprintf(stderr, "Path '%s' not found.\n", path);
        free(fat); fclose(fp); return -1;
    }
    if (entry.attributes & FAT12_ATTR_DIRECTORY) {
        fprintf(stderr, "'%s' is a directory, cannot delete with delete-fd.\n", path);
        free(fat); fclose(fp); return -1;
    }

    char parent_path[PATH_MAX];
    const char *last_slash = strrchr(path, '/');
    const char *last_back = strrchr(path, '\\');
    const char *sep = last_slash > last_back ? last_slash : last_back;
    uint16_t parent_cluster = 0;
    if (sep) {
        size_t len = sep - path;
        if (len >= PATH_MAX) len = PATH_MAX - 1;
        memcpy(parent_path, path, len);
        parent_path[len] = '\0';
        parent_cluster = fat12_find_path(fp, &params, fat_offset, root_offset,
                                         data_offset, fat, parent_path, NULL);
        if (parent_cluster == 0xFFFF) {
            fprintf(stderr, "Parent directory not found.\n");
            free(fat); fclose(fp); return -1;
        }
    }

    size_t dir_size;
    uint8_t *dir_buffer = fat12_load_directory(fp, &params, fat_offset, root_offset,
                                               data_offset, fat, parent_cluster, &dir_size);

    uint16_t target_cluster = LE16_TO_HOST(entry.first_cluster_lo);
    fat12_dirent_t *dir_entry = NULL;
    for (size_t off = 0; off < dir_size; off += 32) {
        fat12_dirent_t *e = (fat12_dirent_t*)(dir_buffer + off);
        if (e->filename[0] == 0x00) break;
        if (e->filename[0] == 0xE5) continue;
        if (LE16_TO_HOST(e->first_cluster_lo) == target_cluster &&
            memcmp(e->filename, entry.filename, 11) == 0) {
            dir_entry = e;
            break;
        }
    }
    if (!dir_entry) {
        fprintf(stderr, "File entry not found in parent directory.\n");
        free(dir_buffer); free(fat); fclose(fp); return -1;
    }

    uint16_t cluster = target_cluster;
    while (cluster < 0xFF0) {
        uint16_t next = fat12_get_entry(fat, cluster);
        fat12_set_entry(fat, cluster, 0x000);
        cluster = next;
    }

    dir_entry->filename[0] = 0xE5;

    fat12_save_directory(fp, &params, root_offset, data_offset, fat,
                         parent_cluster, dir_buffer, dir_size);
    fseek(fp, fat_offset, SEEK_SET);
    fwrite(fat, 1, params.sectors_per_fat * SECTOR_SIZE, fp);
    fseek(fp, fat_offset + params.sectors_per_fat * SECTOR_SIZE, SEEK_SET);
    fwrite(fat, 1, params.sectors_per_fat * SECTOR_SIZE, fp);

    free(dir_buffer);
    free(fat);
    fclose(fp);
    printf("Deleted '%s'\n", path);
    return 0;
}

static void make_iso_name(const char *src, char *dest) {
    int i = 0, j = 0;
    const char *dot = strrchr(src, '.');
    if (!dot) dot = src + strlen(src);
    for (; i < 8 && src < dot; src++) {
        if (isalnum((unsigned char)*src) || *src == '_')
            dest[i++] = (char)toupper((unsigned char)*src);
    }
    if (*dot == '.') {
        const char *ext = dot + 1;
        if (*ext) {
            dest[i++] = '.';
            for (; j < 3 && *ext; ext++) {
                if (isalnum((unsigned char)*ext) || *ext == '_')
                    dest[i++] = (char)toupper((unsigned char)*ext), j++;
            }
        }
    }
    dest[i] = '\0';
}

static int iso_find_path(FILE *fp, uint32_t dir_extent, uint32_t dir_size,
                         const char *path, uint32_t *out_extent, uint32_t *out_size,
                         uint8_t *out_flags) {
    char component[256];
    const char *p = path;
    uint32_t current_extent = dir_extent;
    uint32_t current_size = dir_size;
    uint8_t *buf = NULL;

    if (current_size == 0) current_size = ISO_BLOCK_SIZE;

    while (*p) {
        while (*p == '/' || *p == '\\') p++;
        if (*p == '\0') break;

        int ci = 0;
        while (*p && *p != '/' && *p != '\\' && ci < 255)
            component[ci++] = *p++;
        component[ci] = '\0';

        buf = (uint8_t*)malloc(current_size);
        fseek(fp, current_extent * ISO_BLOCK_SIZE, SEEK_SET);
        fread(buf, 1, current_size, fp);

        int found = 0;
        uint8_t *end = buf + current_size;
        uint8_t *rec_ptr = buf;
        while (rec_ptr < end) {
            iso_dir_rec_t *rec = (iso_dir_rec_t*)rec_ptr;
            if (rec->record_len == 0) break;
            if (rec->name_len > 0 && rec->name[0] != 0 && rec->name[0] != 1) {
                char name[256];
                memcpy(name, rec->name, rec->name_len);
                name[rec->name_len] = '\0';
                int nlen = rec->name_len;
                while (nlen > 0 && name[nlen-1] == ' ') nlen--;
                name[nlen] = '\0';

                if (strcasecmp(name, component) == 0) {
                    current_extent = READ_ISO_32LE(rec->extent_loc);
                    current_size   = READ_ISO_32LE(rec->data_len);
                    if (*p == '\0') {
                        *out_extent = current_extent;
                        *out_size   = current_size;
                        *out_flags  = rec->flags;
                    }
                    found = 1;
                    break;
                }
            }
            rec_ptr += rec->record_len;
        }
        free(buf);
        if (!found) return 0;
        if (*p == '\0') return 1;
    }
    return 0;
}

static uint8_t* write_iso_dir_record(uint8_t *buf, const char *name, int name_len,
                                     uint32_t extent, uint32_t size,
                                     uint8_t flags, const struct tm *tm) {
    iso_dir_rec_t *rec = (iso_dir_rec_t*)buf;
    rec->record_len = (uint8_t)(sizeof(iso_dir_rec_t) + name_len);
    if (rec->record_len % 2) rec->record_len++;                                 
    rec->ext_attr_len = 0;
    WRITE_ISO_32LE(rec->extent_loc, extent);
    WRITE_ISO_32LE(rec->data_len, size);
    rec->rec_date[0] = (uint8_t)tm->tm_year;
    rec->rec_date[1] = (uint8_t)(tm->tm_mon + 1);
    rec->rec_date[2] = (uint8_t)tm->tm_mday;
    rec->rec_date[3] = (uint8_t)tm->tm_hour;
    rec->rec_date[4] = (uint8_t)tm->tm_min;
    rec->rec_date[5] = (uint8_t)tm->tm_sec;
    rec->rec_date[6] = 0;
    rec->flags = flags;
    rec->file_unit_size = 0;
    rec->interleave_gap = 0;
    WRITE_ISO_32LE(rec->vol_seq_num, 1);
    rec->name_len = (uint8_t)name_len;
    memcpy(rec->name, name, (size_t)name_len);
    return buf + rec->record_len;
}

static uint32_t iso_add_tree(FILE *fp, const char *local_path, const char *iso_path,
                             uint32_t *current_extent) {
    DIR *dir;
    struct dirent *de;
    struct stat st;
    char full_local[PATH_MAX];
    char full_iso[PATH_MAX];
    char iso_name[13];
    uint32_t extent = *current_extent;
    uint32_t data_start = extent;
    uint8_t *dir_block = NULL;                                     
    uint8_t *block_ptr;
    uint32_t dir_size_bytes = 0;
    uint32_t dir_blocks;
    struct tm *tm;
    FILE *in_fp;
    size_t bytes;

    dir = opendir(local_path);
    if (!dir) {
        perror("opendir");
        return 0;
    }

    rewinddir(dir);
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (snprintf(full_local, sizeof(full_local), "%s/%s", local_path, de->d_name) >= (int)sizeof(full_local))
            continue;
        if (stat(full_local, &st) != 0) continue;

        make_iso_name(de->d_name, iso_name);
        int name_len = (int)strlen(iso_name);
        dir_size_bytes += sizeof(iso_dir_rec_t) + name_len;
        if ((sizeof(iso_dir_rec_t) + name_len) % 2) dir_size_bytes++;
    }
    dir_size_bytes += sizeof(iso_dir_rec_t) + 1;            
    if ((sizeof(iso_dir_rec_t) + 1) % 2) dir_size_bytes++;
    dir_size_bytes += sizeof(iso_dir_rec_t) + 1;             
    if ((sizeof(iso_dir_rec_t) + 1) % 2) dir_size_bytes++;

    dir_blocks = (dir_size_bytes + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE;
    dir_block = (uint8_t*)calloc(dir_blocks, ISO_BLOCK_SIZE);
    if (!dir_block) {
        closedir(dir);
        return 0;
    }

    data_start = *current_extent;
    *current_extent += dir_blocks;

    block_ptr = dir_block;
    if (stat(local_path, &st) != 0) {
        free(dir_block);
        closedir(dir);
        return 0;
    }
    tm = localtime(&st.st_mtime);

    block_ptr = write_iso_dir_record(block_ptr, "\0", 1, data_start, dir_blocks * ISO_BLOCK_SIZE, 0x02, tm);
    block_ptr = write_iso_dir_record(block_ptr, "\1", 1, extent, dir_blocks * ISO_BLOCK_SIZE, 0x02, tm);

    rewinddir(dir);
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (snprintf(full_local, sizeof(full_local), "%s/%s", local_path, de->d_name) >= (int)sizeof(full_local)) continue;
        if (stat(full_local, &st) != 0) continue;
        if (snprintf(full_iso, sizeof(full_iso), "%s/%s", iso_path, de->d_name) >= (int)sizeof(full_iso)) continue;

        make_iso_name(de->d_name, iso_name);

        if (S_ISDIR(st.st_mode)) {
            uint32_t sub_extent = iso_add_tree(fp, full_local, full_iso, current_extent);
            tm = localtime(&st.st_mtime);
            block_ptr = write_iso_dir_record(block_ptr, iso_name, (int)strlen(iso_name), sub_extent,
                                             dir_blocks * ISO_BLOCK_SIZE, 0x02, tm);
        } else if (S_ISREG(st.st_mode)) {
            uint32_t file_extent = *current_extent;
            uint32_t file_blocks = (uint32_t)((st.st_size + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE);
            in_fp = fopen(full_local, "rb");
            if (in_fp) {
                uint8_t *file_buf = (uint8_t*)malloc(ISO_BLOCK_SIZE);
                fseek(fp, file_extent * ISO_BLOCK_SIZE, SEEK_SET);
                while ((bytes = fread(file_buf, 1, ISO_BLOCK_SIZE, in_fp)) > 0) {
                    if (bytes < ISO_BLOCK_SIZE)
                        memset(file_buf + bytes, 0, ISO_BLOCK_SIZE - bytes);
                    fwrite(file_buf, 1, ISO_BLOCK_SIZE, fp);
                }
                free(file_buf);
                fclose(in_fp);
            }
            *current_extent += file_blocks;
            tm = localtime(&st.st_mtime);
            block_ptr = write_iso_dir_record(block_ptr, iso_name, (int)strlen(iso_name), file_extent,
                                             (uint32_t)st.st_size, 0x00, tm);
        }
    }
    closedir(dir);

    fseek(fp, data_start * ISO_BLOCK_SIZE, SEEK_SET);
    fwrite(dir_block, 1, dir_blocks * ISO_BLOCK_SIZE, fp);
    free(dir_block);

    return data_start;
}

static uint32_t iso_get_directory_size(FILE *fp, uint32_t extent) {
    uint8_t block[ISO_BLOCK_SIZE];
    fseek(fp, extent * ISO_BLOCK_SIZE, SEEK_SET);
    if (fread(block, 1, ISO_BLOCK_SIZE, fp) != ISO_BLOCK_SIZE)
        return ISO_BLOCK_SIZE;                
    iso_dir_rec_t *rec = (iso_dir_rec_t*)block;
    return READ_ISO_32LE(rec->data_len);
}

static int iso_create(const char *dirname, const char *iso_path, const char *vol_label) {
    FILE *fp;
    uint8_t block[ISO_BLOCK_SIZE];
    uint32_t current_extent;
    uint32_t root_extent, root_size;
    struct stat st;
    struct tm *tm;
    iso_pvd_t *pvd;
    iso_datetime_t *iso_date;
    uint8_t *p;
    char padded_label[32];
    int i;

    if (stat(dirname, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "'%s' is not a valid directory.\n", dirname);
        return -1;
    }

    fp = fopen(iso_path, "wb");
    if (!fp) {
        perror("fopen iso");
        return -1;
    }

    memset(block, 0, ISO_BLOCK_SIZE);
    for (i = 0; i < 16; i++) {
        fwrite(block, 1, ISO_BLOCK_SIZE, fp);
    }

    current_extent = ISO_SYSTEM_AREA + ISO_VOLUME_DESC_LEN + 1;                                                       

    root_extent = iso_add_tree(fp, dirname, "", &current_extent);

    root_size = iso_get_directory_size(fp, root_extent);
    if (root_size == 0) root_size = ISO_BLOCK_SIZE;               

    memset(block, 0, ISO_BLOCK_SIZE);
    pvd = (iso_pvd_t*)block;
    pvd->type = 1;
    memcpy(pvd->id, "CD001", 5);
    pvd->version = 1;

    p = pvd->data;

    memset(padded_label, ' ', 32);
    if (vol_label) {
        size_t len = strlen(vol_label);
        if (len > 32) len = 32;
        memcpy(padded_label, vol_label, len);
    } else {
        memcpy(padded_label, "CDROM", 5);
    }
    memcpy(p + 40, padded_label, 32);

    WRITE_ISO_32LE(p + 80, current_extent);

    WRITE_ISO_32LE(p + 120, 1);
    WRITE_ISO_32LE(p + 124, 1);

    WRITE_ISO_32LE(p + 128, ISO_BLOCK_SIZE);

    iso_date = (iso_datetime_t*)(p + 813);
    tm = localtime(&st.st_mtime);
    fill_digits(iso_date->year, tm->tm_year + 1900, 4);
    fill_digits(iso_date->month, tm->tm_mon + 1, 2);
    fill_digits(iso_date->day, tm->tm_mday, 2);
    fill_digits(iso_date->hour, tm->tm_hour, 2);
    fill_digits(iso_date->minute, tm->tm_min, 2);
    fill_digits(iso_date->second, tm->tm_sec, 2);
    fill_digits(iso_date->hundredths, 0, 2);
    iso_date->gmt_offset = 0;

    p += 156;
    tm = localtime(&st.st_mtime);
    write_iso_dir_record(p, "\0", 1, root_extent, root_size, 0x02, tm);

    fseek(fp, ISO_SYSTEM_AREA * ISO_BLOCK_SIZE, SEEK_SET);
    fwrite(block, 1, ISO_BLOCK_SIZE, fp);

    memset(block, 0, ISO_BLOCK_SIZE);
    pvd = (iso_pvd_t*)block;
    pvd->type = 255;
    memcpy(pvd->id, "CD001", 5);
    pvd->version = 1;
    fseek(fp, (ISO_SYSTEM_AREA + ISO_VOLUME_DESC_LEN) * ISO_BLOCK_SIZE, SEEK_SET);
    fwrite(block, 1, ISO_BLOCK_SIZE, fp);

    fclose(fp);
    printf("Created ISO image: %s (Volume: %.*s)\n", iso_path, 32, padded_label);
    return 0;
}

static int iso_list(const char *iso_path) {
    FILE *fp;
    uint8_t block[ISO_BLOCK_SIZE];
    iso_pvd_t *pvd;
    iso_dir_rec_t *root_rec;
    uint32_t root_extent, root_size;
    uint8_t *p;

    fp = fopen(iso_path, "rb");
    if (!fp) {
        perror("fopen");
        return -1;
    }

    fseek(fp, 16 * ISO_BLOCK_SIZE, SEEK_SET);
    if (fread(block, 1, ISO_BLOCK_SIZE, fp) != ISO_BLOCK_SIZE) {
        fprintf(stderr, "Failed to read PVD.\n");
        fclose(fp);
        return -1;
    }

    pvd = (iso_pvd_t*)block;
    if (pvd->type != 1) {
        fprintf(stderr, "Invalid ISO image.\n");
        fclose(fp);
        return -1;
    }

    root_rec = (iso_dir_rec_t*)(pvd->data + 156);
    root_extent = READ_ISO_32LE(root_rec->extent_loc);
    root_size = READ_ISO_32LE(root_rec->data_len);

    printf("Listing of %s:\n", iso_path);

    fseek(fp, root_extent * ISO_BLOCK_SIZE, SEEK_SET);
    p = malloc(root_size);
    if (!p) {
        fclose(fp);
        return -1;
    }
    if (fread(p, 1, root_size, fp) != root_size) {
        free(p);
        fclose(fp);
        return -1;
    }

    uint8_t *end = p + root_size;
    uint8_t *rec_ptr = p;

    while (rec_ptr < end) {
        iso_dir_rec_t *rec = (iso_dir_rec_t*)rec_ptr;
        if (rec->record_len == 0) break;
        if (rec->name_len > 0 && rec->name[0] != 0 && rec->name[0] != 1) {
            char name[256];
            uint32_t size = READ_ISO_32LE(rec->data_len);
            int is_dir = (rec->flags & 0x02) ? 1 : 0;

            memcpy(name, rec->name, rec->name_len);
            name[rec->name_len] = '\0';

            printf("%-30s %10u  %s\n", name, (unsigned int)size, is_dir ? "<DIR>" : "");
        }
        rec_ptr += rec->record_len;
    }

    free(p);
    fclose(fp);
    return 0;
}

static int iso_extract_file(const char *iso_path, const char *src_name, const char *dest_file) {
    FILE *fp = fopen(iso_path, "rb");
    if (!fp) return -1;

    uint8_t block[ISO_BLOCK_SIZE];
    fseek(fp, 16 * ISO_BLOCK_SIZE, SEEK_SET);
    fread(block, 1, ISO_BLOCK_SIZE, fp);
    iso_pvd_t *pvd = (iso_pvd_t*)block;
    if (pvd->type != 1) { fclose(fp); return -1; }

    iso_dir_rec_t *root_rec = (iso_dir_rec_t*)(pvd->data + 156);
    uint32_t root_extent = READ_ISO_32LE(root_rec->extent_loc);
    uint32_t root_size   = READ_ISO_32LE(root_rec->data_len);

    uint32_t file_extent, file_size;
    uint8_t flags;
    if (!iso_find_path(fp, root_extent, root_size, src_name,
                       &file_extent, &file_size, &flags) || (flags & 0x02)) {
        fprintf(stderr, "File not found or is a directory.\n");
        fclose(fp);
        return -1;
    }

    FILE *out_fp = fopen(dest_file, "wb");
    if (!out_fp) { fclose(fp); return -1; }
    fseek(fp, file_extent * ISO_BLOCK_SIZE, SEEK_SET);
    while (file_size > 0) {
        size_t to_read = (file_size > ISO_BLOCK_SIZE) ? ISO_BLOCK_SIZE : file_size;
        fread(block, 1, to_read, fp);
        fwrite(block, 1, to_read, out_fp);
        file_size -= (uint32_t)to_read;
    }
    fclose(out_fp);
    fclose(fp);
    printf("Extracted '%s'\n", src_name);
    return 0;
}

static int iso_extract_directory_recursive(FILE *fp, uint32_t extent, uint32_t size,
                                           const char *dest_local) {
    uint8_t *buf = (uint8_t*)malloc(size);
    fseek(fp, extent * ISO_BLOCK_SIZE, SEEK_SET);
    fread(buf, 1, size, fp);
    MKDIR(dest_local);

    uint8_t *end = buf + size;
    uint8_t *rec_ptr = buf;
    while (rec_ptr < end) {
        iso_dir_rec_t *rec = (iso_dir_rec_t*)rec_ptr;
        if (rec->record_len == 0) break;
        if (rec->name_len > 0 && rec->name[0] != 0 && rec->name[0] != 1) {
            char name[256];
            memcpy(name, rec->name, rec->name_len);
            name[rec->name_len] = '\0';
            char out_path[PATH_MAX];
            snprintf(out_path, sizeof(out_path), "%s/%s", dest_local, name);
            if (rec->flags & 0x02) {
                iso_extract_directory_recursive(fp,
                    READ_ISO_32LE(rec->extent_loc),
                    READ_ISO_32LE(rec->data_len), out_path);
            } else {
                FILE *out_fp = fopen(out_path, "wb");
                if (out_fp) {
                    uint32_t file_size = READ_ISO_32LE(rec->data_len);
                    uint32_t file_extent = READ_ISO_32LE(rec->extent_loc);
                    fseek(fp, file_extent * ISO_BLOCK_SIZE, SEEK_SET);
                    uint8_t block[ISO_BLOCK_SIZE];
                    while (file_size > 0) {
                        size_t to_read = (file_size > ISO_BLOCK_SIZE) ? ISO_BLOCK_SIZE : file_size;
                        fread(block, 1, to_read, fp);
                        fwrite(block, 1, to_read, out_fp);
                        file_size -= (uint32_t)to_read;
                    }
                    fclose(out_fp);
                }
            }
        }
        rec_ptr += rec->record_len;
    }
    free(buf);
    return 0;
}

static int iso_extract_directory(const char *iso_path, const char *src_path,
                                 const char *dest_local) {
    FILE *fp = fopen(iso_path, "rb");
    if (!fp) return -1;

    uint8_t block[ISO_BLOCK_SIZE];
    fseek(fp, 16 * ISO_BLOCK_SIZE, SEEK_SET);
    fread(block, 1, ISO_BLOCK_SIZE, fp);
    iso_pvd_t *pvd = (iso_pvd_t*)block;
    if (pvd->type != 1) { fclose(fp); return -1; }

    iso_dir_rec_t *root_rec = (iso_dir_rec_t*)(pvd->data + 156);
    uint32_t root_extent = READ_ISO_32LE(root_rec->extent_loc);
    uint32_t root_size   = READ_ISO_32LE(root_rec->data_len);

    uint32_t dir_extent, dir_size;
    uint8_t flags;
    if (!iso_find_path(fp, root_extent, root_size, src_path,
                       &dir_extent, &dir_size, &flags) || !(flags & 0x02)) {
        fprintf(stderr, "Directory not found.\n");
        fclose(fp);
        return -1;
    }

    int ret = iso_extract_directory_recursive(fp, dir_extent, dir_size, dest_local);
    fclose(fp);
    if (ret == 0)
        printf("Extracted directory '%s' to '%s'\n", src_path, dest_local);
    return ret;
}



#define ISO_NODE_FILE 0
#define ISO_NODE_DIR  1

typedef struct iso_node_t {
    int      type;                                                         
    uint32_t old_extent;                                                   
    uint32_t old_size;                                                     
    uint32_t new_extent;                                                   
    uint32_t new_blocks;                                                   
    uint8_t *dir_buf;
    uint32_t dir_buf_size;                                                 
    int      is_root;
} iso_node_t;

typedef struct {
    iso_node_t *nodes;
    size_t      count;
    size_t      cap;
} iso_node_list_t;

static void iso_nodelist_init(iso_node_list_t *l) {
    l->nodes = NULL; l->count = 0; l->cap = 0;
}

static iso_node_t* iso_nodelist_add(iso_node_list_t *l) {
    if (l->count == l->cap) {
        size_t newcap = l->cap ? l->cap * 2 : 64;
        iso_node_t *tmp = (iso_node_t*)realloc(l->nodes,
                                                newcap * sizeof(iso_node_t));
        if (!tmp) return NULL;
        l->nodes = tmp; l->cap = newcap;
    }
    memset(&l->nodes[l->count], 0, sizeof(iso_node_t));
    return &l->nodes[l->count++];
}

static void iso_nodelist_free(iso_node_list_t *l) {
    size_t i;
    for (i = 0; i < l->count; i++)
        if (l->nodes[i].dir_buf) free(l->nodes[i].dir_buf);
    free(l->nodes);
    iso_nodelist_init(l);
}

typedef struct {
    char **paths;
    size_t count;
} iso_delset_t;

static int iso_delset_contains(const iso_delset_t *ds, const char *path) {
    size_t i;
    for (i = 0; i < ds->count; i++)
        if (strcasecmp(ds->paths[i], path) == 0) return 1;
    return 0;
}

static int iso_walk_tree(FILE *fp,
                         uint32_t old_extent, uint32_t old_size,
                         const char *abs_prefix,
                         const iso_delset_t *delset,
                         iso_node_list_t *nl,
                         int is_root) {
    uint8_t *dir_src;
    uint8_t *rec_ptr, *end;
    uint32_t dir_alloc = ((old_size + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE)
                         * ISO_BLOCK_SIZE;
    uint8_t *dir_new   = (uint8_t*)calloc(1, dir_alloc);
    uint8_t *dir_ptr   = dir_new;

    iso_node_t *dir_node = iso_nodelist_add(nl);
    if (!dir_node || !dir_new) { free(dir_new); return -1; }
    int dir_idx = (int)(dir_node - nl->nodes);
    dir_node->type        = ISO_NODE_DIR;
    dir_node->old_extent  = old_extent;
    dir_node->old_size    = old_size;
    dir_node->new_blocks  = dir_alloc / ISO_BLOCK_SIZE;
    dir_node->is_root     = is_root;

    dir_src = (uint8_t*)malloc(old_size);
    if (!dir_src) { free(dir_new); return -1; }
    fseek(fp, old_extent * ISO_BLOCK_SIZE, SEEK_SET);
    fread(dir_src, 1, old_size, fp);

    end     = dir_src + old_size;
    rec_ptr = dir_src;
    while (rec_ptr < end) {
        iso_dir_rec_t *rec = (iso_dir_rec_t*)rec_ptr;
        if (rec->record_len == 0) break;

        if (rec->name_len == 1 && (rec->name[0] == 0 || rec->name[0] == 1)) {
            memcpy(dir_ptr, rec, rec->record_len);
            dir_ptr += rec->record_len;
            rec_ptr += rec->record_len;
            continue;
        }

        char abs_child[PATH_MAX];
        char entry_name[256];
        memcpy(entry_name, rec->name, rec->name_len);
        entry_name[rec->name_len] = '\0';
        if (abs_prefix[0])
            snprintf(abs_child, sizeof(abs_child), "%s/%s", abs_prefix, entry_name);
        else
            snprintf(abs_child, sizeof(abs_child), "%s", entry_name);

        if (iso_delset_contains(delset, abs_child)) {
            rec_ptr += rec->record_len;
            continue;
        }

        if (rec->flags & 0x02) {
            uint32_t child_old_extent = READ_ISO_32LE(rec->extent_loc);
            uint32_t child_old_size   = READ_ISO_32LE(rec->data_len);
            int child_idx = iso_walk_tree(fp, child_old_extent, child_old_size,
                                          abs_child, delset, nl, 0);
            if (child_idx < 0) { free(dir_src); free(dir_new); return -1; }

            dir_node = &nl->nodes[dir_idx];

            memcpy(dir_ptr, rec, rec->record_len);
            iso_dir_rec_t *new_rec = (iso_dir_rec_t*)dir_ptr;
            WRITE_ISO_32LE(new_rec->extent_loc, (uint32_t)child_idx);
            dir_ptr += rec->record_len;
        } else {
            iso_node_t *fn = iso_nodelist_add(nl);
            if (!fn) { free(dir_src); free(dir_new); return -1; }
            dir_node = &nl->nodes[dir_idx];

            int file_idx = (int)(fn - nl->nodes);
            fn->type       = ISO_NODE_FILE;
            fn->old_extent = READ_ISO_32LE(rec->extent_loc);
            fn->old_size   = READ_ISO_32LE(rec->data_len);
            fn->new_blocks = (fn->old_size + ISO_BLOCK_SIZE - 1) / ISO_BLOCK_SIZE;

            memcpy(dir_ptr, rec, rec->record_len);
            iso_dir_rec_t *new_rec = (iso_dir_rec_t*)dir_ptr;
            WRITE_ISO_32LE(new_rec->extent_loc, (uint32_t)file_idx);
            dir_ptr += rec->record_len;
        }
        rec_ptr += rec->record_len;
    }
    free(dir_src);

    dir_node = &nl->nodes[dir_idx];                                        
    dir_node->dir_buf      = dir_new;
    dir_node->dir_buf_size = dir_alloc;
    return dir_idx;
}

static uint32_t iso_assign_extents(iso_node_list_t *nl, uint32_t start_extent) {
    uint32_t cur = start_extent;
    size_t i;
    for (i = 0; i < nl->count; i++) {
        nl->nodes[i].new_extent = cur;
        cur += nl->nodes[i].new_blocks;
    }
    for (i = 0; i < nl->count; i++) {
        iso_node_t *node = &nl->nodes[i];
        if (node->type != ISO_NODE_DIR) continue;

        uint8_t *ptr = node->dir_buf;
        uint8_t *end = ptr + node->dir_buf_size;
        while (ptr < end) {
            iso_dir_rec_t *rec = (iso_dir_rec_t*)ptr;
            if (rec->record_len == 0) break;
            if (rec->name_len == 1 && rec->name[0] == 0) {
                WRITE_ISO_32LE(rec->extent_loc, node->new_extent);
                WRITE_ISO_32LE(rec->data_len,   node->new_blocks * ISO_BLOCK_SIZE);
            } else if (rec->name_len == 1 && rec->name[0] == 1) {
                uint32_t old_parent_extent = READ_ISO_32LE(rec->extent_loc);
                uint32_t new_parent_extent = node->new_extent;                    
                size_t j;
                for (j = 0; j < nl->count; j++) {
                    if (nl->nodes[j].type == ISO_NODE_DIR &&
                        nl->nodes[j].old_extent == old_parent_extent) {
                        new_parent_extent = nl->nodes[j].new_extent;
                        break;
                    }
                }
                WRITE_ISO_32LE(rec->extent_loc, new_parent_extent);
            } else {
                uint32_t child_idx = READ_ISO_32LE(rec->extent_loc);
                if (child_idx < (uint32_t)nl->count) {
                    WRITE_ISO_32LE(rec->extent_loc, nl->nodes[child_idx].new_extent);
                    if (rec->flags & 0x02) {
                        WRITE_ISO_32LE(rec->data_len,
                            nl->nodes[child_idx].new_blocks * ISO_BLOCK_SIZE);
                    }
                }
            }
            ptr += rec->record_len;
        }
    }
    return cur;                                                   
}

static int iso_write_repacked(FILE *fp, const iso_node_list_t *nl) {
    size_t i;
    uint8_t *buf = (uint8_t*)malloc(ISO_BLOCK_SIZE);
    if (!buf) return -1;

    for (i = 0; i < nl->count; i++) {
        const iso_node_t *node = &nl->nodes[i];
        uint32_t b;

        if (node->type == ISO_NODE_DIR) {
            fseek(fp, node->new_extent * ISO_BLOCK_SIZE, SEEK_SET);
            fwrite(node->dir_buf, 1, node->dir_buf_size, fp);
        } else {
            for (b = 0; b < node->new_blocks; b++) {
                memset(buf, 0, ISO_BLOCK_SIZE);
                fseek(fp, (node->old_extent + b) * ISO_BLOCK_SIZE, SEEK_SET);
                fread(buf, 1, ISO_BLOCK_SIZE, fp);
                fseek(fp, (node->new_extent + b) * ISO_BLOCK_SIZE, SEEK_SET);
                fwrite(buf, 1, ISO_BLOCK_SIZE, fp);
            }
        }
    }
    free(buf);
    return 0;
}

static int iso_truncate(const char *path, long size) {
#ifdef _WIN32
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    SetFilePointer(h, size, NULL, FILE_BEGIN);
    SetEndOfFile(h);
    CloseHandle(h);
    return 0;
#else
    return truncate(path, (off_t)size);
#endif
}

static int iso_repack_delete(const char *iso_path, const iso_delset_t *delset,
                             int delete_type) {
    FILE *fp_r = fopen(iso_path, "rb");
    if (!fp_r) { perror("fopen"); return -1; }

    uint8_t pvd_block[ISO_BLOCK_SIZE];
    fseek(fp_r, 16 * ISO_BLOCK_SIZE, SEEK_SET);
    fread(pvd_block, 1, ISO_BLOCK_SIZE, fp_r);
    iso_pvd_t *pvd_check = (iso_pvd_t*)pvd_block;
    if (pvd_check->type != 1) {
        fprintf(stderr, "Invalid ISO image.\n");
        fclose(fp_r); return -1;
    }
    iso_dir_rec_t *root_check = (iso_dir_rec_t*)(pvd_check->data + 156);
    uint32_t root_extent = READ_ISO_32LE(root_check->extent_loc);
    uint32_t root_size   = READ_ISO_32LE(root_check->data_len);

    size_t di;
    for (di = 0; di < delset->count; di++) {
        uint32_t e, s; uint8_t fl;
        if (!iso_find_path(fp_r, root_extent, root_size,
                           delset->paths[di], &e, &s, &fl)) {
            fprintf(stderr, "'%s' not found in ISO.\n", delset->paths[di]);
            fclose(fp_r); return -1;
        }
        if (delete_type == 0 && (fl & 0x02)) {
            fprintf(stderr, "'%s' is a directory, use delete-dir-iso.\n",
                    delset->paths[di]);
            fclose(fp_r); return -1;
        }
        if (delete_type == 1 && !(fl & 0x02)) {
            fprintf(stderr, "'%s' is a file, use delete-iso.\n",
                    delset->paths[di]);
            fclose(fp_r); return -1;
        }
    }

    iso_node_list_t nl;
    iso_nodelist_init(&nl);
    int root_idx = iso_walk_tree(fp_r, root_extent, root_size, "",
                                 delset, &nl, 1);
    fclose(fp_r);

    if (root_idx < 0) {
        fprintf(stderr, "Failed to walk ISO tree.\n");
        iso_nodelist_free(&nl);
        return -1;
    }

    uint32_t new_total = iso_assign_extents(&nl, ISO_SYSTEM_AREA + 2);

    FILE *fp_w = fopen(iso_path, "r+b");
    if (!fp_w) { perror("fopen r+b"); iso_nodelist_free(&nl); return -1; }

    if (iso_write_repacked(fp_w, &nl) != 0) {
        fprintf(stderr, "Write failed during repack.\n");
        fclose(fp_w); iso_nodelist_free(&nl); return -1;
    }

    fseek(fp_w, 16 * ISO_BLOCK_SIZE, SEEK_SET);
    fread(pvd_block, 1, ISO_BLOCK_SIZE, fp_w);
    iso_pvd_t *pvd_w = (iso_pvd_t*)pvd_block;
    uint8_t *pd = pvd_w->data;

    WRITE_ISO_32LE(pd + 80, new_total);

    iso_dir_rec_t *new_root_rec = (iso_dir_rec_t*)(pd + 156);
    WRITE_ISO_32LE(new_root_rec->extent_loc, nl.nodes[root_idx].new_extent);
    WRITE_ISO_32LE(new_root_rec->data_len,
                   nl.nodes[root_idx].new_blocks * ISO_BLOCK_SIZE);

    fseek(fp_w, 16 * ISO_BLOCK_SIZE, SEEK_SET);
    fwrite(pvd_block, 1, ISO_BLOCK_SIZE, fp_w);
    fclose(fp_w);

    iso_nodelist_free(&nl);

    long new_size = (long)new_total * ISO_BLOCK_SIZE;
    if (iso_truncate(iso_path, new_size) != 0)
        fprintf(stderr, "Warning: could not truncate ISO (image still valid).\n");

    return 0;
}

static int iso_delete_file(const char *iso_path, const char *path) {
    iso_delset_t ds;
    ds.count = 1;
    ds.paths = (char**)&path;
    int r = iso_repack_delete(iso_path, &ds, 0);
    if (r == 0) printf("Deleted '%s' and repacked ISO.\n", path);
    return r;
}

static int iso_delete_directory(const char *iso_path, const char *path) {
    iso_delset_t ds;
    ds.count = 1;
    ds.paths = (char**)&path;
    int r = iso_repack_delete(iso_path, &ds, 1);
    if (r == 0) printf("Deleted directory '%s' and repacked ISO.\n", path);
    return r;
}


static void print_usage(const char *prog) {
    printf("Flippy - A simple CLI tool to manage floppy discs and Level 1 ISO 9660 CD/DVDs\n");
    printf("Usage: %s <command> [options]\n", prog);
    printf("FAT12 Floppy Commands:\n");
    printf("  create-fd <image> [160|180|320|360|720|1440|2880]  Create FAT12 floppy\n");
    printf("  list-fd <image> [-r]                List files (-r recursive)\n");
    printf("  add-fd <image> <src> [dest]         Add file to root\n");
    printf("  add-dir-fd <image> <local_dir> [dest] Add directory recursively\n");
    printf("  extract-fd <image> <src> <dest>     Extract file\n");
    printf("  extract-dir-fd <image> <src_path> <dest_dir> Extract directory\n");
    printf("  delete-fd <image> <path>            Delete file\n");
    printf("  delete-dir-fd <image> <path>        Delete directory recursively\n");
    printf("ISO 9660 Commands:\n");
    printf("  create-iso <dir> <iso> [label]      Create ISO from directory\n");
    printf("  list-iso <iso>                      List files\n");
    printf("  extract-iso <iso> <file> <dest>     Extract file\n");
    printf("  extract-dir-iso <iso> <src_path> <dest_dir> Extract directory\n");
    printf("  delete-iso <iso> <path>             Delete file from ISO\n");
    printf("  delete-dir-iso <iso> <path>         Delete directory from ISO recursively\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    if (strcmp(argv[1], "create-fd") == 0) {
        floppy_size_t size = FLOPPY_1440KB;
        if (argc < 3 || argc > 4) { fprintf(stderr, "Usage: %s create-fd <image> [160|180|320|360|720|1440|2880]\n", argv[0]); return 1; }
        if (argc == 4) size = (floppy_size_t)atoi(argv[3]);
        return fat12_create(argv[2], size);
    }
    else if (strcmp(argv[1], "list-fd") == 0) {
        int recursive = 0;
        const char *image = NULL;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "/r") == 0)
                recursive = 1;
            else if (!image)
                image = argv[i];
            else {
                fprintf(stderr, "Too many arguments.\n");
                return 1;
            }
        }
        if (!image) {
            fprintf(stderr, "Usage: %s list-fd <image> [-r]\n", argv[0]);
            return 1;
        }
        return fat12_list(image, recursive);
    }
    else if (strcmp(argv[1], "add-fd") == 0) {
        if (argc < 4 || argc > 5) { fprintf(stderr, "Usage: %s add-fd <image> <src> [dest]\n", argv[0]); return 1; }
        return fat12_add_file(argv[2], argv[3], argc == 5 ? argv[4] : NULL);
    }
    else if (strcmp(argv[1], "add-dir-fd") == 0) {
        if (argc < 4 || argc > 5) { fprintf(stderr, "Usage: %s add-dir-fd <image> <local_dir> [dest_path]\n", argv[0]); return 1; }
        return fat12_add_directory(argv[2], argv[3], argc == 5 ? argv[4] : NULL);
    }
    else if (strcmp(argv[1], "extract-fd") == 0) {
        if (argc != 5) { fprintf(stderr, "Usage: %s extract-fd <image> <src> <dest>\n", argv[0]); return 1; }
        return fat12_extract_file(argv[2], argv[3], argv[4]);
    }
    else if (strcmp(argv[1], "extract-dir-fd") == 0) {
        if (argc != 5) { fprintf(stderr, "Usage: %s extract-dir-fd <image> <src_path> <dest_dir>\n", argv[0]); return 1; }
        return fat12_extract_directory(argv[2], argv[3], argv[4]);
    }
    else if (strcmp(argv[1], "delete-fd") == 0) {
        if (argc != 4) { fprintf(stderr, "Usage: %s delete-fd <image> <path>\n", argv[0]); return 1; }
        return fat12_delete_file(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "delete-dir-fd") == 0) {
        if (argc != 4) { fprintf(stderr, "Usage: %s delete-dir-fd <image> <path>\n", argv[0]); return 1; }
        return fat12_delete_directory(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "create-iso") == 0) {
        if (argc < 4 || argc > 5) { fprintf(stderr, "Usage: %s create-iso <dir> <iso> [label]\n", argv[0]); return 1; }
        return iso_create(argv[2], argv[3], argc == 5 ? argv[4] : "CDROM");
    }
    else if (strcmp(argv[1], "list-iso") == 0) {
        if (argc != 3) { fprintf(stderr, "Usage: %s list-iso <iso>\n", argv[0]); return 1; }
        return iso_list(argv[2]);
    }
    else if (strcmp(argv[1], "extract-iso") == 0) {
        if (argc != 5) { fprintf(stderr, "Usage: %s extract-iso <iso> <file> <dest>\n", argv[0]); return 1; }
        return iso_extract_file(argv[2], argv[3], argv[4]);
    }
    else if (strcmp(argv[1], "extract-dir-iso") == 0) {
        if (argc != 5) { fprintf(stderr, "Usage: %s extract-dir-iso <iso> <src_path> <dest_dir>\n", argv[0]); return 1; }
        return iso_extract_directory(argv[2], argv[3], argv[4]);
    }
    else if (strcmp(argv[1], "delete-iso") == 0) {
        if (argc != 4) { fprintf(stderr, "Usage: %s delete-iso <iso> <path>\n", argv[0]); return 1; }
        return iso_delete_file(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "delete-dir-iso") == 0) {
        if (argc != 4) { fprintf(stderr, "Usage: %s delete-dir-iso <iso> <path>\n", argv[0]); return 1; }
        return iso_delete_directory(argv[2], argv[3]);
    }
    else {
        fprintf(stderr, "Unknown command.\n");
        print_usage(argv[0]);
        return 1;
    }
}

#ifdef _MSC_VER
#pragma pack(pop)
#endif

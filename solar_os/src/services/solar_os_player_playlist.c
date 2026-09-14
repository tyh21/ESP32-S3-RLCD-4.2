#include "solar_os_player_playlist.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "solar_os_memory.h"

#define PLAYER_PLAYLIST_MAGIC 0x50534f53U
#define PLAYER_PLAYLIST_VERSION 1U
#define PLAYER_PLAYLIST_DIR ".player"
#define PLAYER_PLAYLIST_FILE "playlist.bin"

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint32_t generation;
} player_playlist_store_t;

typedef struct {
    SemaphoreHandle_t lock;
    bool initialized;
    size_t count;
    size_t capacity;
    uint32_t generation;
    solar_os_player_track_t *tracks;
    char path[SOLAR_OS_STORAGE_PATH_MAX];
} player_playlist_state_t;

static EXT_RAM_BSS_ATTR player_playlist_state_t playlist;

static bool player_playlist_audio_path(const char *path)
{
    const char *dot = path != NULL ? strrchr(path, '.') : NULL;
    return dot != NULL && (strcasecmp(dot, ".wav") == 0 ||
                           strcasecmp(dot, ".mp3") == 0);
}

static uint32_t player_playlist_next_generation(uint32_t generation)
{
    return generation == UINT32_MAX ? 1U : generation + 1U;
}

static esp_err_t player_playlist_prepare_path(void)
{
    if (!solar_os_storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    char directory[SOLAR_OS_STORAGE_PATH_MAX];
    esp_err_t err = solar_os_storage_default_path(
        PLAYER_PLAYLIST_DIR, directory, sizeof(directory));
    if (err != ESP_OK) {
        return err;
    }
    if (solar_os_storage_mkdir(directory) != ESP_OK && errno != EEXIST) {
        return ESP_FAIL;
    }
    return solar_os_storage_join_path(directory, PLAYER_PLAYLIST_FILE,
                                      playlist.path, sizeof(playlist.path));
}

static esp_err_t player_playlist_save_locked(void)
{
    player_playlist_store_t store = {
        .magic = PLAYER_PLAYLIST_MAGIC,
        .version = PLAYER_PLAYLIST_VERSION,
        .count = (uint16_t)playlist.count,
        .generation = playlist.generation,
    };
    char temporary[SOLAR_OS_STORAGE_PATH_MAX];
    char backup[SOLAR_OS_STORAGE_PATH_MAX];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", playlist.path) >=
            (int)sizeof(temporary) ||
        snprintf(backup, sizeof(backup), "%s.bak", playlist.path) >=
            (int)sizeof(backup)) {
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *file = fopen(temporary, "wb");
    if (file == NULL) {
        return ESP_FAIL;
    }
    esp_err_t err = fwrite(&store, sizeof(store), 1U, file) == 1U &&
        (playlist.count == 0U ||
         fwrite(playlist.tracks, sizeof(playlist.tracks[0]),
                playlist.count, file) == playlist.count) ? ESP_OK : ESP_FAIL;
    if (err == ESP_OK && fflush(file) != 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && fsync(fileno(file)) != 0) {
        err = ESP_FAIL;
    }
    if (fclose(file) != 0 && err == ESP_OK) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        struct stat info;
        const bool had_active = stat(playlist.path, &info) == 0;
        (void)solar_os_storage_remove(backup);
        if (had_active) {
            err = solar_os_storage_rename(playlist.path, backup);
        }
        if (err == ESP_OK) {
            err = solar_os_storage_rename(temporary, playlist.path);
        }
        if (err != ESP_OK && had_active) {
            (void)solar_os_storage_rename(backup, playlist.path);
        }
        if (err == ESP_OK) {
            (void)solar_os_storage_remove(backup);
        }
    }
    if (err != ESP_OK) {
        (void)solar_os_storage_remove(temporary);
    }
    return err;
}

esp_err_t solar_os_player_playlist_init(void)
{
    if (playlist.initialized) {
        return ESP_OK;
    }
    if (playlist.lock == NULL) {
        playlist.lock = xSemaphoreCreateMutex();
        if (playlist.lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    esp_err_t err = player_playlist_prepare_path();
    if (err != ESP_OK) {
        return err;
    }
    FILE *file = fopen(playlist.path, "rb");
    if (file != NULL) {
        player_playlist_store_t store;
        bool valid = fread(&store, sizeof(store), 1U, file) == 1U &&
            store.magic == PLAYER_PLAYLIST_MAGIC &&
            store.version == PLAYER_PLAYLIST_VERSION &&
            store.count <= SOLAR_OS_PLAYER_PLAYLIST_CAPACITY;
        if (valid) {
            if (store.count > 0U) {
                playlist.tracks = solar_os_memory_alloc(
                    store.count * sizeof(playlist.tracks[0]),
                    SOLAR_OS_MEMORY_EXTERNAL_PREFERRED,
                    "player.playlist");
                valid = playlist.tracks != NULL &&
                    fread(playlist.tracks, sizeof(playlist.tracks[0]),
                          store.count, file) == store.count;
            }
            if (valid) {
                playlist.count = store.count;
                playlist.capacity = store.count;
                playlist.generation = store.generation;
            } else {
                solar_os_memory_free(playlist.tracks);
                playlist.tracks = NULL;
            }
        }
        fclose(file);
    }
    playlist.initialized = true;
    return ESP_OK;
}

size_t solar_os_player_playlist_count(void)
{
    return playlist.initialized ? playlist.count : 0U;
}

uint32_t solar_os_player_playlist_generation(void)
{
    return playlist.generation;
}

bool solar_os_player_playlist_get(size_t index, solar_os_player_track_t *track)
{
    if (!playlist.initialized || track == NULL || index >= playlist.count) {
        return false;
    }
    xSemaphoreTake(playlist.lock, portMAX_DELAY);
    *track = playlist.tracks[index];
    xSemaphoreGive(playlist.lock);
    return true;
}

esp_err_t solar_os_player_playlist_add(const char *path, size_t *index)
{
    if (!playlist.initialized || path == NULL || !player_playlist_audio_path(path)) {
        return ESP_ERR_INVALID_ARG;
    }
    char normalized[SOLAR_OS_STORAGE_PATH_MAX];
    esp_err_t err = solar_os_storage_normalize_path(path, normalized,
                                                    sizeof(normalized));
    if (err != ESP_OK) {
        return err;
    }
    xSemaphoreTake(playlist.lock, portMAX_DELAY);
    for (size_t i = 0U; i < playlist.count; i++) {
        if (strcmp(playlist.tracks[i].path, normalized) == 0) {
            if (index != NULL) {
                *index = i;
            }
            xSemaphoreGive(playlist.lock);
            return ESP_OK;
        }
    }
    if (playlist.count >= SOLAR_OS_PLAYER_PLAYLIST_CAPACITY) {
        xSemaphoreGive(playlist.lock);
        return ESP_ERR_NO_MEM;
    }
    if (playlist.count == playlist.capacity) {
        const size_t capacity = playlist.capacity + 1U;
        void *tracks = solar_os_memory_realloc(
            playlist.tracks,
            capacity * sizeof(playlist.tracks[0]),
            SOLAR_OS_MEMORY_EXTERNAL_PREFERRED,
            "player.playlist");
        if (tracks == NULL) {
            xSemaphoreGive(playlist.lock);
            return ESP_ERR_NO_MEM;
        }
        playlist.tracks = tracks;
        playlist.capacity = capacity;
    }
    const size_t added = playlist.count++;
    strlcpy(playlist.tracks[added].path, normalized,
            sizeof(playlist.tracks[added].path));
    const uint32_t previous_generation = playlist.generation;
    playlist.generation = player_playlist_next_generation(previous_generation);
    err = player_playlist_save_locked();
    if (err != ESP_OK) {
        playlist.count--;
        playlist.generation = previous_generation;
    } else if (index != NULL) {
        *index = added;
    }
    xSemaphoreGive(playlist.lock);
    return err;
}

esp_err_t solar_os_player_playlist_remove(size_t index)
{
    if (!playlist.initialized || index >= playlist.count) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(playlist.lock, portMAX_DELAY);
    const solar_os_player_track_t removed = playlist.tracks[index];
    memmove(&playlist.tracks[index], &playlist.tracks[index + 1U],
            (playlist.count - index - 1U) * sizeof(playlist.tracks[0]));
    playlist.count--;
    const uint32_t previous_generation = playlist.generation;
    playlist.generation = player_playlist_next_generation(previous_generation);
    esp_err_t err = player_playlist_save_locked();
    if (err != ESP_OK) {
        memmove(&playlist.tracks[index + 1U], &playlist.tracks[index],
                (playlist.count - index) * sizeof(playlist.tracks[0]));
        playlist.tracks[index] = removed;
        playlist.count++;
        playlist.generation = previous_generation;
    }
    xSemaphoreGive(playlist.lock);
    return err;
}

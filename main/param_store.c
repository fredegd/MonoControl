#include "param_store.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "PARAM_STORE";

static param_t s_params[PARAM_MAX_COUNT];
static uint8_t s_param_count = PARAM_MAX_COUNT;
static uint8_t s_current_anim = 0;
static SemaphoreHandle_t s_mutex = NULL;

static const anim_profile_t s_anim_profiles[ANIM_MAX_COUNT] = {
    {
        .name = "Lissajous",
        .params = {
            { "← Back",       0.0f,  0.0f,   0.00f, 0.00f, 0 },
            { "freq A",       0.0f,  5.0f,   0.01f, 1.00f, 2 },
            { "freq B",       0.0f,  5.0f,   0.01f, 2.00f, 2 },
            { "fade",         0.005f,0.30f,  0.001f,0.05f, 3 },
            { "brightness",   0.0f,  1.0f,   0.01f, 0.80f, 2 },
            { "R",            0.0f,  1.0f,   0.01f, 0.50f, 2 },
            { "G",            0.0f,  1.0f,   0.01f, 0.50f, 2 },
            { "B",            0.0f,  1.0f,   0.01f, 0.50f, 2 },
            { "velocity",     0.0f,  2.0f,   0.01f, 0.25f, 2 },
            { "line width",   0.5f, 15.0f,   0.10f, 2.00f, 1 },
            { "shadow",       0.0f,100.0f,   1.00f,12.00f, 0 },
            { "phase",        0.0f,  6.28f,  0.01f, 0.00f, 2 },
            { "scale",        0.1f,  2.0f,   0.01f, 1.00f, 2 },
            { "x offset",    -1.0f,  1.0f,   0.01f, 0.00f, 2 },
            { "y offset",    -1.0f,  1.0f,   0.01f, 0.00f, 2 },
            { "rotation",     0.0f,360.0f,   1.00f, 0.00f, 0 },
            { "density",      1.0f,  8.0f,   1.00f, 4.00f, 0 },
        }
    },
    {
        .name = "Particle Flow",
        .params = {
            { "← Back",       0.0f,  0.0f,   0.00f, 0.00f, 0 },
            { "speed",        0.0f,  2.0f,   0.01f, 0.50f, 2 },
            { "size",         0.5f, 10.0f,   0.10f, 2.00f, 1 },
            { "fade",         0.005f,0.20f,  0.001f,0.02f, 3 },
            { "brightness",   0.0f,  1.0f,   0.01f, 0.80f, 2 },
            { "R",            0.0f,  1.0f,   0.01f, 0.70f, 2 },
            { "G",            0.0f,  1.0f,   0.01f, 0.30f, 2 },
            { "B",            0.0f,  1.0f,   0.01f, 0.50f, 2 },
            { "spawn rate",   1.0f, 20.0f,   1.00f, 5.00f, 0 },
            { "life span",   10.0f,200.0f,   1.00f,60.00f, 0 },
            { "gravity",     -0.5f,  0.5f,   0.01f, 0.00f, 2 },
            { "wind",        -0.5f,  0.5f,   0.01f, 0.00f, 2 },
            { "turbulence",   0.0f,  2.0f,   0.01f, 0.50f, 2 },
            { "scale",        0.1f,  2.0f,   0.01f, 1.00f, 2 },
            { "spread",       0.0f,  1.0f,   0.01f, 0.50f, 2 },
            { "rotation",     0.0f,360.0f,   1.00f, 0.00f, 0 },
            { "count",       50.0f,500.0f,  10.00f,200.00f,0 },
        }
    },
    {
        .name = "Hypnotic Spiral",
        .params = {
            { "← Back",       0.0f,  0.0f,   0.00f, 0.00f, 0 },
            { "speed",        0.0f,  2.0f,   0.01f, 0.30f, 2 },
            { "arms",         2.0f, 12.0f,   1.00f, 4.00f, 0 },
            { "fade",         0.005f,0.30f,  0.001f,0.03f, 3 },
            { "brightness",   0.0f,  1.0f,   0.01f, 0.90f, 2 },
            { "R",            0.0f,  1.0f,   0.01f, 0.90f, 2 },
            { "G",            0.0f,  1.0f,   0.01f, 0.20f, 2 },
            { "B",            0.0f,  1.0f,   0.01f, 0.60f, 2 },
            { "twist",        0.0f, 10.0f,   0.10f, 3.00f, 1 },
            { "thickness",    0.5f, 10.0f,   0.10f, 2.00f, 1 },
            { "pulse",        0.0f,  1.0f,   0.01f, 0.50f, 2 },
            { "color cycle",  0.0f,  1.0f,   0.01f, 0.00f, 2 },
            { "scale",        0.1f,  2.0f,   0.01f, 1.00f, 2 },
            { "x offset",    -1.0f,  1.0f,   0.01f, 0.00f, 2 },
            { "y offset",    -1.0f,  1.0f,   0.01f, 0.00f, 2 },
            { "rot offset",   0.0f,360.0f,   1.00f, 0.00f, 0 },
            { "complexity",   1.0f, 10.0f,   1.00f, 5.00f, 0 },
        }
    },
    {
        .name = "Wave Grid",
        .params = {
            { "← Back",       0.0f,  0.0f,   0.00f, 0.00f, 0 },
            { "speed",        0.0f,  2.0f,   0.01f, 0.50f, 2 },
            { "wave height",  0.0f,  1.0f,   0.01f, 0.40f, 2 },
            { "fade",         0.005f,0.30f,  0.001f,0.02f, 3 },
            { "brightness",   0.0f,  1.0f,   0.01f, 0.90f, 2 },
            { "R",            0.0f,  1.0f,   0.01f, 0.30f, 2 },
            { "G",            0.0f,  1.0f,   0.01f, 0.70f, 2 },
            { "B",            0.0f,  1.0f,   0.01f, 1.00f, 2 },
            { "grid size",    5.0f, 40.0f,   1.00f,20.00f, 0 },
            { "frequency",    0.5f,  5.0f,   0.10f, 1.50f, 1 },
            { "perspective",  0.0f,  1.0f,   0.01f, 0.30f, 2 },
            { "hue shift",    0.0f,  1.0f,   0.01f, 0.00f, 2 },
            { "scale",        0.1f,  2.0f,   0.01f, 1.00f, 2 },
            { "x offset",    -1.0f,  1.0f,   0.01f, 0.00f, 2 },
            { "y offset",    -1.0f,  1.0f,   0.01f, 0.00f, 2 },
            { "rotation",     0.0f,360.0f,   1.00f, 0.00f, 0 },
            { "line weight",  0.5f,  5.0f,   0.10f, 1.00f, 1 },
        }
    },
    {
        .name = "Game of Life",
        .params = {
            { "← Back",       0.0f,  0.0f,   0.00f, 0.00f, 0 },
            { "speed",        0.0f,  2.0f,   0.01f, 0.50f, 2 },
            { "density",      0.0f,  1.0f,   0.01f, 0.30f, 2 },
            { "fade",         0.005f,0.30f,  0.001f,0.05f, 3 },
            { "brightness",   0.0f,  1.0f,   0.01f, 0.90f, 2 },
            { "R",            0.0f,  1.0f,   0.01f, 0.10f, 2 },
            { "G",            0.0f,  1.0f,   0.01f, 0.80f, 2 },
            { "B",            0.0f,  1.0f,   0.01f, 0.30f, 2 },
            { "grid size",   10.0f,120.0f,   1.00f,40.00f, 0 },
            { "wrap",         0.0f,  1.0f,   1.00f, 1.00f, 0 },
            { "birth min",    0.0f,  8.0f,   1.00f, 3.00f, 0 },
            { "birth max",    0.0f,  8.0f,   1.00f, 3.00f, 0 },
            { "survive min",  0.0f,  8.0f,   1.00f, 2.00f, 0 },
            { "survive max",  0.0f,  8.0f,   1.00f, 3.00f, 0 },
            { "cell size",    1.0f, 12.0f,   1.00f, 3.00f, 0 },
            { "glow",         0.0f, 40.0f,   1.00f, 8.00f, 0 },
            { "reseed",       0.0f,  1.0f,   0.01f, 0.00f, 2 },
        }
    },
};

static void save_params_to_nvs(const param_t *params, uint8_t count, const char *ns_name) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed for %s: %s", ns_name, esp_err_to_name(err));
        return;
    }
    for (uint8_t i = 0; i < count; i++) {
        if (i == 0) continue; // skip virtual back entry
        char key[16];
        snprintf(key, sizeof(key), "p%02u", i);
        float val = params[i].value;
        nvs_set_blob(nvs_handle, key, &val, sizeof(float));
    }
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

static void load_params_from_nvs(param_t *params, uint8_t count, const char *ns_name) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed for %s: %s", ns_name, esp_err_to_name(err));
        return;
    }
    for (uint8_t i = 0; i < count; i++) {
        if (i == 0) continue; // skip virtual back entry
        char key[16];
        snprintf(key, sizeof(key), "p%02u", i);
        float val = 0.0f;
        size_t required_size = sizeof(float);
        err = nvs_get_blob(nvs_handle, key, &val, &required_size);
        if (err == ESP_OK && required_size == sizeof(float)) {
            if (val >= params[i].min && val <= params[i].max) {
                params[i].value = val;
                ESP_LOGI(TAG, "Loaded %s[%s] = %.2f from NVS", ns_name, params[i].name, val);
            } else {
                ESP_LOGW(TAG, "NVS value %.2f for %s[%s] out of bounds", val, ns_name, params[i].name);
            }
        }
    }
    nvs_close(nvs_handle);
}

esp_err_t param_store_init(void) {
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex during init");
        return ESP_ERR_TIMEOUT;
    }

    // Read current animation index from NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(PARAM_NVS_NS, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        uint8_t anim_idx = 0;
        size_t sz = sizeof(uint8_t);
        if (nvs_get_blob(nvs_handle, "cur_anim", &anim_idx, &sz) == ESP_OK && sz == sizeof(uint8_t)) {
            if (anim_idx < ANIM_MAX_COUNT) {
                s_current_anim = anim_idx;
                ESP_LOGI(TAG, "Loaded anim index %d from NVS", s_current_anim);
            }
        }
        nvs_close(nvs_handle);
    }

    // Copy default params for current animation
    s_param_count = PARAM_MAX_COUNT;
    memcpy(s_params, s_anim_profiles[s_current_anim].params, s_param_count * sizeof(param_t));

    // Load persisted values from NVS for this animation
    char ns_name[16];
    snprintf(ns_name, sizeof(ns_name), "params_%u", s_current_anim);
    load_params_from_nvs(s_params, s_param_count, ns_name);

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Initialized: anim=%d (%s)", s_current_anim, s_anim_profiles[s_current_anim].name);
    return ESP_OK;
}

uint8_t param_store_count(void) {
    return s_param_count;
}

esp_err_t param_store_set_value(uint8_t idx, float value) {
    if (idx >= s_param_count) {
        return ESP_ERR_INVALID_ARG;
    }

    float clamped_val = value;
    if (clamped_val < s_params[idx].min) clamped_val = s_params[idx].min;
    if (clamped_val > s_params[idx].max) clamped_val = s_params[idx].max;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_params[idx].value = clamped_val;
    xSemaphoreGive(s_mutex);

    // Persist to animation-specific NVS namespace
    char ns_name[16];
    snprintf(ns_name, sizeof(ns_name), "params_%u", s_current_anim);
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ns_name, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        char key[16];
        snprintf(key, sizeof(key), "p%02u", idx);
        nvs_set_blob(nvs_handle, key, &clamped_val, sizeof(float));
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    return ESP_OK;
}

esp_err_t param_store_get_value(uint8_t idx, float *out_value) {
    if (idx >= s_param_count || out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *out_value = s_params[idx].value;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t param_store_get_snapshot(param_t *dst, uint8_t *out_count) {
    if (dst == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memcpy(dst, s_params, s_param_count * sizeof(param_t));
    *out_count = s_param_count;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t param_store_switch_anim(uint8_t anim_idx) {
    if (anim_idx >= ANIM_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Save current params to old animation's NVS namespace
    char old_ns[16];
    snprintf(old_ns, sizeof(old_ns), "params_%u", s_current_anim);
    save_params_to_nvs(s_params, s_param_count, old_ns);

    // Switch to new animation
    s_current_anim = anim_idx;
    memcpy(s_params, s_anim_profiles[anim_idx].params, s_param_count * sizeof(param_t));

    // Load persisted values for new animation
    char new_ns[16];
    snprintf(new_ns, sizeof(new_ns), "params_%u", anim_idx);
    load_params_from_nvs(s_params, s_param_count, new_ns);

    // Save current animation index
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(PARAM_NVS_NS, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        uint8_t idx = anim_idx;
        nvs_set_blob(nvs_handle, "cur_anim", &idx, sizeof(uint8_t));
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Switched to anim %d (%s)", anim_idx, s_anim_profiles[anim_idx].name);
    return ESP_OK;
}

uint8_t param_store_get_current_anim(void) {
    return s_current_anim;
}

uint8_t param_store_get_anim_count(void) {
    return ANIM_MAX_COUNT;
}

const anim_profile_t* param_store_get_anim_profile(uint8_t idx) {
    if (idx >= ANIM_MAX_COUNT) return NULL;
    return &s_anim_profiles[idx];
}

const anim_profile_t* param_store_get_anim_profiles(void) {
    return s_anim_profiles;
}

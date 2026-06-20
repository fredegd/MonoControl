#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define PARAM_MAX_COUNT   17
#define PARAM_NAME_LEN    24
#define PARAM_NVS_NS      "param_store"
#define ANIM_MAX_COUNT    5

typedef struct {
    char    name[PARAM_NAME_LEN];
    float   min;
    float   max;
    float   step;
    float   value;
    uint8_t decimals;
} param_t;

typedef struct {
    char    name[PARAM_NAME_LEN];
    param_t params[PARAM_MAX_COUNT];
} anim_profile_t;

esp_err_t param_store_init(void);
uint8_t param_store_count(void);
esp_err_t param_store_set_value(uint8_t idx, float value);
esp_err_t param_store_get_value(uint8_t idx, float *out_value);
esp_err_t param_store_get_snapshot(param_t *dst, uint8_t *out_count);

esp_err_t param_store_switch_anim(uint8_t anim_idx);
uint8_t param_store_get_current_anim(void);
uint8_t param_store_get_anim_count(void);
const anim_profile_t* param_store_get_anim_profile(uint8_t idx);
const anim_profile_t* param_store_get_anim_profiles(void);

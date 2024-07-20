#pragma once

#include "esp_err.h"

#define MAX_CATEGORY_LABELS 8

struct tf_model_ctx {
    int label_count;
    char *labels[MAX_CATEGORY_LABELS];

    size_t data_len;
    uint8_t *data;
};

struct tf_model_ctx *model_init_from_file(char *path);
esp_err_t model_free(struct tf_model_ctx *ctx);

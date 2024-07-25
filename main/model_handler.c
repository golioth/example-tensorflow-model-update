#include "esp_err.h"
#include "esp_log.h"
#include "model_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

static const char *TAG = "model_handler";

#define MAX_HEADER_LEN 128
#define HEADER_START "GLTHBEGIN"
#define HEADER_END "GLTHEND"

static esp_err_t add_category(struct tf_model_ctx *ctx, char *str, size_t len)
{
    if (ctx->label_count == MAX_CATEGORY_LABELS)
    {
        ESP_LOGE(TAG, "Max categories reached, dropping this one.");
        return ESP_ERR_NO_MEM;
    }

    char *word = (char *) calloc(len + 1, sizeof(char));
    if (!word)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for category");
        return ESP_ERR_NO_MEM;
    }

    snprintf(word, len + 1, "%s", str);
    ctx->labels[ctx->label_count] = word;

    ESP_LOGD(TAG, "Category %d: %s", ctx->label_count, ctx->labels[ctx->label_count]);

    /* Safe to increment, max - 1 was checked at top of function */
    ctx->label_count++;
    return ESP_OK;
}

static esp_err_t ingest_header(struct tf_model_ctx *ctx, char *header, size_t header_len)
{
    if (header_len < (strlen(HEADER_START) + strlen(HEADER_END)))
    {

        ESP_LOGE(TAG, "Header too small to be valid: %zu", header_len);
        return ESP_ERR_INVALID_ARG;
    }

    bool found_start = false;
    char *token;
    header[header_len - 1] = '\0';
    token = strtok(header, ";");

    ctx->label_count = 0;

    while (token != NULL)
    {
        if (!found_start)
        {
            if (strncmp(token, HEADER_START, strlen(HEADER_START)) == 0)
            {
                found_start = true;
                ESP_LOGD(TAG, "Found header start");
            }
            else
            {
                ESP_LOGE(TAG, "Header start not found");
                return ESP_ERR_INVALID_ARG;
            }
        }
        else if (strncmp(token, HEADER_END, strlen(HEADER_END)) == 0)
        {
            ESP_LOGD(TAG, "Found header end");
            return ESP_OK;
        }
        else
        {
            ESP_LOGD(TAG, "Token Found: %s", token);
            int err = add_category(ctx, token, strlen(token));
            if (err)
            {
                return err;
            }
        }
        token = strtok(NULL, ";");
    }

    ESP_LOGE(TAG, "Header end not found.");
    return ESP_ERR_INVALID_ARG;
}

struct tf_model_ctx *model_init_from_file(char *path)
{
    if (!path)
    {
        ESP_LOGE(TAG, "Path is NULL");
        return NULL;
    }

    int model_offset = 0;
    size_t model_size = 0;
    unsigned char *new_data = NULL;
    struct tf_model_ctx *ctx = NULL;
    struct stat st;

    if (stat(path, &st) != 0)
    {
        ESP_LOGE(TAG, "File not found: %s", path);
        return NULL;
    }

    FILE *f = fopen(path, "r");
    if (!f)
    {
        ESP_LOGE(TAG, "Unable to open model file");
        return NULL;
    }

    /* Read header from file; establish starting index of model data */
    char header[MAX_HEADER_LEN];
    for (int i = 0; i < MAX_HEADER_LEN; i++)
    {
        fread(header + i, 1, 1, f);
        if (header[i] == '\n')
        {
            model_offset = i + 1;
            model_size = st.st_size - model_offset;
            ESP_LOGD(TAG,
                     "Found header; Model starts at %d with size %zu",
                     model_offset,
                     model_size);
            break;
        }

        if (feof(f))
        {
            ESP_LOGE(TAG, "Reached end of file but no header found");
            goto model_load_error;
        }
    }

    if (model_offset == 0)
    {
        ESP_LOGE(TAG, "Can't find header in model file");
        goto model_load_error;
    }

    /* Create new context; initialize to 0 to help in freeing memory later */
    ctx = (struct tf_model_ctx *) calloc(1, sizeof(struct tf_model_ctx));

    /* Populate model labels */
    esp_err_t err = ingest_header(ctx, header, model_offset);
    if (err)
    {
        goto model_load_error;
    }

    ESP_LOG_BUFFER_HEXDUMP(TAG, header, model_offset, ESP_LOG_DEBUG);

    /* Populate model data */
    new_data = (unsigned char *) malloc(sizeof(unsigned char) * model_size);
    if (!new_data)
    {
        ESP_LOGE(TAG, "Unable to allocate memory of size: %zu", model_size);
        goto model_load_error;
    }

    /* File position should already be at the start of the model */
    size_t bytes_read = fread(new_data, 1, model_size, f);
    if (bytes_read != model_size)
    {
        ESP_LOGE(TAG,
                 "Error copying model. Copied %zu but expected to copy %zu",
                 bytes_read,
                 model_size);
        goto model_load_error;
    }

    ctx->data_len = model_size;
    ctx->data = new_data;

    ESP_LOGI(TAG, "Loaded model from %s", path);
    ESP_LOGI(TAG, "Model label count: %d", ctx->label_count);
    for (int i = 0; i < ctx->label_count; i++)
    {
        ESP_LOGI(TAG, "Label: %s", ctx->labels[i]);
    }

    fclose(f);
    return ctx;

model_load_error:
    model_free(ctx);
    fclose(f);
    return NULL;
}

esp_err_t model_free(struct tf_model_ctx *ctx)
{
    if (!ctx)
    {
        ESP_LOGW(TAG, "Context is NULL, nothing to free!");
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < MAX_CATEGORY_LABELS; i++)
    {
        free(ctx->labels[i]);
    }

    free(ctx->data);
    free(ctx);

    return ESP_OK;
}

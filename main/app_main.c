/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

static const char *TAG = "golioth_tensorflow";

/* Golioth */
#include "nvs.h"
#include "shell.h"
#include "wifi.h"
#include "sample_credentials.h"
#include <golioth/client.h>
#include <golioth/stream.h>
#include <golioth/ota.h>

#include "bsp/m5stack_core_s3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "model_handler.h"
#include <sys/stat.h>

/* Component Queue */
#define QUEUE_LENGTH CONFIG_GOLIOTH_OTA_MAX_NUM_COMPONENTS
#define QUEUE_ITEM_SIZE sizeof(struct golioth_ota_component *)
static StaticQueue_t xStaticQueue;
static uint8_t ucQueueStorageArea[QUEUE_LENGTH * QUEUE_ITEM_SIZE];
static QueueHandle_t xQueue;

#define SD_MOUNT_POINT "/sdcard"
#define MODEL_PACKAGE_NAME "model"
static char *selected_model_path = NULL;
static bool new_model_available = false;

static SemaphoreHandle_t _connected_sem = NULL;

static void on_client_event(struct golioth_client *client,
                            enum golioth_client_event event,
                            void *arg)
{
    bool is_connected = (event == GOLIOTH_CLIENT_EVENT_CONNECTED);
    if (is_connected)
    {
        xSemaphoreGive(_connected_sem);
    }
    GLTH_LOGI(TAG, "Golioth client %s", is_connected ? "connected" : "disconnected");
}

static void on_manifest(struct golioth_client *client,
                        const struct golioth_response *response,
                        const char *path,
                        const uint8_t *payload,
                        size_t payload_size,
                        void *arg)
{
    struct golioth_ota_manifest man;

    int err = golioth_ota_payload_as_manifest(payload, payload_size, &man);

    if (err)
    {
        GLTH_LOGE(TAG,
                  "Error converting payload to manifest: %d (check GOLIOTH_OTA_MAX_NUM_COMPONENTS)",
                  err);
        return;
    }

    for (int i = 0; i < man.num_components; i++)
    {
        GLTH_LOGI(TAG, "Package found: %s", man.components[i].package);

        size_t pn_len = strlen(man.components[i].package);
        if (pn_len > CONFIG_GOLIOTH_OTA_MAX_PACKAGE_NAME_LEN)
        {
            GLTH_LOGE(TAG,
                      "Package name length limited to %d but got %zu",
                      CONFIG_GOLIOTH_OTA_MAX_PACKAGE_NAME_LEN,
                      pn_len);
        }
        else if (strcmp(MODEL_PACKAGE_NAME, man.components[i].package) != 0)
        {
            GLTH_LOGI(TAG, "Skipping download for package name: %s", man.components[i].package);
        }
        else
        {
            GLTH_LOGI(TAG, "Queueing for download: %s", man.components[i].package);

            struct golioth_ota_component *stored_component =
                (struct golioth_ota_component *) malloc(sizeof(struct golioth_ota_component));

            if (!stored_component)
            {
                GLTH_LOGE(TAG, "Unable to allocate memory to store component");
                continue;
            }

            memcpy(stored_component, &man.components[i], sizeof(struct golioth_ota_component));

            err = xQueueSendToBackFromISR(xQueue, &stored_component, NULL);
            if (err != pdPASS)
            {
                GLTH_LOGE(TAG, "Failed to enqueue component: %d", err);
            }
        }
    }
}

static char *store_model_path(char *path, size_t len)
{
    char *model_path = (char *) calloc(len + 1, sizeof(char));
    if (!model_path)
    {
        ESP_LOGE(TAG, "Failed to allocate memory to store path");
        return NULL;
    }

    snprintf(model_path, strlen(path) + 1, "%s", path);

    return model_path;
}

static enum golioth_status write_artifact_block(const struct golioth_ota_component *component,
                                                uint32_t block_idx,
                                                uint8_t *block_buffer,
                                                size_t block_size,
                                                bool is_last,
                                                void *arg)
{

    if (!arg)
    {
        GLTH_LOGE(TAG, "arg is NULL but should be a file stream");
        return GOLIOTH_ERR_INVALID_FORMAT;
    }
    FILE *f = (FILE *) arg;

    fwrite(block_buffer, block_size, 1, f);

    if (is_last)
    {
        GLTH_LOGI(TAG, "Block download complete!");
    }

    return GOLIOTH_OK;
}

static void init_package_queue(void)
{
    xQueue = xQueueCreateStatic(QUEUE_LENGTH, QUEUE_ITEM_SIZE, ucQueueStorageArea, &xStaticQueue);
    assert(xQueue);
}

static void download_packages_in_queue(struct golioth_client *client)
{
    char *new_path = NULL;

    while (uxQueueMessagesWaiting(xQueue))
    {
        struct golioth_ota_component *component = NULL;
        FILE *f = NULL;

        if (xQueueReceive(xQueue, &component, 0) == pdFALSE || !component)
        {
            GLTH_LOGE(TAG, "Failed to receive from queue");
            continue;
        }

        /* Store components with name_version format: "componentname_1.2.3" */
        size_t path_len = sizeof(SD_MOUNT_POINT) + strlen("_") + strlen(component->package)
            + strlen("_xxx.xxx.xxx") + strlen("\0");

        char path[path_len];
        snprintf(path,
                 sizeof(path),
                 "%s/%s_%s",
                 SD_MOUNT_POINT,
                 component->package,
                 component->version);

        /* Server has told us this is the most recent release, use it as the selected model */
        if (strncmp(component->package, MODEL_PACKAGE_NAME, strlen(MODEL_PACKAGE_NAME)) == 0)
        {
            free(new_path);
            new_path = store_model_path(path, strlen(path));
        }

        /* Check if file exists */
        struct stat st;
        if (stat(path, &st) == 0)
        {
            GLTH_LOGI(TAG, "Package already exists on SD card: %s", path);
            goto package_cleanup;
        }
        else
        {
            GLTH_LOGI(TAG, "Opening file for writing: %s", path);
            f = fopen(path, "a");
            if (!f)
            {
                GLTH_LOGE(TAG, "Error opening file");
                goto package_cleanup;
            }
        }

        golioth_ota_download_component(client, component, write_artifact_block, (void *) f);

    package_cleanup:
        fclose(f);
        free(component);
    }

    if (new_path)
    {
        free(selected_model_path);
        selected_model_path = new_path;
        new_model_available = true;
    }
}

void app_main(void)
{
    GLTH_LOGI(TAG, "Start Golioth TensorFlow model update example");

    init_package_queue();
    bsp_sdcard_mount();

    /* Golioth connection */
    /* Get credentials from NVS and enable shell */
    nvs_init();
    shell_start();

    if (!nvs_credentials_are_set())
    {
        GLTH_LOGW(TAG,
                  "WiFi and Golioth credentials are not set. "
                  "Use the shell settings commands to set them.");

        while (!nvs_credentials_are_set())
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    /* Initialize WiFi and wait for it to connect */
    wifi_init(nvs_read_wifi_ssid(), nvs_read_wifi_password());
    wifi_wait_for_connected();

    /* Connect to Golioth */
    const struct golioth_client_config *config = golioth_sample_credentials_get();
    struct golioth_client *client = golioth_client_create(config);
    _connected_sem = xSemaphoreCreateBinary();
    golioth_client_register_event_callback(client, on_client_event, NULL);

    /* Listen for OTA manifest */
    int err = golioth_ota_observe_manifest_async(client, on_manifest, NULL);
    if (err)
    {
        GLTH_LOGE(TAG, "Unable to observe manifest");
    }

    GLTH_LOGW(TAG, "Waiting for connection to Golioth...");
    xSemaphoreTake(_connected_sem, portMAX_DELAY);

    struct tf_model_ctx *model_context = NULL;
    ESP_LOGI(TAG, "Awaiting version information from server before loading a TensorFlow model");

    int counter = 0;

    while (true)
    {
        download_packages_in_queue(client);

        if (new_model_available)
        {
            new_model_available = false;
            model_free(model_context);
            model_context = model_init_from_file(selected_model_path);
            if (model_context != NULL)
            {
                ESP_LOGI(TAG, "Model loaded from SD card.");
                /* TODO: Set up TensorFlow */
            }
        }

        /* TODO: Replace counter and delay with TensorFlow voice recogntion */
        GLTH_LOGI(TAG, "Sending hello! %d", counter);
        ++counter;
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

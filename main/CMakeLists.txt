set(esp_idf_common "../submodules/golioth-firmware-sdk/examples/esp_idf/common")

set(tflite_micro_speech_srcs
        "../tf_micro_speech/main/main_functions.cc"
        "../tf_micro_speech/main/audio_provider.cc"
        "../tf_micro_speech/main/feature_provider.cc"
        "../tf_micro_speech/main/micro_features_generator.cc"
        "../tf_micro_speech/main/ringbuf.c"
        )

set(tflite_micro_speech_priv_reqs
        "spi_flash"
        "driver"
        "esp_timer"
        )

idf_component_register(SRCS
                        "app_main.c"
                        "model_handler.c"
                        "${esp_idf_common}/shell.c"
                        "${esp_idf_common}/wifi.c"
                        "${esp_idf_common}/nvs.c"
                        "${esp_idf_common}/sample_credentials.c"
                        "${tflite_micro_speech_srcs}"

                    INCLUDE_DIRS
                        "${esp_idf_common}"
                        "."

                    PRIV_REQUIRES
                        "golioth_sdk"
                        "console"
                        "fatfs"
                        "spi_flash"
                        "nvs_flash"
                        "json"
                        "driver"
                        "esp_hw_support"
                        "esp_wifi"
                        "${tflite_micro_speech_priv_reqs}"
                        )

# From the TFlite micro_speech example:
    # Reduce the level of paranoia to be able to compile sources
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-maybe-uninitialized
    -Wno-missing-field-initializers
    -Wno-error=sign-compare
    -Wno-error=double-promotion
    -Wno-type-limits)

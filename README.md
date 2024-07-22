# Golioth TensorFlow Lite Model Update Example

## Overview

This application downloads a TensorFlow Lite model from the Golioth
servers and uses it to recognize spoken words using an i2s microphone.

* Compile and load the application on an m5stack CoreS3 device
* Upload the included TensorFlow models as Golioth artifacts
* Roll out a release with your desired artifact; the device will no be
  able to recognize simple words (eg: yes, no)
* Release the other artifact to load a new model that recognizes
  additional words (stop, go)

Use this application as a reference when adding multi-artifact download
to update trained models on your machine-learning applications.

## Local Setup

```
git submodule add https://github.com/golioth/golioth-firmware-sdk.git submodules/golioth-firmware-sdk
cd example-tensorflow-model-update
git submodule update --init --recursive
```

## Building the Application and Assign Credentials

### Build for m5stack CoreS3

```
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

### Provisioning

```
esp32> settings set wifi/ssid <my-wifi-ssid>
esp32> settings set wifi/psk <my-wifi-psk>
esp32> settings set golioth/psk-id <my-psk-id@my-project>
esp32> settings set golioth/psk <my-psk>
esp32> kernel reboot cold
```

## TensorFlow Lite Models

TensorFlow Lite models are found in the `models` directory.

1. Upload these models to Golioth as `artifacts` using the name `model`
2. Create and rollout a release that includes a model artifact
3. The most recent release that includes a `model` artifact will be used
   by the device as the currently selected TensorFlow Lite model.

### Available Models

* `model.bin_header_yn`: Trained to recognize `yes`, and `no`
* `model.bin_header_ynsg`: Trained to recognize `yes`, `no`, `stop`, and
  `go`

### Model Formatting

Models may be trained by following the [tflite-micro Micro Speech
Training
guide](https://github.com/tensorflow/tflite-micro/blob/main/tensorflow/lite/micro/examples/micro_speech/train/README.md).

The `models/model.tflite` binary file output during the training process
is given a custom header that indicates the text labels used during the
training. Here is an example of adding a header to the training data:

```
echo "GLTHBEGIN;silence;unknown;yes;no;GLTHEND" > model.bin_header_yn
cat models/model.tflite >> model.bin_header_yn
```

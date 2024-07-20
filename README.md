# Golioth TensorFlow Lite Model Update Example

## Overview

This application download a TensorFlow Lite model from the Golioth
servers and uses it to recognize spoken words using an i2c microphone.

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

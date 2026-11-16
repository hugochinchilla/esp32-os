#!/bin/sh
# Installs ESP-IDF v5.5 for the ESP32-S3 in a cloud container (about 4.5 GB,
# ten minutes), so firmware can at least be compiled and linked there.
#
#   tools/setup-cloud-idf.sh
#   . tools/setup-cloud-idf.sh env       # in each new shell, then:
#   cd firmware/os && idf.py -B /tmp/build-fw build
#
# dl.espressif.com is not reachable from the container, so the Python
# packages are installed without Espressif's version constraints file.
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$HOME/.espressif}"
export IDF_PYTHON_CHECK_CONSTRAINTS=no
IDF_DIR="${IDF_DIR:-$HOME/esp-idf}"

if [ "$1" = "env" ]; then
    . "$IDF_DIR/export.sh"
else
    [ -d "$IDF_DIR" ] || git clone --depth 1 --branch v5.5 --recurse-submodules --shallow-submodules \
        -j8 https://github.com/espressif/esp-idf.git "$IDF_DIR" || exit 1
    python3 "$IDF_DIR/tools/idf_tools.py" install --targets esp32s3 || exit 1
    python3 "$IDF_DIR/tools/idf_tools.py" install-python-env || exit 1
    echo "done: . tools/setup-cloud-idf.sh env"
fi

#!/bin/sh
# Installs ESP-IDF v5.5 for the ESP32-S3 in a cloud container (about 4.5 GB,
# ten minutes), so firmware can at least be compiled and linked there.
#
#   tools/setup-cloud-idf.sh
#   . tools/setup-cloud-idf.sh env       # in each new shell, then:
#   cd firmware/os && idf.py -B /tmp/build-fw build
#
# Espressif's own servers are not reachable from the container, so the
# Python packages are installed without Espressif's version constraints
# file, and LittleFS, mDNS and the ELF loader come from GitHub instead of the component registry
# (into firmware/os/components/, which is not committed; "env" also turns
# the component manager off).
export IDF_TOOLS_PATH="${IDF_TOOLS_PATH:-$HOME/.espressif}"
export IDF_PYTHON_CHECK_CONSTRAINTS=no
IDF_DIR="${IDF_DIR:-$HOME/esp-idf}"

if [ "$1" = "env" ]; then
    export IDF_COMPONENT_MANAGER=0
    . "$IDF_DIR/export.sh"
else
    [ -d "$IDF_DIR" ] || git clone --depth 1 --branch v5.5 --recurse-submodules --shallow-submodules \
        -j8 https://github.com/espressif/esp-idf.git "$IDF_DIR" || exit 1
    python3 "$IDF_DIR/tools/idf_tools.py" install --targets esp32s3 || exit 1
    python3 "$IDF_DIR/tools/idf_tools.py" install-python-env || exit 1
    HERE="$(cd "$(dirname "$0")/.." && pwd)"
    [ -d "$HERE/firmware/os/components/joltwallet__littlefs" ] || git clone --depth 1 --branch v1.23.1 \
        --recurse-submodules --shallow-submodules https://github.com/joltwallet/esp_littlefs.git \
        "$HERE/firmware/os/components/joltwallet__littlefs" || exit 1
    if [ ! -d "$HERE/firmware/os/components/espressif__mdns" ]; then
        git clone --depth 1 --branch mdns-v1.15.1 https://github.com/espressif/esp-protocols.git \
            "$HOME/esp-protocols" || exit 1
        cp -r "$HOME/esp-protocols/components/mdns" "$HERE/firmware/os/components/espressif__mdns" || exit 1
    fi
    # The ELF loader (backlog item 27) and the helper component it asks for.
    if [ ! -d "$HERE/firmware/os/components/espressif__elf_loader" ]; then
        [ -d "$HOME/esp-iot-solution" ] || git clone --depth 1 --filter=blob:none --sparse \
            https://github.com/espressif/esp-iot-solution.git "$HOME/esp-iot-solution" || exit 1
        (cd "$HOME/esp-iot-solution" && git sparse-checkout set components/elf_loader tools/cmake_utilities) || exit 1
        cp -r "$HOME/esp-iot-solution/components/elf_loader" "$HERE/firmware/os/components/espressif__elf_loader" || exit 1
        cp -r "$HOME/esp-iot-solution/tools/cmake_utilities" "$HERE/firmware/os/components/espressif__cmake_utilities" || exit 1
    fi
    echo "done: . tools/setup-cloud-idf.sh env"
fi

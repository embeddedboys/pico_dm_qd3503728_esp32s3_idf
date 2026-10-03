# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_STANDARD 17)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(pud_qd3503728_esp32s3)

idf_build_set_property(COMPILE_OPTIONS "-Wno-array-bounds" APPEND)

# CherryUSB v1.5.2's osal/idf/usb_config.h defines USB_NOCACHE_RAM_SECTION as
# DRAM_DMA_ALIGNED_ATTR, which only exists in ESP-IDF >= 5.3.  Map it to the
# 5.2 equivalent (word-aligned internal SRAM) so the vendored component stays
# untouched.
idf_build_set_property(COMPILE_OPTIONS "-DDRAM_DMA_ALIGNED_ATTR=DMA_ATTR" APPEND)

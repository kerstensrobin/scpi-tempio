cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME tempio)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# TinyUSB (git submodule in STM/lib/tinyusb)
set(TINYUSB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/tinyusb)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    App/Src/app.c
    App/Src/dio.c
    App/Src/scpi.c
    App/Src/sht4x.c
    App/Src/usb_descriptors.c
    App/Src/usb_hw.c
    ${TINYUSB_DIR}/src/tusb.c
    ${TINYUSB_DIR}/src/common/tusb_fifo.c
    ${TINYUSB_DIR}/src/device/usbd.c
    ${TINYUSB_DIR}/src/class/cdc/cdc_device.c
    ${TINYUSB_DIR}/src/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
    ${TINYUSB_DIR}/src/portable/st/stm32_fsdev/fsdev_common.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    App/Inc
    ${TINYUSB_DIR}/src
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

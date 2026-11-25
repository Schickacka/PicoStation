# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
# Allow an external SDK (for example ./tools/pico-sdk) without breaking the
# normal submodule-based build.
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-sdk/")
endif()

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Example override variant
# set(PICOSTATION_VARIANT "picostation_plus_pico2")

if(NOT DEFINED PICOSTATION_VARIANT)
    if(PICO_BOARD STREQUAL "pico2")
        set(PICOSTATION_VARIANT "picostation_pico2")
    else()
        set(PICOSTATION_VARIANT "picostation_pico1")
    endif()
    message(STATUS "NOTE: PicoStation variant not defined. Using ${PICOSTATION_VARIANT}")
endif()

include(boards/picostation_variant.cmake)

cmake_minimum_required(VERSION 3.24.1)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(pico_sdk_import.cmake)

project(${PROJECT_NAME})

pico_sdk_init()

set(CMAKE_BUILD_TYPE Release)

function(addBinaryFileWithSize target name sizeName path)
    set(asmFile "${PROJECT_BINARY_DIR}/includes/${target}_${name}.s")
    cmake_path(ABSOLUTE_PATH path OUTPUT_VARIABLE fullPath)

    file(
        CONFIGURE
        OUTPUT "${asmFile}"
        CONTENT [[
            .section .rodata.${name}, "a"
            .balign 8

            .global ${name}
            .type ${name}, %object
            .size ${name}, (${name}_end - ${name})

            ${name}:
                .incbin "${fullPath}"
            ${name}_end:

            .section .rodata.${sizeName}, "a"
            .balign 4

            .global ${sizeName}
            .type ${sizeName}, %object
            .size ${sizeName}, 4

            ${sizeName}:
                .int (${name}_end - ${name})
        ]]
        ESCAPE_QUOTES
        NEWLINE_STYLE LF
    )

    target_sources(${target} PRIVATE "${asmFile}")
    set_source_files_properties(
        "${asmFile}" PROPERTIES OBJECT_DEPENDS "${fullPath}"
    )
endfunction()

add_executable(${PROJECT_NAME})

target_compile_definitions(
    ${PROJECT_NAME} PUBLIC
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_TX_PIN=0
    PICO_DEFAULT_UART_RX_PIN=1
    PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64
    MAXINDEX=2
)

# The official RC5 release keeps the 0xbeef SubQ CRC placeholder. Enabling
# this is experimental and should only be used for hardware validation.
option(PICOSTATION_SUBQ_CRC "Enable experimental SubQ CRC-16 generation" OFF)
if(PICOSTATION_SUBQ_CRC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PICOSTATION_SUBQ_CRC=1)
endif()

# SCEX region: 0 = SCEA (NTSC-U), 1 = SCEE (PAL), 2 = SCEI (NTSC-J),
# 3 = universal (all three once, the current default).
set(PICOSTATION_SCEX_REGION 3 CACHE STRING "SCEX region: 0=SCEA, 1=SCEE, 2=SCEI, 3=universal")
target_compile_definitions(${PROJECT_NAME} PRIVATE PICOSTATION_SCEX_REGION=${PICOSTATION_SCEX_REGION})

# Target PS1 motherboard profile (primary deployment target: PU-18, later PU-8).
set(PICOSTATION_MAINBOARD "PU18" CACHE STRING "Target PS1 motherboard: PU18 or PU8")
if(PICOSTATION_MAINBOARD STREQUAL "PU18")
    target_compile_definitions(${PROJECT_NAME} PRIVATE PICOSTATION_MAINBOARD_PU18=1)
elseif(PICOSTATION_MAINBOARD STREQUAL "PU8")
    target_compile_definitions(${PROJECT_NAME} PRIVATE PICOSTATION_MAINBOARD_PU8=1)
else()
    message(FATAL_ERROR "Unsupported PICOSTATION_MAINBOARD='${PICOSTATION_MAINBOARD}'. Expected PU18 or PU8.")
endif()

option(PICOSTATION_OVERCLOCK "Enable Mechacon overclock profile (_OC)" OFF)
if(PICOSTATION_OVERCLOCK)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PICOSTATION_OVERCLOCK=1)
endif()

option(PICOSTATION_SCEX_OPEN_DRAIN "Use open-drain/Hi-Z for SCEX high level (safer with IC706 on PU-18)" OFF)
if(PICOSTATION_SCEX_OPEN_DRAIN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE PICOSTATION_SCEX_OPEN_DRAIN=1)
endif()

set(PICOSTATION_MENU_BIN "picomenu/picostation-menu.bin" CACHE STRING "Path to embedded PS1 menu image")
addBinaryFileWithSize(${PROJECT_NAME} loaderImage loaderImageSize ${PICOSTATION_MENU_BIN})

target_sources(
    ${PROJECT_NAME} PRIVATE
    src/cmd.cpp
    src/disc_image.cpp
    src/drive_mechanics.cpp
    src/edc.c
    src/i2s.cpp
    src/main.cpp
    src/modchip.cpp
    src/picostation.cpp
    src/subq.cpp
    src/directory_listing.cpp
    src/si5351.c
    third_party/cueparser/cueparser.c
    third_party/cueparser/fileabstract.c
    third_party/cueparser/scheduler.c
    third_party/posix_file.c
)

target_include_directories(
    ${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/third_party
)

add_subdirectory(third_party/SD-fatfs build)

# Prefer the git submodule, but allow a local ./tools checkout for development
# environments where the submodule was not initialized.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/third_party/RP2040_Pseudo_Atomic/CMakeLists.txt")
    add_subdirectory(third_party/RP2040_Pseudo_Atomic)
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/tools/RP2040_Pseudo_Atomic/CMakeLists.txt")
    add_subdirectory(tools/RP2040_Pseudo_Atomic)
else()
    message(FATAL_ERROR "RP2040_Pseudo_Atomic not found (third_party or tools)")
endif()

pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/pio/main.pio)

target_link_libraries(
    ${PROJECT_NAME} PRIVATE
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_vreg
    hardware_i2c
    SD-fatfs
    pico_bootrom
    pico_multicore
    pico_stdlib
)

target_include_directories(SD-fatfs BEFORE INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/source
)

pico_add_extra_outputs(${PROJECT_NAME})

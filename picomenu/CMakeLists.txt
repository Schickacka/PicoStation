cmake_minimum_required(VERSION 3.25)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/ps1-bare-metal/cmake/toolchain.cmake")

set(PROJECT_NAME "picostation-menu")

project(
    ${PROJECT_NAME}
    LANGUAGES    C CXX ASM
    VERSION      1.1.0
    DESCRIPTION  "PicoMenu Release 1.1 (RC5 parity + PT-BR + OWMC GameID + SIO0 Rumble Fix)"
)

include(ps1-bare-metal/cmake/setup.cmake)
include(ps1-bare-metal/cmake/virtualenv.cmake)

add_library(
    common OBJECT
    ps1-bare-metal/libc/clz.s
    ps1-bare-metal/libc/crt0.c
    ps1-bare-metal/libc/cxxsupport.cpp
    ps1-bare-metal/libc/malloc.c
    ps1-bare-metal/libc/misc.c
    ps1-bare-metal/libc/setjmp.s
    ps1-bare-metal/libc/string.c
    ps1-bare-metal/libc/string.s
    ps1-bare-metal/ps1/cache.s
    ps1-bare-metal/vendor/printf.c
    src/psxproject/system.s
    src/psxproject/unhandledexc.c
)
target_include_directories(
    common PUBLIC
    ps1-bare-metal/
    ps1-bare-metal/libc
)

add_executable(
    ${PROJECT_NAME}
    src/gpu.c
    src/main.c
    src/rc5_lang.c
    src/rc5_config.c
    src/rc5_ui.c
    src/rc5_cheats.c
    src/rc5_boot.c
    src/file_manager.c
    src/controller.c
    src/psxproject/cdrom.c
    src/psxproject/delay.c
    src/psxproject/filesystem.c
    src/psxproject/irq.c
    src/psxproject/system.c
    src/psxproject/stream.c
    src/psxproject/spu.c
)
target_link_libraries(${PROJECT_NAME} PRIVATE common)

# Embed RC5 8bpp Latin-1 13px font, 4bpp logo, cheat engine hook, and SPU VAG SFX
addBinaryFile(${PROJECT_NAME} rc5FontTexture     "${PROJECT_SOURCE_DIR}/assets/rc5/rc5_font_tex.dat")
addBinaryFile(${PROJECT_NAME} rc5FontPalette     "${PROJECT_SOURCE_DIR}/assets/rc5/rc5_font_pal.dat")
addBinaryFile(${PROJECT_NAME} rc5LogoTexture     "${PROJECT_SOURCE_DIR}/assets/rc5/rc5_logo_tex.dat")
addBinaryFile(${PROJECT_NAME} rc5LogoPalette     "${PROJECT_SOURCE_DIR}/assets/rc5/rc5_logo_pal.dat")
addBinaryFile(${PROJECT_NAME} rc5CheatEngineBlob "${PROJECT_SOURCE_DIR}/assets/rc5/rc5_cheat_engine.dat")
addBinaryFile(${PROJECT_NAME} click_sfx          "${PROJECT_SOURCE_DIR}/assets/click.vag")
addBinaryFile(${PROJECT_NAME} slide_sfx          "${PROJECT_SOURCE_DIR}/assets/slide.vag")

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    BYPRODUCTS ${PROJECT_NAME}.psexe
    COMMAND
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/ps1-bare-metal/tools/convertExecutable.py"
        "$<TARGET_FILE:${PROJECT_NAME}>"
        ${PROJECT_NAME}.psexe
    VERBATIM
)

if(WIN32)
    set(IMAGE_SCRIPT "${PROJECT_SOURCE_DIR}/make-xiso.bat")
else()
    set(IMAGE_SCRIPT "${PROJECT_SOURCE_DIR}/makeimage.sh")
endif()

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND "${IMAGE_SCRIPT}" "$<TARGET_FILE:${PROJECT_NAME}>"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    VERBATIM
)

set_property(
    TARGET ${PROJECT_NAME}
    APPEND PROPERTY ADDITIONAL_CLEAN_FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.psexe"
)

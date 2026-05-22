cmake_minimum_required(VERSION 3.21)
project(BuildLimitChanger LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CheckIPOSupported)
include(cmake/CPM.cmake)

set(BUILD_LIMIT_CHANGER_SOURCES
    src/main.c
    src/dimension_hook.c
    src/utils.c
    src/config/config_manager.c
    src/binary_parser/section_parser.c
    src/binary_parser/searcher.c
    src/decoder/x64_decoder.c
)

add_library(BuildLimitChanger SHARED ${BUILD_LIMIT_CHANGER_SOURCES})
target_include_directories(BuildLimitChanger PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(BuildLimitChanger PROPERTIES
    OUTPUT_NAME BuildLimitChanger
    C_VISIBILITY_PRESET hidden
)

if(MSVC)
    set_property(TARGET BuildLimitChanger PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    target_compile_options(BuildLimitChanger PRIVATE $<$<CONFIG:Release>:/O2 /Gy /Gw>)
    target_link_options(BuildLimitChanger PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)

    CPMAddPackage("gh:TsudaKageyu/minhook@1.3.4")
    target_link_libraries(BuildLimitChanger PRIVATE minhook psapi)
else()
    if(ANDROID)
        set(_BASE_FLAGS -Oz -fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-stack-protector -fno-ident)
        set(_LINK_FLAGS -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,--icf=all -Wl,--build-id=none -Wl,--pack-dyn-relocs=android -Wl,--hash-style=gnu -s)
    else()
        set(_BASE_FLAGS -Os -fomit-frame-pointer -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident)
        set(_LINK_FLAGS -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,--build-id=none -Wl,--hash-style=gnu -s)
    endif()

    target_compile_options(BuildLimitChanger PRIVATE
        $<$<CONFIG:Release>:${_BASE_FLAGS}>
        $<$<CONFIG:Release>:-ffunction-sections;-fdata-sections>
    )
    target_link_options(BuildLimitChanger PRIVATE $<$<CONFIG:Release>:${_LINK_FLAGS}>)

    CPMAddPackage(
        NAME gamepwnage
        GITHUB_REPOSITORY Kitsuri-Studios/gamepwnage
        GIT_TAG 91807190696041e0f41628c84f9bab77c5d0feeb
        DOWNLOAD_ONLY YES
    )
    add_library(gamepwnage STATIC
        ${gamepwnage_SOURCE_DIR}/src/hook86.c
        ${gamepwnage_SOURCE_DIR}/src/armhook.c
        ${gamepwnage_SOURCE_DIR}/src/inlinehook.c
        ${gamepwnage_SOURCE_DIR}/src/mem.c
        ${gamepwnage_SOURCE_DIR}/src/proc.c
        ${gamepwnage_SOURCE_DIR}/src/nop.c
    )
    target_include_directories(gamepwnage PUBLIC ${gamepwnage_SOURCE_DIR}/src)
    target_compile_options(gamepwnage PRIVATE $<$<CONFIG:Release>:-ffunction-sections;-fdata-sections;${_BASE_FLAGS}>)
    target_link_libraries(BuildLimitChanger PRIVATE gamepwnage)

    if(ANDROID)
        target_link_options(BuildLimitChanger PRIVATE
            $<$<CONFIG:Release>:-Wl,--version-script=${PROJECT_SOURCE_DIR}/map.txt;-Wl,--no-undefined-version>
        )
    endif()
endif()

check_ipo_supported(RESULT _IPO_OK OUTPUT _IPO_ERR)
if(_IPO_OK)
    set_property(TARGET BuildLimitChanger PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    if(TARGET gamepwnage)
        set_property(TARGET gamepwnage PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    endif()
endif()

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/compile_commands.json"
    )
endif()

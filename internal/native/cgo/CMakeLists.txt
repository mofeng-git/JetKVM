cmake_minimum_required(VERSION 3.14)
include(FetchContent)
include(ExternalProject)

project(jknative LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(USE_RK "Build with Rockchip media SDK (RV1106)" ON)
option(USE_LVGL "Build with LVGL UI (requires lvgl)" ON)
option(USE_LIBYUV "Use libyuv for accelerated format conversion" ON)

# Rockchip SDK paths (used only when USE_RK=ON)
set(RK_SDK_BASE "/opt/jetkvm-native-buildkit")
set(RK_MEDIA_OUTPUT "${RK_SDK_BASE}/media/out")
set(RK_MEDIA_INCLUDE_PATH "${RK_MEDIA_OUTPUT}/include")
set(RK_APP_MEDIA_LIBS_PATH "${RK_MEDIA_OUTPUT}/lib")

set(LV_USE_KCONFIG ON CACHE BOOL "" FORCE)
set(LV_BUILD_DEFCONFIG_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lvgl_defconfig CACHE PATH "" FORCE)

# # libgpiod

# ExternalProject_Add(libgpiod-project
#     URL https://mirrors.edge.kernel.org/pub/software/libs/libgpiod/libgpiod-2.2.tar.gz
#     URL_HASH SHA256=f89c2176250f1a9563265479eb8ad5f22a63f42db6a1f438effc570f0254d2f5
#     SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/libgpiod
#     BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/deps/libgpiod
#     CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env CPPFLAGS=-fPIC ${CMAKE_CURRENT_SOURCE_DIR}/deps/libgpiod/configure --enable-tools=no CC=${CMAKE_C_COMPILER} --host=${CMAKE_HOST_SYSTEM_PROCESSOR}
#     BUILD_COMMAND make && make install
#     BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/deps/libgpiod/lib/libgpiod.a
# )


if(USE_LVGL)
    # Fetch LVGL from GitHub
    FetchContent_Declare(
        lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG v9.3.0
        GIT_SHALLOW 1
        UPDATE_DISCONNECTED 1
        PATCH_COMMAND patch -p1 -f < ${CMAKE_CURRENT_SOURCE_DIR}/lvgl-minify.patch && cat ${CMAKE_CURRENT_SOURCE_DIR}/lvgl-minify.del | xargs rm -v
        )
    FetchContent_MakeAvailable(lvgl)
endif()

# Get source files, excluding CMake generated files
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "*.c" "ui/*.c")
list(FILTER sources EXCLUDE REGEX "CMakeFiles.*CompilerId.*\\.c$")

# Build UVC-oriented video shim always, and exclude rv1106-specific video.c
list(FILTER sources EXCLUDE REGEX "/video\\.c$")
list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/video_uvconly.c)

# 添加统一编码器框架（所有构建都需要）
list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/video_encoder.c)
list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/encoder_x264.c)

if(USE_RK)
    # Add MPP helpers for RK builds
    # Drop legacy RK_MPI_VENC-style wrapper to avoid old headers
    list(FILTER sources EXCLUDE REGEX "/enc_mpp\\.c$")
    # 注意：保留enc_mpp2.c暂时不删除，可能有其他用途
    # list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/enc_mpp2.c)
    list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/dec_mppjpeg.c)
    # 添加MPP编码器实现
    list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/encoder_mpp.c)
else()
    # UVC-only build: drop any MPP-specific sources from the glob
    list(FILTER sources EXCLUDE REGEX "/enc_mpp2?\\.c$")
    list(FILTER sources EXCLUDE REGEX "/dec_mppjpeg\\.c$")
    list(FILTER sources EXCLUDE REGEX "/enc_mpp\\.c$")
    list(FILTER sources EXCLUDE REGEX "/encoder_mpp\\.c$")
endif()

# When not using LVGL, drop UI and lvgl-dependent sources and use ctrl_stub
if(NOT USE_LVGL)
    list(FILTER sources EXCLUDE REGEX "/ui/.*")
    list(FILTER sources EXCLUDE REGEX "/screen\\.c$")
    list(FILTER sources EXCLUDE REGEX "/ctrl\\.c$")
    list(FILTER sources EXCLUDE REGEX "/edid\\.c$")
    list(FILTER sources EXCLUDE REGEX "/ui_index\\.c$")
    list(APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/ctrl_stub.c)
endif()

add_library(jknative STATIC ${sources} ${CMAKE_CURRENT_SOURCE_DIR}/ctrl.h)

# Include directories
target_include_directories(jknative PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/deps/libgpiod/include)
if(USE_LVGL)
    target_include_directories(jknative PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
endif()
if(USE_RK)
    target_include_directories(jknative PRIVATE
        ${RK_MEDIA_INCLUDE_PATH}
        ${RK_MEDIA_INCLUDE_PATH}/libdrm
        /usr/include/rockchip
    )
    target_compile_definitions(jknative PRIVATE 
        JETKVM_HAVE_MPP=1
    )
endif()

# libyuv for accelerated format conversion (required)
set(LIBYUV_FOUND FALSE)

# Try pkg-config first (rarely works for libyuv)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    if(CMAKE_CROSSCOMPILING)
        set(ENV{PKG_CONFIG_PATH} "/usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu/pkgconfig:/usr/share/pkgconfig")
    endif()
    pkg_check_modules(LIBYUV QUIET libyuv)
endif()

# Manual search (more reliable for libyuv)
if(NOT LIBYUV_FOUND)
    set(_LIBYUV_INCLUDE_PATHS "/usr/include")
    set(_LIBYUV_LIB_PATHS "/usr/lib")
    
    if(CMAKE_CROSSCOMPILING)
        list(APPEND _LIBYUV_INCLUDE_PATHS 
            "/usr/include/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu"
        )
        list(APPEND _LIBYUV_LIB_PATHS 
            "/usr/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-gnu"
        )
    endif()
    
    find_path(LIBYUV_INCLUDE_DIR 
        NAMES libyuv.h
        PATHS ${_LIBYUV_INCLUDE_PATHS}
        PATH_SUFFIXES libyuv
    )
    
    find_library(LIBYUV_LIBRARY 
        NAMES yuv libyuv
        PATHS ${_LIBYUV_LIB_PATHS}
    )
    
    if(LIBYUV_INCLUDE_DIR AND LIBYUV_LIBRARY)
        set(LIBYUV_FOUND TRUE)
        message(STATUS "libyuv found: ${LIBYUV_LIBRARY}")
        message(STATUS "  include: ${LIBYUV_INCLUDE_DIR}")
    endif()
endif()

# Apply settings (required)
if(LIBYUV_FOUND)
    if(LIBYUV_INCLUDE_DIRS)
        target_include_directories(jknative PRIVATE ${LIBYUV_INCLUDE_DIRS})
    elseif(LIBYUV_INCLUDE_DIR)
        target_include_directories(jknative PRIVATE ${LIBYUV_INCLUDE_DIR})
    endif()
    message(STATUS "libyuv: enabled (SIMD accelerated format conversion)")
else()
    if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR)
        message(FATAL_ERROR "libyuv not found! Install with: apt-get install libyuv-dev:${CMAKE_SYSTEM_PROCESSOR}")
    else()
        message(FATAL_ERROR "libyuv not found! Install with: apt-get install libyuv-dev")
    endif()
endif()
# Set library search path
if(USE_RK)
    target_link_directories(jknative PRIVATE ${RK_APP_MEDIA_LIBS_PATH})
endif()
# target_link_directories(jknative PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/deps/libgpiod/lib)

target_link_libraries(jknative PRIVATE pthread m)
# x264和turbojpeg对所有构建都需要（UVC后端）
target_link_libraries(jknative PRIVATE x264 turbojpeg)

# libyuv for format conversion acceleration (always linked)
if(LIBYUV_LIBRARIES)
    target_link_libraries(jknative PRIVATE ${LIBYUV_LIBRARIES})
    message(STATUS "Linking libyuv: ${LIBYUV_LIBRARIES}")
elseif(LIBYUV_LIBRARY)
    target_link_libraries(jknative PRIVATE ${LIBYUV_LIBRARY})
    message(STATUS "Linking libyuv: ${LIBYUV_LIBRARY}")
else()
    # Fallback to library name
    target_link_libraries(jknative PRIVATE yuv)
    message(STATUS "Linking libyuv: yuv (fallback)")
endif()

# RK专有库
if(USE_RK)
    # MPP and rockit libraries
    target_link_libraries(jknative PRIVATE rockchip_mpp rockit)
    message(STATUS "RK libraries: rockchip_mpp, rockit")
endif()
if(USE_LVGL)
    target_link_libraries(jknative PRIVATE lvgl::lvgl)
endif()

install(TARGETS jknative DESTINATION lib)

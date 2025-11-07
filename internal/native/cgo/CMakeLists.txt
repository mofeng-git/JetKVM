cmake_minimum_required(VERSION 3.14)
include(FetchContent)
include(ExternalProject)

project(jknative LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(USE_RK "Build with Rockchip media SDK (RV1106)" ON)
option(USE_LVGL "Build with LVGL UI (requires lvgl)" ON)

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
    target_compile_definitions(jknative PRIVATE JETKVM_HAVE_MPP=1)
endif()

# Set library search path
if(USE_RK)
    target_link_directories(jknative PRIVATE ${RK_APP_MEDIA_LIBS_PATH})
endif()
# target_link_directories(jknative PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/deps/libgpiod/lib)

target_link_libraries(jknative PRIVATE pthread m)
# x264和turbojpeg对所有构建都需要（UVC后端）
target_link_libraries(jknative PRIVATE x264 turbojpeg)
# RK专有库
if(USE_RK)
    # 注释：turbojpeg已在上面链接
endif()
if(USE_LVGL)
    target_link_libraries(jknative PRIVATE lvgl::lvgl)
endif()
if(USE_RK)
    target_link_libraries(jknative PRIVATE rockit rockchip_mpp rga)
endif()

install(TARGETS jknative DESTINATION lib)

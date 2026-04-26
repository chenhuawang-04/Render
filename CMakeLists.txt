cmake_minimum_required(VERSION 3.21)
project(VulkanRender_New LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VR_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(VR_TOP_LEVEL ON)
endif()

find_package(Vulkan REQUIRED)

set(MEMORYCENTER_ROOT "E:/Project/MelosyneTest/MemoryCenterNew" CACHE PATH "Path to MemoryCenterNew")
set(VECTOR_ROOT "E:/Project/MelosyneTest/Vector_New" CACHE PATH "Path to Vector_New")
set(SDL3_ROOT "E:/Project/MelosyneTest/FinalRender/vender/SDL-release-3.4.0" CACHE PATH "Path to SDL3 source root")

if(NOT EXISTS "${MEMORYCENTER_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "MEMORYCENTER_ROOT is invalid: ${MEMORYCENTER_ROOT}")
endif()
if(NOT EXISTS "${VECTOR_ROOT}/include/Center/Memory/Container/Vector/McVector.hpp")
    message(FATAL_ERROR "VECTOR_ROOT is invalid: ${VECTOR_ROOT}")
endif()
if(NOT EXISTS "${SDL3_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "SDL3_ROOT is invalid: ${SDL3_ROOT}")
endif()

add_subdirectory("${MEMORYCENTER_ROOT}" "${CMAKE_BINARY_DIR}/_deps/memorycenter" EXCLUDE_FROM_ALL)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL3_ROOT}" "${CMAKE_BINARY_DIR}/_deps/sdl3" EXCLUDE_FROM_ALL)

add_library(vulkan_init STATIC
    src/vulkan_context.cpp
    src/render/frame_command_host.cpp
    src/render/upload_host.cpp
    src/render/descriptor_host.cpp
    src/render/pipeline_host.cpp
    src/resource/buffer_host.cpp
    src/resource/image_host.cpp
    src/resource/gpu_memory_host.cpp
    src/resource/sampler_host.cpp
)

target_include_directories(vulkan_init
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${MEMORYCENTER_ROOT}/include
        ${VECTOR_ROOT}/include
)

target_link_libraries(vulkan_init
    PUBLIC
        Vulkan::Vulkan
        Center.Memory.Headers
)

add_library(vulkan_platform_sdl INTERFACE)
target_link_libraries(vulkan_platform_sdl
    INTERFACE
        vulkan_init
        SDL3::SDL3
)

add_executable(vulkan_init_demo
    examples/init_demo.cpp
)

target_link_libraries(vulkan_init_demo
    PRIVATE
        vulkan_init
)

add_executable(sdl_backend_demo
    examples/sdl_backend_demo.cpp
)

target_link_libraries(sdl_backend_demo
    PRIVATE
        vulkan_platform_sdl
)

add_executable(sdl_render_loop_demo
    examples/sdl_render_loop_demo.cpp
)

target_link_libraries(sdl_render_loop_demo
    PRIVATE
        vulkan_platform_sdl
)

add_executable(sdl_runtime_demo
    examples/sdl_runtime_demo.cpp
)

target_link_libraries(sdl_runtime_demo
    PRIVATE
        vulkan_platform_sdl
)

set(_vr_default_build_tests OFF)
if(VR_TOP_LEVEL)
    set(_vr_default_build_tests ON)
endif()
option(VR_BUILD_TESTS "Build VulkanRender_New test suite" ${_vr_default_build_tests})

option(VR_BUILD_BENCHMARKS "Build VulkanRender_New benchmark suite" OFF)
if(VR_BUILD_TESTS OR VR_BUILD_BENCHMARKS)
    enable_testing()
endif()

if(VR_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(VR_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

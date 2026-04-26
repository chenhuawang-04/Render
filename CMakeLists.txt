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
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(MEMORYCENTER_ROOT "E:/Project/MelosyneTest/MemoryCenterNew" CACHE PATH "Path to MemoryCenterNew")
set(VECTOR_ROOT "E:/Project/MelosyneTest/Vector_New" CACHE PATH "Path to Vector_New")
set(FAST_MATH_ROOT "E:/Project/MelosyneTest/Math/fast_math" CACHE PATH "Path to fast_math root")
set(SDL3_ROOT "E:/Project/MelosyneTest/FinalRender/vender/SDL-release-3.4.0" CACHE PATH "Path to SDL3 source root")
set(FREETYPE_ROOT "E:/Project/MelosyneTest/VulkanRender_New/freetype-master" CACHE PATH "Path to FreeType source root")

if(NOT EXISTS "${MEMORYCENTER_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "MEMORYCENTER_ROOT is invalid: ${MEMORYCENTER_ROOT}")
endif()
if(NOT EXISTS "${VECTOR_ROOT}/include/Center/Memory/Container/Vector/McVector.hpp")
    message(FATAL_ERROR "VECTOR_ROOT is invalid: ${VECTOR_ROOT}")
endif()
if(NOT EXISTS "${FAST_MATH_ROOT}/include/fast_math/types.h")
    message(FATAL_ERROR "FAST_MATH_ROOT is invalid: ${FAST_MATH_ROOT}")
endif()
if(NOT EXISTS "${SDL3_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "SDL3_ROOT is invalid: ${SDL3_ROOT}")
endif()
if(NOT EXISTS "${FREETYPE_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "FREETYPE_ROOT is invalid: ${FREETYPE_ROOT}")
endif()

find_program(GLSLANG_VALIDATOR_EXECUTABLE
    NAMES glslangValidator glslangValidator.exe
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin32")
if(NOT GLSLANG_VALIDATOR_EXECUTABLE)
    message(FATAL_ERROR "glslangValidator was not found. Please install Vulkan SDK and ensure Bin is in PATH.")
endif()

add_subdirectory("${MEMORYCENTER_ROOT}" "${CMAKE_BINARY_DIR}/_deps/memorycenter" EXCLUDE_FROM_ALL)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${SDL3_ROOT}" "${CMAKE_BINARY_DIR}/_deps/sdl3" EXCLUDE_FROM_ALL)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HVF ON CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS ON CACHE BOOL "" FORCE)
add_subdirectory("${FREETYPE_ROOT}" "${CMAKE_BINARY_DIR}/_deps/freetype" EXCLUDE_FROM_ALL)
if(TARGET freetype)
    # Clang + Ninja 环境下 rc.exe 常出现 Windows SDK include 路径缺失，
    # 先从构建源中移除 ftver.rc，避免影响 FreeType 主体静态库接入。
    get_property(_freetype_sources TARGET freetype PROPERTY SOURCES)
    list(REMOVE_ITEM _freetype_sources "src/base/ftver.rc")
    list(REMOVE_ITEM _freetype_sources "${FREETYPE_ROOT}/src/base/ftver.rc")
    set_property(TARGET freetype PROPERTY SOURCES "${_freetype_sources}")
endif()

set(VR_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
set(VR_GENERATED_SHADER_DIR "${VR_GENERATED_INCLUDE_DIR}/vr/text/generated")
set(VR_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

function(vr_compile_shader_to_header input_file stage symbol_name output_header)
    get_filename_component(_input_abs "${input_file}" ABSOLUTE)
    get_filename_component(_output_abs "${output_header}" ABSOLUTE)
    set(_spv_file "${CMAKE_BINARY_DIR}/generated/${symbol_name}.spv")

    add_custom_command(
        OUTPUT "${_spv_file}" "${_output_abs}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VR_GENERATED_SHADER_DIR}"
        COMMAND "${GLSLANG_VALIDATOR_EXECUTABLE}" -V --target-env vulkan1.3 -S "${stage}" -e main -o "${_spv_file}" "${_input_abs}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_header.py"
                --input "${_spv_file}"
                --output "${_output_abs}"
                --symbol "${symbol_name}"
                --namespace "vr::text::generated"
        DEPENDS "${_input_abs}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_header.py"
        COMMENT "Compiling shader ${_input_abs} -> ${_output_abs}"
        VERBATIM
    )
endfunction()

set(VR_TEXT_2D_VERT_HEADER "${VR_GENERATED_SHADER_DIR}/text_2d_vert_spv.hpp")
set(VR_TEXT_2D_FRAG_HEADER "${VR_GENERATED_SHADER_DIR}/text_2d_frag_spv.hpp")

vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/text_2d.vert"
                            "vert"
                            "k_text_2d_vert_spv"
                            "${VR_TEXT_2D_VERT_HEADER}")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/text_2d.frag"
                            "frag"
                            "k_text_2d_frag_spv"
                            "${VR_TEXT_2D_FRAG_HEADER}")

add_custom_target(vr_text_shaders
    DEPENDS
        "${VR_TEXT_2D_VERT_HEADER}"
        "${VR_TEXT_2D_FRAG_HEADER}"
)

add_library(vulkan_init STATIC
    src/vulkan_context.cpp
    src/render/frame_command_host.cpp
    src/render/upload_host.cpp
    src/render/descriptor_host.cpp
    src/render/pipeline_host.cpp
    src/text/freetype_host.cpp
    src/text/glyph_atlas_host.cpp
    src/text/glyph_upload_host.cpp
    src/text/text_renderer_2d.cpp
    src/resource/buffer_host.cpp
    src/resource/image_host.cpp
    src/resource/gpu_memory_host.cpp
    src/resource/sampler_host.cpp
)

add_dependencies(vulkan_init vr_text_shaders)

target_include_directories(vulkan_init
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${VR_GENERATED_INCLUDE_DIR}
        ${MEMORYCENTER_ROOT}/include
        ${VECTOR_ROOT}/include
        ${FAST_MATH_ROOT}/include
)

target_link_libraries(vulkan_init
    PUBLIC
        Vulkan::Vulkan
        Center.Memory.Headers
        freetype
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

add_executable(sdl_text_demo
    examples/sdl_text_demo.cpp
)

target_link_libraries(sdl_text_demo
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

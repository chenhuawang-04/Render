cmake_minimum_required(VERSION 3.21)
project(VulkanRender_New LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VR_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(VR_TOP_LEVEL ON)
endif()
option(VR_ENABLE_DESCRIPTOR_VALIDATION
    "Compile descriptor lifecycle validation layer (OFF means zero runtime overhead)"
    OFF)
option(VR_ENABLE_SHADER_ABI_CHECK
    "Validate shader set/binding contracts against reflection JSON (strict build-time check)"
    ON)
option(VR_ENABLE_SHADER_OPTIMIZATION
    "Run spirv-opt after glslang compilation for shader optimization"
    OFF)
set(VR_SHADER_OPTIMIZATION_LEVEL "-O" CACHE STRING "spirv-opt optimization level/flags (e.g. -O, -Os)")

find_package(Vulkan REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(MEMORYCENTER_ROOT "E:/Project/MelosyneTest/MemoryCenterNew" CACHE PATH "Path to MemoryCenterNew")
set(VECTOR_ROOT "E:/Project/MelosyneTest/Vector_New" CACHE PATH "Path to Vector_New")
set(FAST_MATH_ROOT "E:/Project/MelosyneTest/Math/fast_math" CACHE PATH "Path to fast_math root")
set(SDL3_ROOT "E:/Project/MelosyneTest/FinalRender/vender/SDL-release-3.4.0" CACHE PATH "Path to SDL3 source root")
set(FREETYPE_ROOT "E:/Project/MelosyneTest/VulkanRender_New/freetype-master" CACHE PATH "Path to FreeType source root")
set(CRASH_TRACER_ROOT "E:/Project/MelosyneTest/CrashTracer" CACHE PATH "Path to CrashTracer root")
set(MEMORYCENTER_MIMALLOC_INCLUDE_DIR "${MEMORYCENTER_ROOT}/third_party/mimalloc/include")

if(NOT EXISTS "${MEMORYCENTER_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "MEMORYCENTER_ROOT is invalid: ${MEMORYCENTER_ROOT}")
endif()
if(NOT EXISTS "${MEMORYCENTER_MIMALLOC_INCLUDE_DIR}/mimalloc.h")
    message(FATAL_ERROR "MemoryCenter mimalloc header is invalid: ${MEMORYCENTER_MIMALLOC_INCLUDE_DIR}/mimalloc.h")
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

find_program(SPIRV_CROSS_EXECUTABLE
    NAMES spirv-cross spirv-cross.exe
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin32")
find_program(SPIRV_OPT_EXECUTABLE
    NAMES spirv-opt spirv-opt.exe
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin32")
set(_vr_default_shader_reflection OFF)
if(SPIRV_CROSS_EXECUTABLE)
    set(_vr_default_shader_reflection ON)
endif()
option(VR_ENABLE_SHADER_REFLECTION
    "Generate shader reflection JSON artifacts via spirv-cross"
    ${_vr_default_shader_reflection})
if(VR_ENABLE_SHADER_REFLECTION AND NOT SPIRV_CROSS_EXECUTABLE)
    message(FATAL_ERROR "VR_ENABLE_SHADER_REFLECTION is ON but spirv-cross was not found. Please install Vulkan SDK or provide SPIRV_CROSS_EXECUTABLE.")
endif()
if(VR_ENABLE_SHADER_OPTIMIZATION AND NOT SPIRV_OPT_EXECUTABLE)
    message(FATAL_ERROR "VR_ENABLE_SHADER_OPTIMIZATION is ON but spirv-opt was not found. Please install SPIRV-Tools or Vulkan SDK.")
endif()
if(VR_ENABLE_SHADER_ABI_CHECK AND NOT VR_ENABLE_SHADER_REFLECTION)
    message(STATUS "VR_ENABLE_SHADER_ABI_CHECK requested but VR_ENABLE_SHADER_REFLECTION is OFF. ABI check disabled.")
    set(VR_ENABLE_SHADER_ABI_CHECK OFF)
endif()
message(STATUS "Shader reflection enabled: ${VR_ENABLE_SHADER_REFLECTION}")
message(STATUS "Shader ABI check enabled: ${VR_ENABLE_SHADER_ABI_CHECK}")
message(STATUS "Shader optimization enabled: ${VR_ENABLE_SHADER_OPTIMIZATION}")
if(VR_ENABLE_SHADER_REFLECTION)
    message(STATUS "spirv-cross: ${SPIRV_CROSS_EXECUTABLE}")
endif()
if(VR_ENABLE_SHADER_OPTIMIZATION)
    message(STATUS "spirv-opt: ${SPIRV_OPT_EXECUTABLE} (${VR_SHADER_OPTIMIZATION_LEVEL})")
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

set(_vr_default_enable_crash_tracer OFF)
if(VR_TOP_LEVEL)
    set(_vr_default_enable_crash_tracer ON)
endif()
option(VR_ENABLE_CRASH_TRACER
    "Enable CrashTracer for demos, tests, and benchmarks"
    ${_vr_default_enable_crash_tracer})
set(VR_CRASH_TRACER_ENABLED OFF)
if(VR_ENABLE_CRASH_TRACER)
    if(NOT EXISTS "${CRASH_TRACER_ROOT}/CMakeLists.txt")
        message(FATAL_ERROR "CRASH_TRACER_ROOT is invalid: ${CRASH_TRACER_ROOT}")
    endif()
    add_subdirectory("${CRASH_TRACER_ROOT}" "${CMAKE_BINARY_DIR}/_deps/crash_tracer" EXCLUDE_FROM_ALL)
    if(NOT TARGET Center.Debug.CrashTracer)
        message(FATAL_ERROR "CrashTracer target Center.Debug.CrashTracer was not found")
    endif()
    set(VR_CRASH_TRACER_ENABLED ON)
endif()

set(VR_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
set(VR_GENERATED_TEXT_SHADER_DIR "${VR_GENERATED_INCLUDE_DIR}/vr/text/generated")
set(VR_GENERATED_GEOMETRY_SHADER_DIR "${VR_GENERATED_INCLUDE_DIR}/vr/geometry/generated")
set(VR_GENERATED_SURFACE_SHADER_DIR "${VR_GENERATED_INCLUDE_DIR}/vr/surface/generated")
set(VR_GENERATED_PARTICLE_SHADER_DIR "${VR_GENERATED_INCLUDE_DIR}/vr/particle/generated")
set(VR_GENERATED_SHADOW_SHADER_DIR "${VR_GENERATED_INCLUDE_DIR}/vr/shadow/generated")
set(VR_GENERATED_RENDER_SHADER_DIR "${VR_GENERATED_INCLUDE_DIR}/vr/render/generated")
set(VR_GENERATED_SHADER_REFLECT_DIR "${VR_GENERATED_INCLUDE_DIR}/vr/shader_reflect")
set(VR_GENERATED_SHADER_CONTRACT_DIR "${VR_GENERATED_INCLUDE_DIR}/vr/shader_contract")
set(VR_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(VR_SHADER_INCLUDE_DIR "${VR_SHADER_SOURCE_DIR}/include")
file(GLOB_RECURSE VR_SHADER_INCLUDE_FILES
    CONFIGURE_DEPENDS
    "${VR_SHADER_INCLUDE_DIR}/*.glsl")
set(VR_SHADER_REFLECTION_OUTPUTS "")
set(VR_SHADER_CONTRACT_OUTPUTS "")

function(vr_compile_shader_to_header input_file stage symbol_name output_header output_namespace)
    get_filename_component(_input_abs "${input_file}" ABSOLUTE)
    get_filename_component(_output_abs "${output_header}" ABSOLUTE)
    set(_spv_raw_file "${CMAKE_BINARY_DIR}/generated/${symbol_name}.raw.spv")
    set(_spv_file "${CMAKE_BINARY_DIR}/generated/${symbol_name}.spv")
    set(_reflect_file "${CMAKE_BINARY_DIR}/generated/${symbol_name}.reflect.json")
    set(_contract_file "${CMAKE_BINARY_DIR}/generated/${symbol_name}.contract.json")
    set(_outputs "${_spv_file}" "${_output_abs}")
    set(_tool_dependencies
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_header.py"
        ${VR_SHADER_INCLUDE_FILES})
    set(_post_spv_commands "")
    set(_spv_finalize_commands "")

    if(VR_ENABLE_SHADER_REFLECTION)
        list(APPEND _outputs "${_reflect_file}")
        list(APPEND _tool_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_reflect_to_json.py")
        list(APPEND _post_spv_commands
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_reflect_to_json.py"
                --input "${_spv_file}"
                --output "${_reflect_file}"
                --spirv-cross "${SPIRV_CROSS_EXECUTABLE}")

        list(APPEND VR_SHADER_REFLECTION_OUTPUTS "${_reflect_file}")
        set(VR_SHADER_REFLECTION_OUTPUTS "${VR_SHADER_REFLECTION_OUTPUTS}" PARENT_SCOPE)

        if(VR_ENABLE_SHADER_ABI_CHECK)
            list(APPEND _outputs "${_contract_file}")
            list(APPEND _tool_dependencies "${CMAKE_CURRENT_SOURCE_DIR}/tools/shader_contract_check.py")
            list(APPEND _post_spv_commands
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/shader_contract_check.py"
                    --source "${_input_abs}"
                    --reflect "${_reflect_file}"
                    --output "${_contract_file}"
                    --strict)

            list(APPEND VR_SHADER_CONTRACT_OUTPUTS "${_contract_file}")
            set(VR_SHADER_CONTRACT_OUTPUTS "${VR_SHADER_CONTRACT_OUTPUTS}" PARENT_SCOPE)
        endif()
    endif()

    if(VR_ENABLE_SHADER_OPTIMIZATION)
        list(APPEND _spv_finalize_commands
            COMMAND "${SPIRV_OPT_EXECUTABLE}" "${VR_SHADER_OPTIMIZATION_LEVEL}" "${_spv_raw_file}" -o "${_spv_file}")
    else()
        list(APPEND _spv_finalize_commands
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_spv_raw_file}" "${_spv_file}")
    endif()

    add_custom_command(
        OUTPUT ${_outputs}
        BYPRODUCTS "${_spv_raw_file}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/generated"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VR_GENERATED_TEXT_SHADER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VR_GENERATED_GEOMETRY_SHADER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VR_GENERATED_SURFACE_SHADER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VR_GENERATED_PARTICLE_SHADER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VR_GENERATED_SHADOW_SHADER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VR_GENERATED_RENDER_SHADER_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VR_GENERATED_SHADER_REFLECT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VR_GENERATED_SHADER_CONTRACT_DIR}"
        COMMAND "${GLSLANG_VALIDATOR_EXECUTABLE}" -V --target-env vulkan1.3 -S "${stage}" -e main
                "-I${VR_SHADER_SOURCE_DIR}"
                "-I${VR_SHADER_INCLUDE_DIR}"
                -o "${_spv_raw_file}" "${_input_abs}"
        ${_spv_finalize_commands}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_header.py"
                --input "${_spv_file}"
                --output "${_output_abs}"
                --symbol "${symbol_name}"
                --namespace "${output_namespace}"
        ${_post_spv_commands}
        DEPENDS "${_input_abs}" ${_tool_dependencies}
        COMMENT "Compiling shader ${_input_abs} -> ${_output_abs}"
        VERBATIM
    )
endfunction()

set(VR_TEXT_2D_VERT_HEADER "${VR_GENERATED_TEXT_SHADER_DIR}/text_2d_vert_spv.hpp")
set(VR_TEXT_2D_FRAG_HEADER "${VR_GENERATED_TEXT_SHADER_DIR}/text_2d_frag_spv.hpp")
set(VR_TEXT_3D_VERT_HEADER "${VR_GENERATED_TEXT_SHADER_DIR}/text_3d_vert_spv.hpp")
set(VR_TEXT_3D_FRAG_HEADER "${VR_GENERATED_TEXT_SHADER_DIR}/text_3d_frag_spv.hpp")
set(VR_GEOMETRY_2D_VERT_HEADER "${VR_GENERATED_GEOMETRY_SHADER_DIR}/geometry_2d_vert_spv.hpp")
set(VR_GEOMETRY_2D_FRAG_HEADER "${VR_GENERATED_GEOMETRY_SHADER_DIR}/geometry_2d_frag_spv.hpp")
set(VR_GEOMETRY_3D_VERT_HEADER "${VR_GENERATED_GEOMETRY_SHADER_DIR}/geometry_3d_vert_spv.hpp")
set(VR_GEOMETRY_3D_FRAG_HEADER "${VR_GENERATED_GEOMETRY_SHADER_DIR}/geometry_3d_frag_spv.hpp")
set(VR_SURFACE_2D_VERT_HEADER "${VR_GENERATED_SURFACE_SHADER_DIR}/surface_2d_vert_spv.hpp")
set(VR_SURFACE_2D_FRAG_HEADER "${VR_GENERATED_SURFACE_SHADER_DIR}/surface_2d_frag_spv.hpp")
set(VR_SURFACE_3D_VERT_HEADER "${VR_GENERATED_SURFACE_SHADER_DIR}/surface_3d_vert_spv.hpp")
set(VR_SURFACE_3D_FRAG_HEADER "${VR_GENERATED_SURFACE_SHADER_DIR}/surface_3d_frag_spv.hpp")
set(VR_PARTICLE_2D_VERT_HEADER "${VR_GENERATED_PARTICLE_SHADER_DIR}/particle_2d_vert_spv.hpp")
set(VR_PARTICLE_2D_FRAG_HEADER "${VR_GENERATED_PARTICLE_SHADER_DIR}/particle_2d_frag_spv.hpp")
set(VR_PARTICLE_3D_VERT_HEADER "${VR_GENERATED_PARTICLE_SHADER_DIR}/particle_3d_vert_spv.hpp")
set(VR_PARTICLE_3D_FRAG_HEADER "${VR_GENERATED_PARTICLE_SHADER_DIR}/particle_3d_frag_spv.hpp")
set(VR_PARTICLE_UPDATE_2D_COMP_HEADER "${VR_GENERATED_PARTICLE_SHADER_DIR}/particle_update_2d_comp_spv.hpp")
set(VR_PARTICLE_UPDATE_3D_COMP_HEADER "${VR_GENERATED_PARTICLE_SHADER_DIR}/particle_update_3d_comp_spv.hpp")
set(VR_PARTICLE_BUILD_2D_COMP_HEADER "${VR_GENERATED_PARTICLE_SHADER_DIR}/particle_build_2d_comp_spv.hpp")
set(VR_PARTICLE_BUILD_3D_COMP_HEADER "${VR_GENERATED_PARTICLE_SHADER_DIR}/particle_build_3d_comp_spv.hpp")
set(VR_PARTICLE_SORT_3D_COMP_HEADER "${VR_GENERATED_PARTICLE_SHADER_DIR}/particle_sort_3d_comp_spv.hpp")
set(VR_SHADOW_2D_VERT_HEADER "${VR_GENERATED_SHADOW_SHADER_DIR}/shadow_depth_2d_vert_spv.hpp")
set(VR_SHADOW_3D_VERT_HEADER "${VR_GENERATED_SHADOW_SHADER_DIR}/shadow_depth_3d_vert_spv.hpp")
set(VR_RENDER_TARGET_COMPOSITE_VERT_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/render_target_composite_vert_spv.hpp")
set(VR_RENDER_TARGET_COMPOSITE_FAR_VERT_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/render_target_composite_far_vert_spv.hpp")
set(VR_RENDER_TARGET_COMPOSITE_FRAG_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/render_target_composite_frag_spv.hpp")
set(VR_BACKGROUND_2D_FRAG_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/background_2d_frag_spv.hpp")
set(VR_SKY_ENVIRONMENT_FRAG_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/sky_environment_frag_spv.hpp")
set(VR_SKY_ENVIRONMENT_IMAGE_FRAG_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/sky_environment_image_frag_spv.hpp")
set(VR_SKY_ENVIRONMENT_EQUIRECT_FRAG_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/sky_environment_equirect_frag_spv.hpp")
set(VR_SKY_ENVIRONMENT_ATMOSPHERE_FRAG_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/sky_environment_atmosphere_frag_spv.hpp")
set(VR_RENDER_TARGET_BLOOM_PREFILTER_FRAG_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/render_target_bloom_prefilter_frag_spv.hpp")
set(VR_RENDER_TARGET_BLOOM_BLUR_FRAG_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/render_target_bloom_blur_frag_spv.hpp")
set(VR_RENDER_TARGET_BLOOM_COMBINE_FRAG_HEADER "${VR_GENERATED_RENDER_SHADER_DIR}/render_target_bloom_combine_frag_spv.hpp")

vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/text_2d.vert"
                            "vert"
                            "k_text_2d_vert_spv"
                            "${VR_TEXT_2D_VERT_HEADER}"
                            "vr::text::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/text_2d.frag"
                            "frag"
                            "k_text_2d_frag_spv"
                            "${VR_TEXT_2D_FRAG_HEADER}"
                            "vr::text::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/text_3d.vert"
                            "vert"
                            "k_text_3d_vert_spv"
                            "${VR_TEXT_3D_VERT_HEADER}"
                            "vr::text::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/text_3d.frag"
                            "frag"
                            "k_text_3d_frag_spv"
                            "${VR_TEXT_3D_FRAG_HEADER}"
                            "vr::text::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/geometry_3d.vert"
                            "vert"
                            "k_geometry_3d_vert_spv"
                            "${VR_GEOMETRY_3D_VERT_HEADER}"
                            "vr::geometry::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/geometry_3d.frag"
                            "frag"
                            "k_geometry_3d_frag_spv"
                            "${VR_GEOMETRY_3D_FRAG_HEADER}"
                            "vr::geometry::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/geometry_2d.vert"
                            "vert"
                            "k_geometry_2d_vert_spv"
                            "${VR_GEOMETRY_2D_VERT_HEADER}"
                            "vr::geometry::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/geometry_2d.frag"
                            "frag"
                            "k_geometry_2d_frag_spv"
                            "${VR_GEOMETRY_2D_FRAG_HEADER}"
                            "vr::geometry::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/surface_2d.vert"
                            "vert"
                            "k_surface_2d_vert_spv"
                            "${VR_SURFACE_2D_VERT_HEADER}"
                            "vr::surface::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/surface_2d.frag"
                            "frag"
                            "k_surface_2d_frag_spv"
                            "${VR_SURFACE_2D_FRAG_HEADER}"
                            "vr::surface::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/surface_3d.vert"
                            "vert"
                            "k_surface_3d_vert_spv"
                            "${VR_SURFACE_3D_VERT_HEADER}"
                            "vr::surface::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/surface_3d.frag"
                            "frag"
                            "k_surface_3d_frag_spv"
                            "${VR_SURFACE_3D_FRAG_HEADER}"
                            "vr::surface::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/particle_2d.vert"
                            "vert"
                            "k_particle_2d_vert_spv"
                            "${VR_PARTICLE_2D_VERT_HEADER}"
                            "vr::particle::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/particle_2d.frag"
                            "frag"
                            "k_particle_2d_frag_spv"
                            "${VR_PARTICLE_2D_FRAG_HEADER}"
                            "vr::particle::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/particle_3d.vert"
                            "vert"
                            "k_particle_3d_vert_spv"
                            "${VR_PARTICLE_3D_VERT_HEADER}"
                            "vr::particle::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/particle_3d.frag"
                            "frag"
                            "k_particle_3d_frag_spv"
                            "${VR_PARTICLE_3D_FRAG_HEADER}"
                            "vr::particle::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/particle_update_2d.comp"
                            "comp"
                            "k_particle_update_2d_comp_spv"
                            "${VR_PARTICLE_UPDATE_2D_COMP_HEADER}"
                            "vr::particle::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/particle_update_3d.comp"
                            "comp"
                            "k_particle_update_3d_comp_spv"
                            "${VR_PARTICLE_UPDATE_3D_COMP_HEADER}"
                            "vr::particle::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/particle_build_2d.comp"
                            "comp"
                            "k_particle_build_2d_comp_spv"
                            "${VR_PARTICLE_BUILD_2D_COMP_HEADER}"
                            "vr::particle::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/particle_build_3d.comp"
                            "comp"
                            "k_particle_build_3d_comp_spv"
                            "${VR_PARTICLE_BUILD_3D_COMP_HEADER}"
                            "vr::particle::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/particle_sort_3d.comp"
                            "comp"
                            "k_particle_sort_3d_comp_spv"
                            "${VR_PARTICLE_SORT_3D_COMP_HEADER}"
                            "vr::particle::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/shadow_depth_3d.vert"
                            "vert"
                            "k_shadow_depth_3d_vert_spv"
                            "${VR_SHADOW_3D_VERT_HEADER}"
                            "vr::shadow::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/shadow_depth_2d.vert"
                            "vert"
                            "k_shadow_depth_2d_vert_spv"
                            "${VR_SHADOW_2D_VERT_HEADER}"
                            "vr::shadow::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/render_target_composite.vert"
                            "vert"
                            "k_render_target_composite_vert_spv"
                            "${VR_RENDER_TARGET_COMPOSITE_VERT_HEADER}"
                            "vr::render::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/render_target_composite_far.vert"
                            "vert"
                            "k_render_target_composite_far_vert_spv"
                            "${VR_RENDER_TARGET_COMPOSITE_FAR_VERT_HEADER}"
                            "vr::render::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/render_target_composite.frag"
                            "frag"
                            "k_render_target_composite_frag_spv"
                            "${VR_RENDER_TARGET_COMPOSITE_FRAG_HEADER}"
                            "vr::render::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/background_2d.frag"
                            "frag"
                            "k_background_2d_frag_spv"
                            "${VR_BACKGROUND_2D_FRAG_HEADER}"
                            "vr::render::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/sky_environment.frag"
                            "frag"
                            "k_sky_environment_frag_spv"
                            "${VR_SKY_ENVIRONMENT_FRAG_HEADER}"
                            "vr::render::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/sky_environment_image.frag"
                            "frag"
                            "k_sky_environment_image_frag_spv"
                            "${VR_SKY_ENVIRONMENT_IMAGE_FRAG_HEADER}"
                            "vr::render::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/sky_environment_equirect.frag"
                            "frag"
                            "k_sky_environment_equirect_frag_spv"
                            "${VR_SKY_ENVIRONMENT_EQUIRECT_FRAG_HEADER}"
                            "vr::render::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/sky_environment_atmosphere.frag"
                            "frag"
                            "k_sky_environment_atmosphere_frag_spv"
                            "${VR_SKY_ENVIRONMENT_ATMOSPHERE_FRAG_HEADER}"
                            "vr::render::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/render_target_bloom_prefilter.frag"
                            "frag"
                            "k_render_target_bloom_prefilter_frag_spv"
                            "${VR_RENDER_TARGET_BLOOM_PREFILTER_FRAG_HEADER}"
                            "vr::render::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/render_target_bloom_blur.frag"
                            "frag"
                            "k_render_target_bloom_blur_frag_spv"
                            "${VR_RENDER_TARGET_BLOOM_BLUR_FRAG_HEADER}"
                            "vr::render::generated")
vr_compile_shader_to_header("${VR_SHADER_SOURCE_DIR}/render_target_bloom_combine.frag"
                            "frag"
                            "k_render_target_bloom_combine_frag_spv"
                            "${VR_RENDER_TARGET_BLOOM_COMBINE_FRAG_HEADER}"
                            "vr::render::generated")

add_custom_target(vr_generated_shaders
    DEPENDS
        "${VR_TEXT_2D_VERT_HEADER}"
        "${VR_TEXT_2D_FRAG_HEADER}"
        "${VR_TEXT_3D_VERT_HEADER}"
        "${VR_TEXT_3D_FRAG_HEADER}"
        "${VR_GEOMETRY_2D_VERT_HEADER}"
        "${VR_GEOMETRY_2D_FRAG_HEADER}"
        "${VR_GEOMETRY_3D_VERT_HEADER}"
        "${VR_GEOMETRY_3D_FRAG_HEADER}"
        "${VR_SURFACE_2D_VERT_HEADER}"
        "${VR_SURFACE_2D_FRAG_HEADER}"
        "${VR_SURFACE_3D_VERT_HEADER}"
        "${VR_SURFACE_3D_FRAG_HEADER}"
        "${VR_PARTICLE_2D_VERT_HEADER}"
        "${VR_PARTICLE_2D_FRAG_HEADER}"
        "${VR_PARTICLE_3D_VERT_HEADER}"
        "${VR_PARTICLE_3D_FRAG_HEADER}"
        "${VR_PARTICLE_UPDATE_2D_COMP_HEADER}"
        "${VR_PARTICLE_UPDATE_3D_COMP_HEADER}"
        "${VR_PARTICLE_BUILD_2D_COMP_HEADER}"
        "${VR_PARTICLE_BUILD_3D_COMP_HEADER}"
        "${VR_SHADOW_2D_VERT_HEADER}"
        "${VR_SHADOW_3D_VERT_HEADER}"
        "${VR_RENDER_TARGET_COMPOSITE_VERT_HEADER}"
        "${VR_RENDER_TARGET_COMPOSITE_FAR_VERT_HEADER}"
        "${VR_RENDER_TARGET_COMPOSITE_FRAG_HEADER}"
        "${VR_BACKGROUND_2D_FRAG_HEADER}"
        "${VR_SKY_ENVIRONMENT_FRAG_HEADER}"
        "${VR_SKY_ENVIRONMENT_IMAGE_FRAG_HEADER}"
        "${VR_SKY_ENVIRONMENT_EQUIRECT_FRAG_HEADER}"
        "${VR_SKY_ENVIRONMENT_ATMOSPHERE_FRAG_HEADER}"
        "${VR_RENDER_TARGET_BLOOM_PREFILTER_FRAG_HEADER}"
        "${VR_RENDER_TARGET_BLOOM_BLUR_FRAG_HEADER}"
        "${VR_RENDER_TARGET_BLOOM_COMBINE_FRAG_HEADER}"
        ${VR_SHADER_REFLECTION_OUTPUTS}
        ${VR_SHADER_CONTRACT_OUTPUTS}
)

if(VR_ENABLE_SHADER_ABI_CHECK)
    set(VR_SHADER_CONTRACT_SUMMARY_FILE "${CMAKE_BINARY_DIR}/generated/shader_contract_summary.json")
    add_custom_command(
        OUTPUT "${VR_SHADER_CONTRACT_SUMMARY_FILE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/shader_contract_summary.py"
                --input ${VR_SHADER_CONTRACT_OUTPUTS}
                --output "${VR_SHADER_CONTRACT_SUMMARY_FILE}"
                --strict
        DEPENDS
            ${VR_SHADER_CONTRACT_OUTPUTS}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/shader_contract_summary.py"
        COMMENT "Aggregating shader ABI contract summary"
        VERBATIM
    )
    add_custom_target(vr_shader_contract_summary
        DEPENDS "${VR_SHADER_CONTRACT_SUMMARY_FILE}")
    add_dependencies(vr_generated_shaders vr_shader_contract_summary)
endif()

add_library(vulkan_init STATIC
    src/vulkan_context.cpp
    src/animation/animation_clip_host.cpp
    src/animation/animation_frame_sequence_host.cpp
    src/animation/animation_morph_host.cpp
    src/animation/animation_path_host.cpp
    src/animation/animation_skeletal_host.cpp
    src/animation/animation_vertex_deform_host.cpp
    src/asset/texture_host.cpp
    src/render_graph/barrier_plan.cpp
    src/render_graph/render_graph_builder.cpp
    src/render_graph/render_graph_executor.cpp
    src/render_graph/vulkan_barrier_plan.cpp
    src/render_graph/vulkan_resource_table.cpp
    src/render/frame_command_host.cpp
    src/render/frame_composer_host.cpp
    src/render/ibl_bake_host.cpp
    src/render/ibl_host.cpp
    src/render/sky_environment_gpu_host.cpp
    src/render/sky_environment_pass.cpp
    src/render/upload_host.cpp
    src/render/descriptor_host.cpp
    src/render/bindless_resource_system.cpp
    src/render/pipeline_host.cpp
    src/render/render_target_host.cpp
    src/render/render_target_pass.cpp
    src/render/render_target_pool.cpp
    src/render/swapchain_target_set.cpp
    src/render/background_pass_2d.cpp
    src/render/render_target_composite_renderer.cpp
    src/render/render_target_bloom_renderer.cpp
    src/render/scene_recorder_2d.cpp
    src/render/scene_recorder_3d.cpp
    src/render/scene_bloom_post_stack.cpp
    src/render/scene_render_target_set.cpp
    src/text/freetype_host.cpp
    src/text/glyph_atlas_host.cpp
    src/text/glyph_upload_host.cpp
    src/text/text_renderer_2d.cpp
    src/text/text_renderer_3d.cpp
    src/geometry/geometry_resource_host.cpp
    src/geometry/geometry_appearance_host.cpp
    src/geometry/geometry_image_host.cpp
    src/geometry/geometry_upload_host.cpp
    src/geometry/geometry_renderer_2d.cpp
    src/geometry/geometry_renderer_3d.cpp
    src/particle/particle_simulation_host.cpp
    src/particle/particle_upload_host.cpp
    src/particle/particle_renderer_2d.cpp
    src/particle/particle_renderer_3d.cpp
    src/surface/surface_image_host.cpp
    src/surface/surface_upload_host.cpp
    src/surface/surface_renderer_2d.cpp
    src/surface/surface_renderer_3d.cpp
    src/light/light_shadow_upload_host.cpp
    src/shadow/shadow_atlas_host.cpp
    src/shadow/shadow_renderer_2d.cpp
    src/shadow/shadow_renderer_3d.cpp
    src/resource/buffer_host.cpp
    src/resource/image_host.cpp
    src/resource/gpu_memory_host.cpp
    src/resource/sampler_host.cpp
)

add_dependencies(vulkan_init vr_generated_shaders)

target_include_directories(vulkan_init
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${VR_GENERATED_INCLUDE_DIR}
        ${MEMORYCENTER_ROOT}/include
        ${MEMORYCENTER_MIMALLOC_INCLUDE_DIR}
        ${VECTOR_ROOT}/include
        ${FAST_MATH_ROOT}/include
)
target_compile_definitions(vulkan_init
    PUBLIC
        VR_ENABLE_DESCRIPTOR_VALIDATION=$<IF:$<BOOL:${VR_ENABLE_DESCRIPTOR_VALIDATION}>,1,0>
)

target_link_libraries(vulkan_init
    PUBLIC
        Vulkan::Vulkan
        Center.Memory.Headers
        freetype
)

add_library(vr_crash_support STATIC
    src/runtime/crash_tracer_support.cpp
)

target_include_directories(vr_crash_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(vr_crash_support
    PUBLIC
        cxx_std_23
)

if(VR_CRASH_TRACER_ENABLED)
    target_link_libraries(vr_crash_support
        PRIVATE
            Center.Debug.CrashTracer
    )
    target_compile_definitions(vr_crash_support
        PRIVATE
            VR_HAS_CRASH_TRACER=1
    )
endif()

add_library(vulkan_platform_sdl INTERFACE)
target_link_libraries(vulkan_platform_sdl
    INTERFACE
        vulkan_init
        vr_crash_support
        SDL3::SDL3
)

add_executable(vulkan_init_demo
    examples/init_demo.cpp
)

target_link_libraries(vulkan_init_demo
    PRIVATE
        vulkan_init
        vr_crash_support
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

add_executable(sdl_geometry_unified_demo
    examples/sdl_geometry_unified_demo.cpp
)

target_link_libraries(sdl_geometry_unified_demo
    PRIVATE
        vulkan_platform_sdl
)

add_executable(sdl_surface_unified_demo
    examples/sdl_surface_unified_demo.cpp
)

target_link_libraries(sdl_surface_unified_demo
    PRIVATE
        vulkan_platform_sdl
)

add_executable(sdl_surface_light_shadow_2d_demo
    examples/sdl_surface_light_shadow_2d_demo.cpp
)

target_link_libraries(sdl_surface_light_shadow_2d_demo
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

add_executable(sdl_text_3d_demo
    examples/sdl_text_3d_demo.cpp
)

target_link_libraries(sdl_text_3d_demo
    PRIVATE
        vulkan_platform_sdl
)

add_executable(sdl_particle_2d_demo
    examples/sdl_particle_2d_demo.cpp
)

target_link_libraries(sdl_particle_2d_demo
    PRIVATE
        vulkan_platform_sdl
)

add_executable(sdl_particle_3d_demo
    examples/sdl_particle_3d_demo.cpp
)

target_link_libraries(sdl_particle_3d_demo
    PRIVATE
        vulkan_platform_sdl
)

add_executable(sdl_scene_3d_unified_demo
    examples/sdl_scene_3d_unified_demo.cpp
)

target_link_libraries(sdl_scene_3d_unified_demo
    PRIVATE
        vulkan_platform_sdl
)

add_executable(sdl_pbr_material_grid_demo
    examples/sdl_pbr_material_grid_demo.cpp
)

target_link_libraries(sdl_pbr_material_grid_demo
    PRIVATE
        vulkan_platform_sdl
)

add_executable(sdl_offscreen_postprocess_demo
    examples/sdl_offscreen_postprocess_demo.cpp
)

target_link_libraries(sdl_offscreen_postprocess_demo
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

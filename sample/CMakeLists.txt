# SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
# SPDX-License-Identifier: MIT

# Needs Vulkan headers with VK_KHR_shader_abort (Vulkan SDK 1.4.357.0 or newer). The shaders are already compiled
# (shaders/*.spv), so no shader compiler is needed.
cmake_minimum_required(VERSION 3.16)
project(ShaderAbortSamples C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

set(SHADER_ABORT_TOOLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(sample
    sample.c
    ${SHADER_ABORT_TOOLS_DIR}/shader_abort_message.c)
target_include_directories(sample PRIVATE ${SHADER_ABORT_TOOLS_DIR})
target_link_libraries(sample PRIVATE Vulkan::Vulkan)

# Standalone tools
add_executable(sample_strip strip.c ${SHADER_ABORT_TOOLS_DIR}/shader_abort_strip.c)
add_executable(sample_reflect reflect.c ${SHADER_ABORT_TOOLS_DIR}/shader_abort_reflect.c)
set_target_properties(sample_strip PROPERTIES OUTPUT_NAME strip)
set_target_properties(sample_reflect PROPERTIES OUTPUT_NAME reflect)
target_include_directories(sample_strip PRIVATE ${SHADER_ABORT_TOOLS_DIR})
target_include_directories(sample_reflect PRIVATE ${SHADER_ABORT_TOOLS_DIR})

# So the samples find the .spv files from any working directory (--spv-dir overrides it)
target_compile_definitions(sample PRIVATE SAMPLES_SPV_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")

if (MSVC)
    target_compile_options(sample PRIVATE /W4)
    target_compile_options(sample_strip PRIVATE /W4)
    target_compile_options(sample_reflect PRIVATE /W4)
    target_compile_definitions(sample PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(sample_strip PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(sample_reflect PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(sample PRIVATE -Wall -Wextra)
    target_compile_options(sample_strip PRIVATE -Wall -Wextra)
    target_compile_options(sample_reflect PRIVATE -Wall -Wextra)
endif()

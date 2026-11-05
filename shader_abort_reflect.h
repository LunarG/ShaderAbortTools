// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ShaderAbortReflectKind {
    SHADER_ABORT_REFLECT_KIND_SCALAR = 0,
    SHADER_ABORT_REFLECT_KIND_VECTOR = 1,
    SHADER_ABORT_REFLECT_KIND_ARRAY = 2,  // the format string is an array of 8- or 32-bit integers
    SHADER_ABORT_REFLECT_KIND_OTHER = 3,  // a type that can't be in a payload (bool, pointer ...), size is 0
    SHADER_ABORT_REFLECT_KIND_MATRIX = 4,
    SHADER_ABORT_REFLECT_KIND_STRUCT = 5,  // a nested struct, walk type_id for its members
} ShaderAbortReflectKind;

typedef enum ShaderAbortReflectScalarType {
    SHADER_ABORT_REFLECT_SCALAR_NONE = 0,
    SHADER_ABORT_REFLECT_SCALAR_INT = 1,
    SHADER_ABORT_REFLECT_SCALAR_UINT = 2,
    SHADER_ABORT_REFLECT_SCALAR_FLOAT = 3,
} ShaderAbortReflectScalarType;

typedef struct ShaderAbortReflectMember {
    uint32_t offset;   // byte offset in the payload
    uint32_t size;     // size in bytes, including any padding inside it (ArrayStride, MatrixStride). 0 for KIND_OTHER
    uint32_t type_id;  // the SPIR-V <id> of the type, to dig into a KIND_STRUCT
    ShaderAbortReflectKind kind;
    // The scalar, or the components of the vector or matrix, or the elements of the array. NONE for an array of
    // vectors, matrices or structs (then element_type_id is set)
    ShaderAbortReflectScalarType scalar_type;
    uint32_t bit_width;        // of that scalar: 8, 16, 32 or 64
    uint32_t count;            // 1 for a scalar, components of a vector, columns of a matrix, length of an array
    uint32_t array_stride;     // KIND_ARRAY: bytes between elements
    uint32_t element_type_id;  // KIND_ARRAY: the <id> of the element type when it isn't a scalar, else 0
    uint32_t rows;             // KIND_MATRIX: components in each column
    uint32_t matrix_stride;    // KIND_MATRIX: bytes between columns (or rows, if row_major)
    bool row_major;            // KIND_MATRIX
} ShaderAbortReflectMember;

typedef struct ShaderAbortReflectAbort {
    uint32_t instruction_offset;  // offset in 32-bit words of the OpAbortKHR from the start of the module
    uint32_t message_type_id;     // OpAbortKHR's Message Type <id>
    uint32_t message_id;          // OpAbortKHR's Message <id>
    // The format string (null-terminated): the first member, if it is a constant integer array holding text.
    // NULL if there isn't one, e.g. the message is data with no string
    const char* pFormat;
    uint32_t payload_size;  // bytes the payload of this abort is, 0 if any member's size isn't known
    uint32_t member_count;
    const ShaderAbortReflectMember* pMembers;  // if the Message Type isn't a struct, there is a single member
} ShaderAbortReflectAbort;

typedef struct ShaderAbortReflection {
    uint32_t abort_count;
    const ShaderAbortReflectAbort* pAborts;  // in the order they are in the module
} ShaderAbortReflection;

/**
 * @param pCode The SPIR-V (same as VkShaderModuleCreateInfo::pCode)
 * @param codeSize Size of the SPIR-V in bytes (same as VkShaderModuleCreateInfo::codeSize)
 * @return The reflection (abort_count is 0 if the module has no aborts), or NULL if out of memory.
 *         Free it with shader_abort_reflect_destroy(). It does not point into pCode.
 */
ShaderAbortReflection* shader_abort_reflect_create(const uint32_t* pCode, size_t codeSize);

void shader_abort_reflect_destroy(ShaderAbortReflection* pReflection);

/**
 * Reference printer: will print out all the aborts in the shader
 *
 * @return 0, or non-zero if out of memory
 */
int shader_abort_reflect_print(const uint32_t* pCode, size_t codeSize);

#ifdef __cplusplus
}
#endif

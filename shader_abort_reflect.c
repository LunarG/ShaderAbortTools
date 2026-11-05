// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

#include "shader_abort_reflect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// From spirv.h
enum {
    kOpDecorate = 71,
    kOpMemberDecorate = 72,
    kOpTypeInt = 21,
    kOpTypeFloat = 22,
    kOpTypeVector = 23,
    kOpTypeMatrix = 24,
    kOpTypeArray = 28,
    kOpTypeStruct = 30,
    kOpConstant = 43,
    kOpConstantComposite = 44,
    kOpConstantNull = 46,
    kOpSpecConstant = 50,
    kOpSpecConstantComposite = 51,
    kOpCompositeConstruct = 80,
    kOpCopyObject = 83,
    kOpCopyLogical = 400,
    kOpAbortKHR = 5121,
    kOpConstantDataKHR = 5147,
    kOpSpecConstantDataKHR = 5148,
    kDecorationRowMajor = 4,
    kDecorationArrayStride = 6,
    kDecorationMatrixStride = 7,
    kDecorationOffset = 35,
    kDecorationUTFEncodedKHR = 5145,
};

// Deeper than any real Message Type, it only stops a malformed module from recursing forever
enum { kMaxTypeDepth = 16 };

typedef struct Module {
    const uint32_t* pWords;
    size_t word_count;
    uint32_t* pIdToOffset;  // word offset of the instruction that defines an <id>, 0 if it isn't one this file needs
} Module;

static uint32_t opcode_of(const uint32_t* pInst) { return pInst[0] & 0xFFFF; }
static uint32_t length_of(const uint32_t* pInst) { return pInst[0] >> 16; }

// Where the Result <id> is, for the instructions this file needs to look up. 0 for everything else.
static uint32_t result_id_operand(uint32_t opcode) {
    switch (opcode) {
        case kOpTypeInt:
        case kOpTypeFloat:
        case kOpTypeVector:
        case kOpTypeMatrix:
        case kOpTypeArray:
        case kOpTypeStruct:
            return 1;
        case kOpConstant:
        case kOpConstantComposite:
        case kOpConstantNull:
        case kOpSpecConstant:
        case kOpSpecConstantComposite:
        case kOpCompositeConstruct:
        case kOpCopyObject:
        case kOpCopyLogical:
        case kOpConstantDataKHR:
        case kOpSpecConstantDataKHR:
            return 2;
        default:
            return 0;
    }
}

static const uint32_t* find_definition(const Module* pModule, uint32_t id) {
    const uint32_t offset = pModule->pIdToOffset[id];
    return offset != 0 ? pModule->pWords + offset : NULL;
}

// The first literal of an OpMemberDecorate, or |fallback| if the member doesn't have that decoration.
// RowMajor has no literal, check it with has_member_decoration().
static uint32_t find_member_decoration(const Module* pModule, uint32_t struct_id, uint32_t member, uint32_t decoration,
                                       uint32_t fallback) {
    for (size_t i = 5; i < pModule->word_count; i += length_of(pModule->pWords + i)) {
        const uint32_t* pInst = pModule->pWords + i;
        if (opcode_of(pInst) == kOpMemberDecorate && pInst[1] == struct_id && pInst[2] == member &&
            pInst[3] == decoration) {
            return length_of(pInst) > 4 ? pInst[4] : 1;
        }
    }
    return fallback;
}

static bool has_member_decoration(const Module* pModule, uint32_t struct_id, uint32_t member, uint32_t decoration) {
    return find_member_decoration(pModule, struct_id, member, decoration, 0) != 0;
}

// The first literal of an OpDecorate, or |fallback|. For a decoration with no literal, returns 1 if it is there.
static uint32_t find_decoration(const Module* pModule, uint32_t id, uint32_t decoration, uint32_t fallback) {
    for (size_t i = 5; i < pModule->word_count; i += length_of(pModule->pWords + i)) {
        const uint32_t* pInst = pModule->pWords + i;
        if (opcode_of(pInst) == kOpDecorate && pInst[1] == id && pInst[2] == decoration) {
            return length_of(pInst) > 3 ? pInst[3] : 1;
        }
    }
    return fallback;
}

// The length of an OpTypeArray. It is an OpConstant or OpSpecConstant, for which the default value is used.
static uint32_t array_length(const Module* pModule, const uint32_t* pArray) {
    const uint32_t* pLength = find_definition(pModule, pArray[3]);
    return pLength != NULL ? pLength[3] : 0;
}

// Size in bytes of a type in the payload, 0 if it can't be worked out. Matrix layout (MatrixStride, RowMajor) is a
// decoration on the struct member that holds the matrix, or the array of matrices, so that member is passed along.
static uint32_t type_size(const Module* pModule, uint32_t type_id, uint32_t parent_struct, uint32_t member,
                          uint32_t depth) {
    const uint32_t* pInst = find_definition(pModule, type_id);
    if (pInst == NULL || depth > kMaxTypeDepth) {
        return 0;
    }
    switch (opcode_of(pInst)) {
        case kOpTypeInt:
        case kOpTypeFloat:
            return pInst[2] / 8;
        case kOpTypeVector:
            return pInst[3] * type_size(pModule, pInst[2], 0, 0, depth + 1);  // scalar layout, no padding
        case kOpTypeMatrix: {
            // The columns are vectors. A row-major matrix is stored as rows instead, MatrixStride apart.
            const uint32_t* pColumn = find_definition(pModule, pInst[2]);
            if (pColumn == NULL || opcode_of(pColumn) != kOpTypeVector) {
                return 0;
            }
            const uint32_t columns = pInst[3];
            const uint32_t rows = pColumn[3];
            const uint32_t component = type_size(pModule, pColumn[2], 0, 0, depth + 1);
            const bool row_major = has_member_decoration(pModule, parent_struct, member, kDecorationRowMajor);
            const uint32_t vectors = row_major ? rows : columns;
            const uint32_t tight = (row_major ? columns : rows) * component;
            const uint32_t stride =
                find_member_decoration(pModule, parent_struct, member, kDecorationMatrixStride, tight);
            return vectors * stride;
        }
        case kOpTypeArray: {
            const uint32_t element = type_size(pModule, pInst[2], parent_struct, member, depth + 1);
            return array_length(pModule, pInst) * find_decoration(pModule, type_id, kDecorationArrayStride, element);
        }
        case kOpTypeStruct: {
            // Ends where its last member ends, the members are not always in order of their Offset
            uint32_t size = 0;
            for (uint32_t i = 0; i + 2 < length_of(pInst); ++i) {
                const uint32_t member_size = type_size(pModule, pInst[2 + i], type_id, i, depth + 1);
                const uint32_t* pMemberType = find_definition(pModule, pInst[2 + i]);
                if (member_size == 0 && (pMemberType == NULL || opcode_of(pMemberType) != kOpTypeStruct)) {
                    return 0;  // a member that can't be sized (an empty struct is fine, it is 0 bytes)
                }
                const uint32_t end = find_member_decoration(pModule, type_id, i, kDecorationOffset, 0) + member_size;
                size = end > size ? end : size;
            }
            return size;
        }
        default:
            return 0;
    }
}

static bool describe_scalar(const Module* pModule, uint32_t type_id, ShaderAbortReflectMember* pMember) {
    const uint32_t* pInst = find_definition(pModule, type_id);
    if (pInst == NULL) {
        return false;
    }
    if (opcode_of(pInst) == kOpTypeInt) {
        pMember->scalar_type = pInst[3] ? SHADER_ABORT_REFLECT_SCALAR_INT : SHADER_ABORT_REFLECT_SCALAR_UINT;
    } else if (opcode_of(pInst) == kOpTypeFloat) {
        pMember->scalar_type = SHADER_ABORT_REFLECT_SCALAR_FLOAT;
    } else {
        return false;
    }
    pMember->bit_width = pInst[2];
    return true;
}

// |parent_struct| and |member| are where the type is used, for the matrix decorations. 0, 0 if it isn't in a struct.
static void describe_member(const Module* pModule, uint32_t type_id, uint32_t parent_struct, uint32_t member,
                            ShaderAbortReflectMember* pMember) {
    memset(pMember, 0, sizeof(*pMember));
    pMember->type_id = type_id;
    pMember->kind = SHADER_ABORT_REFLECT_KIND_OTHER;
    pMember->count = 1;
    pMember->size = type_size(pModule, type_id, parent_struct, member, 0);

    if (describe_scalar(pModule, type_id, pMember)) {
        pMember->kind = SHADER_ABORT_REFLECT_KIND_SCALAR;
        return;
    }

    const uint32_t* pInst = find_definition(pModule, type_id);
    if (pInst == NULL) {
        pMember->size = 0;
        return;
    }
    switch (opcode_of(pInst)) {
        case kOpTypeVector:
            if (describe_scalar(pModule, pInst[2], pMember)) {
                pMember->kind = SHADER_ABORT_REFLECT_KIND_VECTOR;
                pMember->count = pInst[3];
            }
            break;
        case kOpTypeMatrix: {
            const uint32_t* pColumn = find_definition(pModule, pInst[2]);
            if (pColumn != NULL && describe_scalar(pModule, pColumn[2], pMember)) {
                pMember->kind = SHADER_ABORT_REFLECT_KIND_MATRIX;
                pMember->count = pInst[3];
                pMember->rows = pColumn[3];
                pMember->row_major = has_member_decoration(pModule, parent_struct, member, kDecorationRowMajor);
                const uint32_t vectors = pMember->row_major ? pMember->rows : pMember->count;
                pMember->matrix_stride = vectors != 0 ? pMember->size / vectors : 0;
            }
            break;
        }
        case kOpTypeArray:
            pMember->kind = SHADER_ABORT_REFLECT_KIND_ARRAY;
            pMember->count = array_length(pModule, pInst);
            pMember->array_stride = pMember->count != 0 ? pMember->size / pMember->count : 0;
            if (!describe_scalar(pModule, pInst[2], pMember)) {
                pMember->element_type_id = pInst[2];  // an array of vectors, matrices or structs
            }
            break;
        case kOpTypeStruct:
            pMember->kind = SHADER_ABORT_REFLECT_KIND_STRUCT;
            break;
        default:
            pMember->size = 0;
            break;
    }
}

// Writes the bytes of constant |id| (an array of integers, or one integer of it) to pBytes, up to |capacity|.
// Returns how many bytes it wrote.
static size_t read_constant_bytes(const Module* pModule, uint32_t id, uint8_t* pBytes, size_t capacity) {
    const uint32_t* pInst = find_definition(pModule, id);
    if (pInst == NULL) {
        return 0;
    }
    ShaderAbortReflectMember type;
    describe_member(pModule, pInst[1], 0, 0, &type);
    const uint32_t opcode = opcode_of(pInst);

    size_t count = 0;
    if (opcode == kOpConstantDataKHR || opcode == kOpSpecConstantDataKHR || opcode == kOpConstant) {
        // The literal is the value's bytes packed into words, lowest byte first
        for (; count < type.size && count < capacity; ++count) {
            pBytes[count] = (uint8_t)(pInst[3 + count / 4] >> (8 * (count % 4)));
        }
    } else if (opcode == kOpConstantNull) {
        for (; count < type.size && count < capacity; ++count) {
            pBytes[count] = 0;
        }
    } else if (opcode == kOpConstantComposite || opcode == kOpSpecConstantComposite) {
        for (uint32_t i = 3; i < length_of(pInst) && count < capacity; ++i) {
            count += read_constant_bytes(pModule, pInst[i], pBytes + count, capacity - count);
        }
    }
    return count;
}

// Is |pText| (|size| bytes) a null-terminated string? Used when the array isn't decorated UTFEncodedKHR (Slang doesn't
// decorate it), so that an array of numbers in the first member isn't mistaken for the format string.
static bool looks_like_string(const char* pText, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        const unsigned char c = (unsigned char)pText[i];
        if (c == '\0') {
            return true;
        }
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            return false;  // a control character, this is data, not text. Bytes >= 0x80 are allowed for UTF-8
        }
    }
    return false;  // no null terminator
}

// Follows OpAbortKHR's Message back to the constant with the format string, and copies the string out
static char* find_format_string(const Module* pModule, uint32_t message_id) {
    const uint32_t* pInst = find_definition(pModule, message_id);
    while (pInst != NULL && (opcode_of(pInst) == kOpCopyObject || opcode_of(pInst) == kOpCopyLogical)) {
        pInst = find_definition(pModule, pInst[3]);
    }
    if (pInst == NULL) {
        return NULL;
    }

    // When the message is a struct, the string is its first member
    uint32_t string_id = pInst[2];
    const uint32_t opcode = opcode_of(pInst);
    if (opcode == kOpCompositeConstruct || opcode == kOpConstantComposite || opcode == kOpSpecConstantComposite) {
        const uint32_t* pType = find_definition(pModule, pInst[1]);
        if (pType != NULL && opcode_of(pType) == kOpTypeStruct) {
            string_id = pInst[3];
        }
    }

    // It has to be a constant array of integers
    const uint32_t* pString = find_definition(pModule, string_id);
    if (pString == NULL) {
        return NULL;
    }
    ShaderAbortReflectMember type;
    describe_member(pModule, pString[1], 0, 0, &type);
    if (type.kind != SHADER_ABORT_REFLECT_KIND_ARRAY ||
        (type.scalar_type != SHADER_ABORT_REFLECT_SCALAR_INT && type.scalar_type != SHADER_ABORT_REFLECT_SCALAR_UINT)) {
        return NULL;
    }

    char* pText = (char*)calloc(type.size + 1, 1);  // the + 1 guarantees a null terminator
    if (pText == NULL) {
        return NULL;
    }
    const size_t length = read_constant_bytes(pModule, string_id, (uint8_t*)pText, type.size);
    const bool utf_encoded = find_decoration(pModule, pString[1], kDecorationUTFEncodedKHR, 0) != 0;
    if (length == 0 || (!utf_encoded && !looks_like_string(pText, length))) {
        free(pText);
        return NULL;
    }
    return pText;
}

static bool reflect_abort(const Module* pModule, size_t offset, ShaderAbortReflectAbort* pAbort) {
    const uint32_t* pInst = pModule->pWords + offset;
    pAbort->instruction_offset = (uint32_t)offset;
    pAbort->message_type_id = pInst[1];
    pAbort->message_id = pInst[2];

    const uint32_t* pType = find_definition(pModule, pAbort->message_type_id);
    const bool is_struct = pType != NULL && opcode_of(pType) == kOpTypeStruct;
    const uint32_t member_count = is_struct ? length_of(pType) - 2 : 1;

    ShaderAbortReflectMember* pMembers =
        (ShaderAbortReflectMember*)calloc(member_count ? member_count : 1, sizeof(ShaderAbortReflectMember));
    if (pMembers == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < member_count; ++i) {
        if (is_struct) {
            describe_member(pModule, pType[2 + i], pAbort->message_type_id, i, &pMembers[i]);
            pMembers[i].offset = find_member_decoration(pModule, pAbort->message_type_id, i, kDecorationOffset, 0);
        } else {
            describe_member(pModule, pAbort->message_type_id, 0, 0, &pMembers[i]);
        }
    }

    pAbort->member_count = member_count;
    pAbort->pMembers = pMembers;
    pAbort->payload_size = type_size(pModule, pAbort->message_type_id, 0, 0, 0);
    pAbort->pFormat = find_format_string(pModule, pAbort->message_id);
    return true;
}

ShaderAbortReflection* shader_abort_reflect_create(const uint32_t* pCode, size_t codeSize) {
    const uint32_t bound = pCode[3];
    Module module = {pCode, codeSize / 4, (uint32_t*)calloc(bound, sizeof(uint32_t))};
    ShaderAbortReflection* pReflection = (ShaderAbortReflection*)calloc(1, sizeof(ShaderAbortReflection));
    if (module.pIdToOffset == NULL || pReflection == NULL) {
        free(module.pIdToOffset);
        free(pReflection);
        return NULL;
    }

    // Pass 1: remember where the <id>s this file needs are defined, and count the aborts
    uint32_t abort_count = 0;
    for (size_t i = 5; i < module.word_count; i += length_of(pCode + i)) {
        const uint32_t result_operand = result_id_operand(opcode_of(pCode + i));
        if (result_operand != 0) {
            module.pIdToOffset[pCode[i + result_operand]] = (uint32_t)i;
        }
        if (opcode_of(pCode + i) == kOpAbortKHR) {
            abort_count++;
        }
    }

    // Pass 2: reflect each abort
    ShaderAbortReflectAbort* pAborts = NULL;
    if (abort_count > 0) {
        pAborts = (ShaderAbortReflectAbort*)calloc(abort_count, sizeof(ShaderAbortReflectAbort));
        pReflection->pAborts = pAborts;
    }
    bool ok = abort_count == 0 || pAborts != NULL;
    for (size_t i = 5; ok && i < module.word_count; i += length_of(pCode + i)) {
        if (opcode_of(pCode + i) == kOpAbortKHR) {
            ok = reflect_abort(&module, i, &pAborts[pReflection->abort_count]);
            pReflection->abort_count += ok ? 1 : 0;
        }
    }

    free(module.pIdToOffset);
    if (!ok) {
        shader_abort_reflect_destroy(pReflection);
        return NULL;
    }
    return pReflection;
}

void shader_abort_reflect_destroy(ShaderAbortReflection* pReflection) {
    if (pReflection == NULL) {
        return;
    }
    for (uint32_t i = 0; i < pReflection->abort_count; ++i) {
        free((void*)pReflection->pAborts[i].pFormat);
        free((void*)pReflection->pAborts[i].pMembers);
    }
    free((void*)pReflection->pAborts);
    free(pReflection);
}

static void type_name(const ShaderAbortReflectMember* pMember, char* pOut, size_t outSize) {
    static const char* kScalarNames[] = {"?", "int", "uint", "float"};
    const char* scalar = kScalarNames[pMember->scalar_type];
    switch (pMember->kind) {
        case SHADER_ABORT_REFLECT_KIND_SCALAR:
            snprintf(pOut, outSize, "%s%u", scalar, pMember->bit_width);
            break;
        case SHADER_ABORT_REFLECT_KIND_VECTOR:
            snprintf(pOut, outSize, "%s%u vector of %u", scalar, pMember->bit_width, pMember->count);
            break;
        case SHADER_ABORT_REFLECT_KIND_MATRIX:
            snprintf(pOut, outSize, "%s%u matrix %ux%u%s stride %u", scalar, pMember->bit_width, pMember->count,
                     pMember->rows, pMember->row_major ? " row-major" : "", pMember->matrix_stride);
            break;
        case SHADER_ABORT_REFLECT_KIND_ARRAY:
            if (pMember->element_type_id != 0) {
                snprintf(pOut, outSize, "(type %%%u)[%u] stride %u", pMember->element_type_id, pMember->count,
                         pMember->array_stride);
            } else if (pMember->array_stride != pMember->bit_width / 8) {
                snprintf(pOut, outSize, "%s%u[%u] stride %u", scalar, pMember->bit_width, pMember->count,
                         pMember->array_stride);
            } else {
                snprintf(pOut, outSize, "%s%u[%u]", scalar, pMember->bit_width, pMember->count);
            }
            break;
        case SHADER_ABORT_REFLECT_KIND_STRUCT:
            snprintf(pOut, outSize, "struct (type %%%u)", pMember->type_id);
            break;
        default:
            snprintf(pOut, outSize, "(type %%%u)", pMember->type_id);
            break;
    }
}

int shader_abort_reflect_print(const uint32_t* pCode, size_t codeSize) {
    ShaderAbortReflection* pReflection = shader_abort_reflect_create(pCode, codeSize);
    if (pReflection == NULL) {
        return 1;
    }
    printf("%u OpAbortKHR in this shader\n", pReflection->abort_count);
    for (uint32_t a = 0; a < pReflection->abort_count; ++a) {
        const ShaderAbortReflectAbort* pAbort = &pReflection->pAborts[a];
        printf("\nabort %u: \"%s\"\n", a, pAbort->pFormat ? pAbort->pFormat : "(no format string)");
        printf("  payload is %u bytes\n", pAbort->payload_size);
        for (uint32_t m = 0; m < pAbort->member_count; ++m) {
            const ShaderAbortReflectMember* pMember = &pAbort->pMembers[m];
            char type[64];
            type_name(pMember, type, sizeof(type));
            if (m == 0 && pAbort->pFormat) {
                printf("  offset %3u  size %3u  %-24s the format string\n", pMember->offset, pMember->size, type);
            } else {
                printf("  offset %3u  size %3u  %s\n", pMember->offset, pMember->size, type);
            }
        }
    }
    shader_abort_reflect_destroy(pReflection);
    return 0;
}

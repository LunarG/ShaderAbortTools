// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

#include "shader_abort_strip.h"
#include <string.h>

// Returns the word offset (from the start of the instruction) of the Decoration operand for every
// instruction that decorates something, or 0 if |opcode| is not a decorate instruction.
static size_t decoration_operand_offset(uint32_t opcode) {
    // From spirv.h
    const uint32_t kOpDecorate = 71;
    const uint32_t kOpMemberDecorate = 72;
    const uint32_t kOpDecorateId = 332;
    const uint32_t kOpDecorateString = 5632;        // OpDecorateStringGOOGLE
    const uint32_t kOpMemberDecorateString = 5633;  // OpMemberDecorateStringGOOGLE

    if (opcode == kOpDecorate || opcode == kOpDecorateId || opcode == kOpDecorateString) {
        return 2;  // OpDecorate <target> <decoration> ...
    }
    if (opcode == kOpMemberDecorate || opcode == kOpMemberDecorateString) {
        return 3;  // OpMemberDecorate <struct type> <member> <decoration> ...
    }
    return 0;
}

// Returns true if |id| is the Result <id> of an OpSpecConstantDataKHR. Those get turned into plain
// OpConstantNull, so a SpecId decoration pointing at them would no longer be legal.
static bool is_spec_constant_data_id(const uint32_t* spirv, size_t word_count, uint32_t id) {
    const uint32_t kOpSpecConstantDataKHR = 5148;

    size_t offset = 5;  // skip the SPIR-V header
    while (offset < word_count) {
        uint32_t opcode = spirv[offset] & 0xFFFF;
        uint32_t length = spirv[offset] >> 16;
        if (length == 0 || length > word_count - offset) {
            return false;
        }
        if (opcode == kOpSpecConstantDataKHR && length >= 3 && spirv[offset + 2] == id) {
            return true;
        }
        offset += length;
    }
    return false;
}

bool has_shader_abort(const uint32_t* pCode, size_t codeSize) {
    // From spirv.h
    const uint32_t kOpCapability = 17;
    const uint32_t kCapabilityAbortKHR = 5120;

    // Only loop through the capabilities at the very start of the file
    size_t word_count = codeSize / 4;
    size_t in_offset = 5;
    while (in_offset < word_count) {
        uint32_t instruction = pCode[in_offset];
        uint32_t opcode = instruction & 0xFFFF;
        uint32_t length = instruction >> 16;

        if (opcode == kOpCapability) {
            uint32_t capability = pCode[in_offset + 1];
            if (capability == kCapabilityAbortKHR) {
                return true;
            }
        } else {
            // Capabilities are always first, nothing can be before
            break;
        }
        in_offset += length;
    }

    return false;
}

int strip_abort_extensions(const uint32_t* pCode, size_t codeSize, uint32_t* out_pCode, size_t* out_codeSize) {
    // From spirv.h
    const uint32_t kOpCapability = 17;
    const uint32_t kOpExtension = 10;
    const uint32_t kOpConstantNull = 46;
    const uint32_t kOpUnreachable = 255;
    const uint32_t kCapabilityAbortKHR = 5120;
    const uint32_t kCapabilityConstantDataKHR = 5146;
    const uint32_t kOpAbortKHR = 5121;
    const uint32_t kOpDecorate = 71;
    const uint32_t kDecorationSpecId = 1;
    const uint32_t kDecorationUTFEncodedKHR = 5145;
    const uint32_t kOpConstantDataKHR = 5147;
    const uint32_t kOpSpecConstantDataKHR = 5148;

    // classic "pass in as bytes" but need as dword count
    const size_t word_count = codeSize / 4;

    // Copy SPIR-V header
    for (size_t i = 0; i < 5; ++i) {
        out_pCode[i] = pCode[i];
    }

    size_t in_offset = 5;
    size_t out_offset = 5;

    while (in_offset < word_count) {
        uint32_t instruction = pCode[in_offset];
        uint32_t opcode = instruction & 0xFFFF;
        uint32_t length = instruction >> 16;

        if (length == 0) {
            return -1;
        }

        bool remove_inst = false;
        bool make_null = false;
        bool make_unreachable = false;
        const size_t decoration_offset = decoration_operand_offset(opcode);

        // now strip the Capabilities, Extensions, Decorations and the Opcodes
        if (opcode == kOpCapability) {
            uint32_t capability = pCode[in_offset + 1];
            if (capability == kCapabilityAbortKHR || capability == kCapabilityConstantDataKHR) {
                remove_inst = true;
            }
        } else if (opcode == kOpExtension) {
            const char* extension_name = (const char*)(&pCode[in_offset + 1]);
            if (strcmp(extension_name, "SPV_KHR_abort") == 0 || strcmp(extension_name, "SPV_KHR_constant_data") == 0) {
                remove_inst = true;
            }
        } else if (decoration_offset != 0 && length > decoration_offset) {
            uint32_t decoration = pCode[in_offset + decoration_offset];
            if (decoration == kDecorationUTFEncodedKHR) {
                remove_inst = true;
            } else if (opcode == kOpDecorate && decoration == kDecorationSpecId &&
                       is_spec_constant_data_id(pCode, word_count, pCode[in_offset + 1])) {
                // OpSpecConstantDataKHR becomes OpConstantNull below, and SpecId is only legal on spec constants
                remove_inst = true;
            }
        } else if (opcode == kOpAbortKHR) {
            make_unreachable = true;
        } else if (opcode == kOpConstantDataKHR || opcode == kOpSpecConstantDataKHR) {
            make_null = true;
        }

        if (remove_inst) {
            // to remove, simply just skip over it
        } else if (make_unreachable) {
            // Replace the termination instruction with OpUnreachable so the basic block remains valid
            out_pCode[out_offset] = (1 << 16) | kOpUnreachable;
            out_offset += 1;
        } else if (make_null) {
            // For something like
            // %a = OpConstantDataKHR %type 012341234 11111111
            // %b = OpCompositeConstruct %abortMessage %a
            //
            // we just replace it with
            // %a = OpConstantNull %type
            out_pCode[out_offset] = (3 << 16) | kOpConstantNull;
            out_pCode[out_offset + 1] = pCode[in_offset + 1];  // Keep original Result Type <id>
            out_pCode[out_offset + 2] = pCode[in_offset + 2];  // Keep original Result <id>
            out_offset += 3;
        } else {
            // normal case, copy the whole instruction
            for (size_t i = 0; i < length; ++i) {
                out_pCode[out_offset + i] = pCode[in_offset + i];
            }
            out_offset += length;
        }

        in_offset += length;
    }

    *out_codeSize = out_offset * 4;
    return 0;
}

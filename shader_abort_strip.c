// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

#include "shader_abort_strip.h"
#include <stdio.h>
#include <string.h>

// Returns the word offset (from the start of the instruction) of the Decoration operand for every
// instruction that decorates something, or 0 if |opcode| is not a decorate instruction.
static size_t decoration_operand_offset(uint32_t opcode) {
    // From spirv.h
    const uint32_t _OpDecorate = 71;
    const uint32_t _OpMemberDecorate = 72;
    const uint32_t _OpDecorateId = 332;
    const uint32_t _OpDecorateString = 5632;        // OpDecorateStringGOOGLE
    const uint32_t _OpMemberDecorateString = 5633;  // OpMemberDecorateStringGOOGLE

    if (opcode == _OpDecorate || opcode == _OpDecorateId || opcode == _OpDecorateString) {
        return 2;  // OpDecorate <target> <decoration> ...
    }
    if (opcode == _OpMemberDecorate || opcode == _OpMemberDecorateString) {
        return 3;  // OpMemberDecorate <struct type> <member> <decoration> ...
    }
    return 0;
}

// Returns true if |id| is the Result <id> of an OpSpecConstantDataKHR. Those get turned into plain
// OpConstantNull, so a SpecId decoration pointing at them would no longer be legal.
static bool is_spec_constant_data_id(const uint32_t* spirv, size_t word_count, uint32_t id) {
    const uint32_t _OpSpecConstantDataKHR = 5148;

    size_t offset = 5;  // skip the SPIR-V header
    while (offset < word_count) {
        uint32_t opcode = spirv[offset] & 0xFFFF;
        uint32_t length = spirv[offset] >> 16;
        if (length == 0 || length > word_count - offset) {
            return false;
        }
        if (opcode == _OpSpecConstantDataKHR && length >= 3 && spirv[offset + 2] == id) {
            return true;
        }
        offset += length;
    }
    return false;
}

bool has_shader_abort(const uint32_t* pCode, size_t codeSize) {
    // From spirv.h
    const uint32_t _OpCapability = 17;
    const uint32_t _CapabilityAbortKHR = 5120;

    // Only loop through the capabilities at the very start of the file
    size_t word_count = codeSize / 4;
    size_t in_offset = 5;
    while (in_offset < word_count) {
        uint32_t instruction = pCode[in_offset];
        uint32_t opcode = instruction & 0xFFFF;
        uint32_t length = instruction >> 16;

        if (opcode == _OpCapability) {
            uint32_t capability = pCode[in_offset + 1];
            if (capability == _CapabilityAbortKHR) {
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

int strip_abort_extensions(const uint32_t* spirv_in, size_t code_size_in, uint32_t* spirv_out, size_t* code_size_out) {
    // From spirv.h
    const uint32_t _OpCapability = 17;
    const uint32_t _OpExtension = 10;
    const uint32_t _OpConstantNull = 46;
    const uint32_t _OpUnreachable = 255;
    const uint32_t _CapabilityAbortKHR = 5120;
    const uint32_t _CapabilityConstantDataKHR = 5146;
    const uint32_t _OpAbortKHR = 5121;
    const uint32_t _OpDecorate = 71;
    const uint32_t _DecorationSpecId = 1;
    const uint32_t _DecorationUTFEncodedKHR = 5145;
    const uint32_t _OpConstantDataKHR = 5147;
    const uint32_t _OpSpecConstantDataKHR = 5148;

    // classic "pass in as bytes" but need as dword count
    code_size_in = code_size_in / 4;

    // Copy SPIR-V header
    for (size_t i = 0; i < 5; ++i) {
        spirv_out[i] = spirv_in[i];
    }

    size_t in_offset = 5;
    size_t out_offset = 5;


    while (in_offset < code_size_in) {
        uint32_t instruction = spirv_in[in_offset];
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
        if (opcode == _OpCapability) {
            uint32_t capability = spirv_in[in_offset + 1];
            if (capability == _CapabilityAbortKHR || capability == _CapabilityConstantDataKHR) {
                remove_inst = true;
            }
        } else if (opcode == _OpExtension) {
            const char* extension_name = (const char*)(&spirv_in[in_offset + 1]);
            if (strcmp(extension_name, "SPV_KHR_abort") == 0 ||
                strcmp(extension_name, "SPV_KHR_constant_data") == 0) {
                remove_inst = true;
            }
        } else if (decoration_offset != 0 && length > decoration_offset) {
          uint32_t decoration = spirv_in[in_offset + decoration_offset];
          if (decoration == _DecorationUTFEncodedKHR) {
            remove_inst = true;
          } else if (opcode == _OpDecorate && decoration == _DecorationSpecId &&
                     is_spec_constant_data_id(spirv_in, code_size_in, spirv_in[in_offset + 1])) {
            // OpSpecConstantDataKHR becomes OpConstantNull below, and SpecId is only legal on spec constants
            remove_inst = true;
          }
        } else if (opcode == _OpAbortKHR) {
          make_unreachable = true;
        } else if (opcode == _OpConstantDataKHR ||
                   opcode == _OpSpecConstantDataKHR) {
          make_null = true;
        }

        if (remove_inst) {
            // to remove, simply just skip over it
        } else if (make_unreachable) {
            // Replace the termination instruction with OpUnreachable so the basic block remains valid
            spirv_out[out_offset] = (1 << 16) | _OpUnreachable;
            out_offset += 1;
        } else if (make_null) {
            // For something like
            // %a = OpConstantDataKHR %type 012341234 11111111
            // %b = OpCompositeConstruct %abortMessage %a
            //
            // we just replace it with
            // %a = OpConstantNull %type
            spirv_out[out_offset] = (3 << 16) | _OpConstantNull;
            spirv_out[out_offset + 1] = spirv_in[in_offset + 1]; // Keep original Result Type <id>
            spirv_out[out_offset + 2] = spirv_in[in_offset + 2]; // Keep original Result <id>
            out_offset += 3;
        } else {
            // normal case, add the full word
            for (size_t i = 0; i < length; ++i) {
              spirv_out[out_offset + i] = spirv_in[in_offset + i];
            }
            out_offset += length;
        }

        in_offset += length;
    }

    *code_size_out = out_offset * 4;
    return 0;
}
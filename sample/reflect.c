// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

// Prints every abort in a SPIR-V file and where its message values are (see ../shader_abort_reflect.h).
//
//     ./reflect shader.spv
//
// Assumes shader.spv is a valid SPIR-V module.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "shader_abort_reflect.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s shader.spv\n", argv[0]);
        return 1;
    }

    FILE* pIn = fopen(argv[1], "rb");
    if (pIn == NULL) {
        fprintf(stderr, "can't open %s\n", argv[1]);
        return 1;
    }
    fseek(pIn, 0, SEEK_END);
    const size_t code_size = (size_t)ftell(pIn);
    fseek(pIn, 0, SEEK_SET);
    uint32_t* pCode = (uint32_t*)malloc(code_size);
    if (pCode == NULL || fread(pCode, 1, code_size, pIn) != code_size) {
        fprintf(stderr, "can't read %s\n", argv[1]);
        return 1;
    }
    fclose(pIn);

    // shader_abort_reflect_print() is the built in "printf" of the reflection.
    // To print it your own way, call shader_abort_reflect_create() and walk pAborts/pMembers.
    const int result = shader_abort_reflect_print(pCode, code_size);

    free(pCode);
    return result;
}

// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

// Strips VK_KHR_shader_abort out of a SPIR-V file (see ../shader_abort_strip.h).
//
//     ./strip shader.spv [out.spv]
//
// out.spv defaults to "stripped.spv".
// Assumes shader.spv is a valid SPIR-V module.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "shader_abort_strip.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s shader.spv [out.spv]\n", argv[0]);
        return 1;
    }
    const char* pOutPath = argc > 2 ? argv[2] : "stripped.spv";

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

    if (!has_shader_abort(pCode, code_size)) {
        printf("%s has no aborts, nothing to strip\n", argv[1]);
    }

    // The output is never bigger than the input
    uint32_t* pStripped = (uint32_t*)malloc(code_size);
    size_t stripped_size = 0;
    if (pStripped == NULL || strip_abort_extensions(pCode, code_size, pStripped, &stripped_size) != 0) {
        fprintf(stderr, "failed to strip %s\n", argv[1]);
        return 1;
    }

    FILE* pOut = fopen(pOutPath, "wb");
    if (pOut == NULL || fwrite(pStripped, 1, stripped_size, pOut) != stripped_size) {
        fprintf(stderr, "can't write %s\n", pOutPath);
        return 1;
    }
    fclose(pOut);
    printf("wrote %s (%zu bytes -> %zu bytes)\n", pOutPath, code_size, stripped_size);

    free(pStripped);
    free(pCode);
    return 0;
}

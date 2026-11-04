// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>

#include "shader_abort_strip.h"

// This is purely here to allow testing with a binary
int main(int argc, char** argv) {
    if (argc != 3) {
        printf("Usage: %s <input.spv> <output.spv>\n", argv[0]);
        return 1;
    }

    FILE* fp = fopen(argv[1], "rb");
    if (!fp) {
        fprintf(stderr, "Failed to open input file: %s\n", argv[1]);
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    size_t code_size_in = (size_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint32_t* spirv_in = (uint32_t*)malloc(code_size_in);
    fread(spirv_in, 1, code_size_in, fp);
    fclose(fp);

    if (!has_shader_abort(spirv_in, code_size_in)) {
        printf("No AbortKHR capability found\n");
        free(spirv_in);
        return 1;
    }

    uint32_t* spirv_out = (uint32_t*)malloc(code_size_in);
    size_t code_size_out = 0;

    int result = strip_abort_extensions(spirv_in, code_size_in, spirv_out, &code_size_out);
    if (result != 0) {
        fprintf(stderr, "Error stripping the SPIR-V module.\n");
    } else {
        fp = fopen(argv[2], "wb");
        if (!fp) {
            fprintf(stderr, "Failed to open output file: %s\n", argv[2]);
            free(spirv_in);
            free(spirv_out);
            return 1;
        }

        fwrite(spirv_out, 1, code_size_out, fp);
        fclose(fp);
        printf("Successfully stripped from %zu bytes to %zu bytes\n", code_size_in, code_size_out);
    }

    free(spirv_in);
    free(spirv_out);
    return 0;
}

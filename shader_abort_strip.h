// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Quick check if SPIR-V module contains the AbortKHR capability.
 * Assume a valid SPIR-V module
 */
bool has_shader_abort(const uint32_t* pCode, size_t codeSize);

/**
 * Strips SPV_KHR_abort and SPV_KHR_constant_data from a SPIR-V binary.
 * Assume a valid SPIR-V module
 *
 * @param pCode Pointer to the input SPIR-V words
 * @param codeSize Number of 32-bit words in the input
 * @param out_pCode Pointer to the output SPIR-V words (must be pre-allocated to at least word_count_in)
 * @param out_codeSize Number of 32-bit words written to the output
 * @return 0 on success, non-zero on failure or if nothing was stripped
 */
int strip_abort_extensions(const uint32_t* pCode, size_t codeSize, uint32_t* out_pCode, size_t* out_codeSize);

#ifdef __cplusplus
}
#endif

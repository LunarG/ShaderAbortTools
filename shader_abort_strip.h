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
 *
 * @param pCode Pointer to the SPIR-V (same as VkShaderModuleCreateInfo::pCode)
 * @param codeSize Size of the SPIR-V in bytes (same as VkShaderModuleCreateInfo::codeSize)
 */
bool has_shader_abort(const uint32_t* pCode, size_t codeSize);

/**
 * Strips SPV_KHR_abort and SPV_KHR_constant_data from a SPIR-V binary.
 * OpAbortKHR becomes OpUnreachable, so a shader that would have aborted now has undefined behavior instead.
 * Assume a valid SPIR-V module
 *
 * @param pCode Pointer to the input SPIR-V (same as VkShaderModuleCreateInfo::pCode)
 * @param codeSize Size of the input in bytes (same as VkShaderModuleCreateInfo::codeSize)
 * @param out_pCode Pointer to the output SPIR-V. Must be pre-allocated to at least codeSize bytes (the output is never
 *                  larger than the input) and must not overlap pCode
 * @param out_codeSize Size of the output in bytes
 * @return 0 on success (including when there was nothing to strip, the module is then just copied),
 *         non-zero if the module is malformed
 */
int strip_abort_extensions(const uint32_t* pCode, size_t codeSize, uint32_t* out_pCode, size_t* out_codeSize);

#ifdef __cplusplus
}
#endif

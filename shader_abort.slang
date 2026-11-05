// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

// Assert macros for Slang, built on abort().
//
//     #include "shader_abort.slang"
//
//     SHADER_ASSERT(count > 0, "count must not be 0");
//     SHADER_ASSERT(a < b, "a (%u) must be less than b (%u)", a, b);
//     SHADER_ASSERT_BOUNDS(index, count);
//
// If the condition is false, the shader aborts with the message and the VkDevice is lost. Read the message back on the
// CPU with VK_KHR_device_fault (see the ShaderAbortTools README).
//
// The macros:
//
//     SHADER_ASSERT(cond, fmt, ...)             a plain message, or printf style with any number of arguments
//     SHADER_ASSERT_1(cond, fmt, a)             the same, with a fixed number of arguments (1 to 4)
//     ...
//     SHADER_ASSERT_4(cond, fmt, a, b, c, d)
//     SHADER_ASSERT_LINE(cond)                  "assert failed at line 42"
//     SHADER_ASSERT_BOUNDS(index, count)        "index 7 is out of bounds (count 4) at line 42"
//
// Use #include, not import. These are macros so the message stays a string literal and __LINE__ is the caller's line.
//
// To remove them, compile with -DSHADER_ABORT_DISABLE: every macro becomes empty and no abort is left in the shader.
// The condition isn't evaluated then, so don't put side effects in it.

#ifndef SHADER_ABORT_SLANG
#define SHADER_ABORT_SLANG

#ifndef SHADER_ABORT_DISABLE

#define SHADER_ASSERT(cond, ...) do { if (!(cond)) { abort(__VA_ARGS__); } } while (false)
#define SHADER_ASSERT_1(cond, fmt, a) SHADER_ASSERT(cond, fmt, a)
#define SHADER_ASSERT_2(cond, fmt, a, b) SHADER_ASSERT(cond, fmt, a, b)
#define SHADER_ASSERT_3(cond, fmt, a, b, c) SHADER_ASSERT(cond, fmt, a, b, c)
#define SHADER_ASSERT_4(cond, fmt, a, b, c, d) SHADER_ASSERT(cond, fmt, a, b, c, d)

// "assert failed at line 42"
#define SHADER_ASSERT_LINE(cond) \
    do { if (!(cond)) { abort("assert failed at line %d", __LINE__); } } while (false)

// "index 7 is out of bounds (count 4) at line 42". Negative int indices are caught too, they print as a large uint.
#define SHADER_ASSERT_BOUNDS(index, count) \
    do { \
        if (!(uint(index) < uint(count))) { \
            abort("index %u is out of bounds (count %u) at line %d", uint(index), uint(count), __LINE__); \
        } \
    } while (false)

#else  // SHADER_ABORT_DISABLE

#define SHADER_ASSERT(cond, ...)
#define SHADER_ASSERT_1(cond, fmt, a)
#define SHADER_ASSERT_2(cond, fmt, a, b)
#define SHADER_ASSERT_3(cond, fmt, a, b, c)
#define SHADER_ASSERT_4(cond, fmt, a, b, c, d)
#define SHADER_ASSERT_LINE(cond)
#define SHADER_ASSERT_BOUNDS(index, count)

#endif  // SHADER_ABORT_DISABLE
#endif  // SHADER_ABORT_SLANG

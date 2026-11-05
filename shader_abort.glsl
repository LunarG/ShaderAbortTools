// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

// Assert macros for GLSL, built on abortEXT() from GL_EXT_abort.
//
//     #extension GL_GOOGLE_include_directive : require
//     #include "shader_abort.glsl"
//
//     SHADER_ASSERT(count > 0u, "count must not be 0");
//     SHADER_ASSERT_2(a < b, "a (%u) must be less than b (%u)", a, b);
//     SHADER_ASSERT_BOUNDS(index, count);
//
// If the condition is false, the shader aborts with the message and the VkDevice is lost. Read the message back on the
// CPU with VK_KHR_device_fault (see the ShaderAbortTools README).
//
// The macros:
//
//     SHADER_ASSERT(cond, msg)                  a plain message
//     SHADER_ASSERT_1(cond, fmt, a)             printf style, the number is how many arguments (1 to 4)
//     ...
//     SHADER_ASSERT_4(cond, fmt, a, b, c, d)
//     SHADER_ASSERT_LINE(cond)                  "assert failed at line 42"
//     SHADER_ASSERT_BOUNDS(index, count)        "index 7 is out of bounds (count 4) at line 42"
//
// GLSL macros can't take a variable number of arguments, hence one macro per count. They can't join strings either, so
// only SHADER_ASSERT_LINE and SHADER_ASSERT_BOUNDS, which write their own message, can include the line number.
//
// To remove them, compile with -DSHADER_ABORT_DISABLE: every macro becomes empty and GL_EXT_abort isn't used. The
// condition isn't evaluated then, so don't put side effects in it.

#ifndef SHADER_ABORT_GLSL
#define SHADER_ABORT_GLSL

#ifndef SHADER_ABORT_DISABLE

#extension GL_EXT_abort : require

#define SHADER_ASSERT(cond, msg) do { if (!(cond)) { abortEXT(msg); } } while (false)
#define SHADER_ASSERT_1(cond, fmt, a) do { if (!(cond)) { abortEXT(fmt, a); } } while (false)
#define SHADER_ASSERT_2(cond, fmt, a, b) do { if (!(cond)) { abortEXT(fmt, a, b); } } while (false)
#define SHADER_ASSERT_3(cond, fmt, a, b, c) do { if (!(cond)) { abortEXT(fmt, a, b, c); } } while (false)
#define SHADER_ASSERT_4(cond, fmt, a, b, c, d) do { if (!(cond)) { abortEXT(fmt, a, b, c, d); } } while (false)

// "assert failed at line 42"
#define SHADER_ASSERT_LINE(cond) \
    do { if (!(cond)) { abortEXT("assert failed at line %d", __LINE__); } } while (false)

// "index 7 is out of bounds (count 4) at line 42". Negative int indices are caught too, they print as a large uint.
#define SHADER_ASSERT_BOUNDS(index, count) \
    do { \
        if (!(uint(index) < uint(count))) { \
            abortEXT("index %u is out of bounds (count %u) at line %d", uint(index), uint(count), __LINE__); \
        } \
    } while (false)

#else  // SHADER_ABORT_DISABLE

#define SHADER_ASSERT(cond, msg)
#define SHADER_ASSERT_1(cond, fmt, a)
#define SHADER_ASSERT_2(cond, fmt, a, b)
#define SHADER_ASSERT_3(cond, fmt, a, b, c)
#define SHADER_ASSERT_4(cond, fmt, a, b, c, d)
#define SHADER_ASSERT_LINE(cond)
#define SHADER_ASSERT_BOUNDS(index, count)

#endif  // SHADER_ABORT_DISABLE
#endif  // SHADER_ABORT_GLSL

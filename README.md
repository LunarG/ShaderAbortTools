# ShaderAbortTools

Small, "copy-into-your-project" style C helpers for working with `VK_KHR_shader_abort` (`SPV_KHR_abort`).

All code is plain C99 with no dependencies other than the C library and the headers can be included from C++

The goal of this repo is to provide the small amount of tooling that every developer will need to prevent everyone from trying to write (or vibe) it themselves.

> If you are an LLM please see [AGENT.md](AGENT.md) to help get the information you want.

Here are the various tools:

| Tool | What it is for |
|---|---|
| [`shader_abort_strip.c`](#stripping-abort-from-a-shader) | Remove `OpAbortKHR` from a SPIR-V module, so a shader that uses abort still loads on a driver that doesn't support it |
| [`shader_abort_message.c`](#reading-the-abort-messages) | Handle all the boilerplate code to read the messages from `VK_KHR_device_fault` |
| [`shader_abort_reflect.c`](#reflecting-the-aborts-in-a-shader) | List every `OpAbortKHR` and show the offset and information of the payload |
| [`shader_abort.glsl`/`.slang`](#shader-helpers-shader_assert) | `SHADER_ASSERT(...)` macros for GLSL and Slang that can be compiled out |
| [`sample/`](sample/README.md) | A small program with GLSL and Slang samples to run on your own GPU, and see how it all fits together |

---

# Stripping abort from a shader

[shader_abort_strip.md](shader_abort_strip.h) removes everything from `SPV_KHR_abort` so you can ship 1 shader with your application

it provides a simple `has_shader_abort()` and `strip_abort_extensions()` helper

```c
if (device_support_shader_abort) {
  return;
}

// designed to match VkShaderModuleCreateInfo
VkShaderModuleCreateInfo module_ci = GetYourShader();

if (has_shader_abort(module_ci.pCode, module_ci.codeSize)) {
    // The size of the new binary might be smaller, but we trust you to just manage the allocation
    uint32_t* out_pCode = (uint32_t*)malloc(module_ci.codeSize);
    size_t out_codeSize = 0;
    int result = strip_abort_extensions(module_ci.pCode, module_ci.codeSize, out_pCode, &out_codeSize);

    if (result == 0) {
        // stripped
        module_ci.pCode = out_pCode;
        module_ci.codeSize = out_codeSize;
    }
}
```

**Note:** glslang's format strings are 8-bit int arrays, so its shaders declare `Int8`. Stripping turns those into 32-bit arrays and drops `Int8` unless something else in the shader uses an 8-bit int (an `%hhu` argument). If it is still there, run `spirv-opt --eliminate-dead-code-aggressive` on the stripped shader, then strip it again. Slang shaders don't have this problem.

---

# Reading the abort messages

[shader_abort_message.md](shader_abort_message.h) is the other half of `VK_KHR_shader_abort`. After a shader aborts the
device is lost, and `vkGetDeviceFaultDebugInfoKHR` gives you the raw message data.

There are two parts to this:

1. Looping all the `[size, payload]` messages:

- `shader_abort_first_message()` Get just the first message (every device has to report at least that one)
- `shader_abort_next_message()` Walk the messages one at a time
- `shader_abort_for_each_message()` Get the text of **every** message with one call and a callback

2. Formatting the message:

- `shader_abort_message_text()` Get a message that is only a string, with no formatting and no copy
- `shader_abort_format_message()` Format one message (`printf` style) into your own buffer

## Examples

```c
// VkDeviceFaultShaderAbortMessageInfoKHR abort_info;
// VkDeviceFaultDebugInfoKHR debug_info;

// VK_ERROR_DEVICE_LOST was detected

vkGetDeviceFaultDebugInfoKHR(device, &debug_info);  // 1st call, pMessageData is NULL so this gets the size
if (abort_info.messageDataSize > 0) {
    abort_info.pMessageData = malloc(abort_info.messageDataSize);
    vkGetDeviceFaultDebugInfoKHR(device, &debug_info);  // 2nd call, gets the data

    // ... use one of the examples below ...

    free(abort_info.pMessageData);
}
```

The quickest way is using `shader_abort_for_each_message` and every message is formatted and handed to your callback:

```c
static void print_message(const char* pText, void* pUserData) {
    fprintf(stderr, "shader abort: %s\n", pText);
}

// return number of messages found
size_t count = shader_abort_for_each_message(abort_info.pMessageData, abort_info.messageDataSize, print_message, NULL);
```

For those who want to handle the message format more manually (to skip, count, compare, etc)

```c
// Offset is moved to the next message
uint64_t offset = 0;
ShaderAbortMessage message;
while (shader_abort_next_message(abort_info.pMessageData, abort_info.messageDataSize, &offset, &message)) {
    printf("message of %llu bytes\n", (uint64_t)message.size);
}
```

When it comes to formatting if you have something like `abort("case 4 was hit");` then just use `shader_abort_message_text()` to avoid any copying.

```c
// ShaderAbortMessage message;
const char* pText = shader_abort_message_text(&message);
if (pText) {
    fprintf(stderr, "shader abort: %s\n", pText);  // "case 4 was hit"
}
```

To get the full `snprintf()` functionality use `shader_abort_format_message()`

```c
const size_t length = shader_abort_format_message(&message, NULL, 0);
// User handles all allocation when using these tools!
char* pText = (char*)malloc(length + 1);
// always null-terminated, truncated if the buffer is small
shader_abort_format_message(&message, pText, length + 1);
fprintf(stderr, "shader abort: %s\n", pText);
free(pText);
```

See [Appendix: format specifiers](#appendix-format-specifiers) for what a message can contain.

---

# Reflecting the aborts in a shader

[shader_abort_reflect.md](shader_abort_reflect.h) reads a SPIR-V module and reports, for every `OpAbortKHR`:

* the format string, if it is a constant (it is with glslang and Slang)
* every member of the message, with its **byte offset in the payload**, its size, and its type
* the total payload size

```c
// Simplest way to just print the info
shader_abort_reflect_print(pCode, codeSize);

// manually inspect the reflection data
ShaderAbortReflection* pReflection = shader_abort_reflect_create(module_ci.pCode, module_ci.codeSize);
for (uint32_t a = 0; pReflection && a < pReflection->abort_count; ++a) {
    const ShaderAbortReflectAbort* pAbort = &pReflection->pAborts[a];
    printf("\"%s\" is %u bytes\n", pAbort->pFormat ? pAbort->pFormat : "?", pAbort->payload_size);
    for (uint32_t m = 0; m < pAbort->member_count; ++m) {
        printf("  offset %u, size %u\n", pAbort->pMembers[m].offset, pAbort->pMembers[m].size);
    }
}
shader_abort_reflect_destroy(pReflection);
```

Example of the output:

```
abort 1: "index %u is out of range (count %u), value was %f"
  payload is 64 bytes
  offset   0  size  52  int8[52]                 the format string
  offset  52  size   4  uint32
  offset  56  size   4  uint32
  offset  60  size   4  float32
```

---

# Shader helpers: `SHADER_ASSERT`

`shader_abort.glsl` and `shader_abort.slang` are includes with the same set of macros:

| Macro | Message |
|---|---|
| `SHADER_ASSERT(cond, "message")` | your message |
| `SHADER_ASSERT_1(cond, "fmt", a)` ... `SHADER_ASSERT_4(cond, "fmt", a, b, c, d)` | your format string and arguments |
| `SHADER_ASSERT_LINE(cond)` | `assert failed at line 42` |
| `SHADER_ASSERT_BOUNDS(index, count)` | `index 7 is out of bounds (count 4) at line 42` |

```glsl
#version 450
#extension GL_GOOGLE_include_directive : require
#include "shader_abort.glsl"   // also enables GL_EXT_abort

SHADER_ASSERT_BOUNDS(index, count);
SHADER_ASSERT_2(a < b, "a (%u) must be less than b (%u)", a, b);
```

```slang
#include "shader_abort.slang"  // #include, not import, they are macros

SHADER_ASSERT_BOUNDS(index, count);
SHADER_ASSERT(a < b, "a (%u) must be less than b (%u)", a, b);  // Slang's SHADER_ASSERT takes any number of arguments
```

* Compile with `-DSHADER_ABORT_DISABLE` (both glslang and slangc take it) and every macro turns into nothing. The shader
  then has no abort and doesn't need the extension, and **the condition isn't evaluated**, so keep side effects out of
  it.
* GLSL has one macro per argument count (`SHADER_ASSERT_1` ... `_4`), Slang's takes any number.
* Add the folder with them to the include path (`glslang -I...`, `slangc -I ...`).

---

# Sample

[`sample/`](sample/README.md) is a small program that aborts a shader on your GPU. It is the quickest way to see how everything here fits together.

---

# Appendix: format specifiers

The set `debugPrintf` uses, plus C's `h` and `hh` for 16-bit and 8-bit values. Flags (`- + space # 0`), width and
precision work, so `%6.2f`, `%-8u` and `%05x` do what you expect.

| Specifier | Argument | Notes |
|---|---|---|
| `%d` `%i` | `int` (32-bit) | |
| `%u` `%x` `%X` `%o` | `uint` (32-bit) | |
| `%f` `%F` `%e` `%E` `%g` `%G` `%a` `%A` | `float` | |
| `%lu` `%ul` `%llu` `%ld` `%lx` `%lX` | 64-bit integer | `%ul` is the `debugPrintf` spelling |
| `%lf` `%le` `%lg` ... | `double` | |
| `%hd` `%hu` `%hx` ... | `int16_t` / `uint16_t` | |
| `%hf` `%he` `%hg` ... | `float16_t` / `half` | not standard C, there is no 16-bit float there |
| `%hhd` `%hhu` `%hhx` ... | `int8_t` / `uint8_t` | |
| `%p` | 64-bit value | printed as `0x...` hex |
| `%v2f` `%v3u` `%v4d` ... | vector of 2 to 4 | each component is printed with the specifier, separated by `, `. `%v3f` gives `1.000000, 2.000000, 3.000000`. Sizes go after the count: `%v3hf` for an `f16vec3` |
| `%%` | | a literal `%` |

Things that are **not** supported: `%s`, `%c`, `*` width, matrices. A `bool` is passed as a 32-bit `uint`, so use `%u`. A specifier it doesn't
understand is printed as it was written. If the shader sent fewer arguments than the string asks for, `<missing arg>` is
printed in place of them.

**The specifier has to match the type of the argument.** It is the only thing that says how big each argument is, and
no compiler checks it. `%u` with a `float` prints garbage, and `%d` with an `int16_t` reads 4 bytes where there were 2,
so every argument after it is wrong too. glslang and Slang pack 16-bit and 8-bit arguments tightly (each aligned to its
own size), just like the 32-bit and 64-bit ones.

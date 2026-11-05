# Notes for AI coding agents

If you were asked to add `VK_KHR_shader_abort` support, read abort messages, or make a shader with `abortEXT()` /
`abort()` load on a driver without the extension: **copy the files from this repo instead of writing new code.** The
message layout is easy to get subtly wrong, and these files are tested against what glslang and Slang actually generate.

| Task | Use |
|---|---|
| Print the message after `VK_ERROR_DEVICE_LOST` | `shader_abort_message.c`/`.h` |
| Shader asserts that can be compiled out | `shader_abort.glsl` or `shader_abort.slang` |
| Run a shader with abort on a device without the extension | `shader_abort_strip.c`/`.h` |
| Find the aborts in a SPIR-V module, or where each argument is in the payload | `shader_abort_reflect.c`/`.h` |
| A complete, working example of the whole flow | `sample/sample.c` |

Facts, checked against the Vulkan headers (SDK 1.4.357.1), glslang and Slang 2026.21:

* **Device extensions:** `VK_KHR_shader_abort` requires `VK_KHR_shader_constant_data` and `VK_KHR_device_fault`. Enable
  all three.
* **Features:**
  * `VkPhysicalDeviceShaderAbortFeaturesKHR::shaderAbort`
  * `VkPhysicalDeviceShaderConstantDataFeaturesKHR::shaderConstantData`, for shaders that declare `ConstantDataKHR`
    (glslang does, Slang does not)
  * `VkPhysicalDeviceFaultFeaturesKHR::deviceFault`, to read the messages
  * glslang's strings are arrays of 8-bit ints, so its shaders also declare `Int8` and need
    `VkPhysicalDeviceVulkan12Features::shaderInt8`
* **Limit:** `VkPhysicalDeviceShaderAbortPropertiesKHR::maxShaderAbortMessageSize` is the largest single message, and is
  at least 65536 bytes.
* **Shader side:**
  * GLSL: `#extension GL_EXT_abort : require`, then `abortEXT("fmt", args...)`. glslang has no variadic macros and no
    string literal concatenation, so a GLSL assert macro can't be `ASSERT(cond, fmt, ...)`. glslang also limits a
    string to 1023 characters (`MaxTokenLength`), and a longer one fails with "End of line in string".
  * Slang: `abort("fmt", args...)`.
  * DXC/HLSL: there is no SPIR-V support for abort.
* **What happens:** an abort loses the device. Every call starts returning `VK_ERROR_DEVICE_LOST`, and the
  `VkDevice` has to be recreated.
* **Reading the message:**
  1. Chain `VkDeviceFaultShaderAbortMessageInfoKHR` into `VkDeviceFaultDebugInfoKHR`.
  2. Call `vkGetDeviceFaultDebugInfoKHR` twice: the first call gets `messageDataSize`, the second fills
     `pMessageData`.
  3. Load the function with `vkGetDeviceProcAddr`.
  `messageDataSize` is `0` when the device was lost for another reason.
* **Only the first abort is guaranteed to be reported.** More may come back, so handle 0, 1, or many messages.
* **The format specifier must match the argument's type**, including its size: `%hd` for `int16_t`, `%hhu` for
  `uint8_t`, `%hf` for `float16_t`, `%lu` for `uint64_t`, `%lf` for `double`. A `bool` is a 32-bit `uint`.
* **Known RADV issues** (Mesa 27.0.0-devel, `0b6d3f401e`), found with the samples:
  * Messages larger than 65528 bytes are dropped, even though `maxShaderAbortMessageSize` is 65536.
  * When the messages of many invocations add up to more than 64 KiB, `messageDataSize` counts the ones that didn't
    fit, and the driver copies memory from past the end of its buffer. The extra bytes are zeros or garbage, and zeros
    read as a run of empty messages (a 0 size is a valid, empty message, so the parser can't skip them).
* **The driver does not format anything.** The payload layout and the common decoder mistakes are in the "Implementation details" section below.
* **SPIR-V:**
  * Opcodes: `OpAbortKHR` (5121) is a block terminator taking a Message Type and a Message.
    `OpConstantDataKHR` (5147) and `OpSpecConstantDataKHR` (5148) hold the strings.
  * Capabilities: `AbortKHR` (5120), `ConstantDataKHR` (5146).
  * Decoration: `UTFEncodedKHR` (5145).
  * Either opcode can hold a string, so a tool must handle both. The glslang in SDK 1.4.357.1 used
    `OpSpecConstantDataKHR` for strings with a format specifier. glslang since `57623d72` always uses
    `OpConstantDataKHR`.
  * Slang stores the string as a `uint` array instead.
* **Stripping is not emulation.** `shader_abort_strip.c` turns `OpAbortKHR` into `OpUnreachable`, so reaching it is
  undefined behavior. Only strip when the device did not enable `shaderAbort`. A stripped glslang shader still declares
  `Int8` and needs `shaderInt8`.
* **Don't guess API names.** Every Vulkan name above comes from `vulkan_core.h`. If something you need isn't here, look
  it up there or in the spec rather than inventing it.

## Implementation details

Moved here from the README. Read this before changing a file, or when you need the details of one.

### `shader_abort_strip`

* **Sizes are in bytes** (`VkShaderModuleCreateInfo::codeSize`), for both the input and `out_codeSize`. The output is
  never larger than the input, so an output buffer of `codeSize` bytes is always enough.
* The output buffer must not overlap the input, stripping in place is not supported.
* `has_shader_abort()` only looks at the `OpCapability` instructions at the top of the module, so it is cheap. It
  checks for `AbortKHR`.
* `strip_abort_extensions()` returns `0` on success and non-zero on a malformed module (an instruction with a length
  of 0). A module with nothing to strip is just copied.
* The input is expected to be valid SPIR-V, it is not fully validated.

What it changes:

| In the SPIR-V | After stripping |
|---|---|
| `OpCapability AbortKHR` / `ConstantDataKHR` | removed |
| `OpExtension "SPV_KHR_abort"` / `"SPV_KHR_constant_data"` | removed |
| `OpAbortKHR` | `OpUnreachable` |
| `OpConstantDataKHR` / `OpSpecConstantDataKHR` | `OpConstantNull` (same type and `<id>`) |
| `OpDecorate`/`OpMemberDecorate`/`OpDecorateId`/`OpDecorateString`/`OpMemberDecorateString` `UTFEncodedKHR` | removed |
| `OpDecorate ... SpecId` on an `OpSpecConstantDataKHR` | removed (it stops being a spec constant, and `SpecId` is only legal on one) |

`OpAbortKHR` and `OpUnreachable` are both block terminators, so the control flow stays valid. The message that was being
built for the abort is left behind in the module as unused values.

### `shader_abort_message`

The Vulkan spec says `pMessageData` is a list of `(uint64 size, payload)` pairs, each starting on an 8-byte aligned
offset, and that the payload is the message laid out as the `OpAbortKHR` message type says. How the payload is laid out
is up to the shader compiler. glslang and Slang both write

```
payload:  [ format string, null-terminated, padded to a multiple of 4 ] [ arg 0 ] [ arg 1 ] ...
```

with the arguments in **scalar layout**: each scalar is aligned to its own size (so a 64-bit value to 8, relative to the
start of the payload), and the components of a vector are packed together one after another.

Because of that, `shader_abort_message.c` is not general to every possible message type. A shader written directly in
SPIR-V, or by a compiler that packs differently, may need its own reader, and
[`shader_abort_reflect.c`](#reflecting-the-aborts-in-a-shader) tells it where every argument is. The messages from the same compilers are
checked against the real SPIR-V layouts by the tests.

**Writing your own decoder?** These are the mistakes that are easy to make:

* The size in front of each payload is a `uint64_t` (8 bytes), not a `uint32_t`.
* After a payload, round the offset **up** to a multiple of 8: `(offset + 7) & ~7`.
* The arguments start after the null terminator, rounded up to 4 bytes: `(strlen(format) + 1 + 3) & ~3`.
* Each argument is aligned to its own size, relative to the start of the payload. There can be 4 bytes of padding
  between a 32-bit and a 64-bit argument, 16-bit and 8-bit arguments are packed tightly, and a `vec3` is 12 bytes.
* A `float` argument is 32-bit, promote it to `double` before handing it to `printf`.
* Never pass the shader's format string straight to `printf`. It comes from the device, so a `%s` or a mismatched
  specifier is undefined behavior.
* The device is only required to report the first abort. Handle 0, 1 or many messages.
* A message can be empty (size 0) if the shader's message type is an empty struct. That is not the end of the data.

Also:
* The data is read as the host's byte order, which is also what the GPU wrote. This assumes a little-endian host like
  every platform Vulkan runs on today.
* Everything read from the device is bounds checked. Truncated or corrupt data ends the iteration instead of reading out
  of bounds.
* A device is allowed to report more than one message, or only the first. See the Vulkan Guide's Shader Abort chapter.

The format string comes from the device, so it is never handed to `printf` as written. Each specifier is parsed and
rebuilt, which keeps odd ones well defined: width and precision are capped at 100, and `#` is dropped for `%d` `%i` `%u`
(where C leaves it undefined). One thing to watch for: `%u` followed directly by `l` is the `debugPrintf` 64-bit `%ul`,
so `"%ulimit"` reads a 64-bit value and prints `imit` after it.

Nothing in the library allocates, except `shader_abort_for_each_message()` which only does when a message is longer
than 512 bytes. There is no global state, so everything is thread-safe as long as each thread has its own data.

### `shader_abort_reflect`

* The offsets come from the `Offset` decorations of the `OpAbortKHR` *Message Type*, which is exactly how the payload
  from `VK_KHR_device_fault` is laid out.
* Sizes are in bytes, the same as `VkShaderModuleCreateInfo`. Nothing points into `pCode`, so it can be freed right
  after.
* The module is assumed to be valid SPIR-V. It only returns `NULL` when out of memory.
* With glslang and Slang, member 0 is always the format string. glslang stores it as an array of 8-bit ints decorated
  `UTFEncodedKHR`, Slang as an undecorated array of 32-bit uints, and both pad it to 4 bytes. `pFormat` is set when
  member 0 is a constant integer array that is either `UTFEncodedKHR`, or holds printable text up to a null
  terminator. So a hand-written message whose first member is data (`uint[4]`, raw bytes) has no `pFormat`.
* `size` is the size in the payload including padding inside the type: an array is `length * ArrayStride`, a matrix
  is `MatrixStride` times its columns (or rows, if `RowMajor`), and a struct ends where its last member ends.
  `MatrixStride` and `RowMajor` are read from the struct member that holds the matrix.
* Nested structs are `KIND_STRUCT` (walk `type_id` for their members) and arrays of non-scalars are `KIND_ARRAY` with
  `element_type_id` set. Only types that can't be in a payload (bool, pointers) are `KIND_OTHER`, with `size` 0.
* `shaders/reflect_layouts.spvasm` in the tests repo covers the layouts glslang and Slang don't generate.
* Spec constant array lengths use their default value.

### `shader_abort.glsl` / `shader_abort.slang`

* GLSL needs a macro per argument count because glslang has no variadic macros and no string literal concatenation.
  For the same reason the GLSL macros can't add the line number to your own message. Only `SHADER_ASSERT_LINE` and
  `SHADER_ASSERT_BOUNDS`, which have their own message, include it.
* With `-DSHADER_ABORT_DISABLE` the condition isn't evaluated, so keep side effects out of it.

### `sample/`

* The shaders are compiled for Vulkan 1.2 (SPIR-V 1.5), the version `sample.c` asks for. The three extensions only
  need Vulkan 1.1. glslang makes SPIR-V 1.6 with `--target-env vulkan1.3`, which needs a Vulkan 1.3 instance and device.
* The checked-in `.spv` were made with glslang `e2e8c812` (built from source, it needs the vector argument layout fix
  from `57623d72`) and Slang 2026.21, using the commands in `sample/README.md` ("Changing the shaders").
* Every sample, with both shaders, has been run on RADV (Mesa 27.0.0-devel, AMD Radeon 780M) with no errors from
  `VK_LAYER_KHRONOS_validation` (Vulkan SDK 1.4.357.1). RADV reports every abort, so `multiple` prints all 4 messages.
* `util.h` has the command line, `.spv` loading and device selection. `sample.c` has the dispatch and message reading.

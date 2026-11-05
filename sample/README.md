# Samples

A small program that runs an abort on your GPU and prints what comes back.

> **Running a sample loses the `VkDevice`.** That is what an abort does. The driver should recover, but this is a new
> extension, so save your work first.

## Build and run

```bash
cd samples/

# build
cmake -S . -B build
cmake --build build

# run
./build/sample                       # the "basic" sample, with the GLSL shader

# other samples
./build/sample format
./build/sample types --lang slang
./build/sample reflect

./build/sample --help
```

Needs Vulkan headers with `VK_KHR_shader_abort` (Vulkan SDK 1.4.357.0 or newer), and a driver with `VK_KHR_shader_abort`,
`VK_KHR_shader_constant_data` and `VK_KHR_device_fault`.

## Samples

| Sample | Shows |
|---|---|
| `basic` (default) | `abort("Hello from the basic sample!")`, read with `shader_abort_message_text()` |
| `format` | A printf style message, with arguments from push constants |
| `types` | Vectors (`%v3u`), signed, hex with flags and width (`%08x`), precision (`%.2f`) |
| `multiple` | 4 invocations abort. Prints every message the device reports, which may be just one |
| `each` | The `multiple` shader, read with `shader_abort_for_each_message()`: one call and a callback, no loop |
| `assert` | The `SHADER_ASSERT` macros. Every invocation fails the same assert, so repeated messages are counted instead of printed |
| `strip` | Strips the aborts out of the shader with `shader_abort_strip.c` and runs it on a device created without any of the abort extensions. The shader runs a case that does nothing, because a stripped abort is undefined behavior if it is reached |
| `reflect` | Prints every abort in the shader and where its message values are, from the SPIR-V (`shader_abort_reflect.c`). Doesn't use the GPU |

## Changing the shaders

After changing a shader, rebuild the `.spv` files. From the `sample/shaders` directory:

```
# GLSL (glslang built with GL_EXT_abort support)
glslang -V --target-env vulkan1.2 -I../.. samples.comp -o samples_glsl.spv

# Slang
slangc samples.slang -I ../.. -target spirv -profile spirv_1_5 -entry main -stage compute -o samples_slang.spv
```
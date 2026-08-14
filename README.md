# VK_KHR_shader_abort stripper

While `VK_KHR_shader_abort` is awesome, the main issue is how to ship code with it on older drivers that don't support `VK_KHR_shader_abort`

The `SPV_KHR_abort` exposes a new `OpAbortKHR` instruction that a driver's compiler stack might crash as it doesn't know what to do with it.

This library (file) is a very simple "grab and put in your own code" and will strip out the `SPV_KHR_abort` for you

# How to use

1. Copy `shader_abort_strip.c`/`.h` to your code
2. Add some code

```c
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

## What is main.c

That is just a way to test this as a standalone binary

```bash
# Linux
gcc main.c shader_abort_strip.c -o strip_abort
clang main.c shader_abort_strip.c -o strip_abort

# Windows
cl main.c shader_abort_strip.c /Fe:strip_abort.exe
```
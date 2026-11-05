// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

// The setup parts of sample.c: command line, loading the .spv, and choosing a device with the right extensions and
// features. sample.c is left with what happens after that: the dispatch and reading the messages.
// Header only, included by sample.c alone.
#ifndef SAMPLES_UTIL_H
#define SAMPLES_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

#ifndef SAMPLES_SPV_DIR
#define SAMPLES_SPV_DIR "shaders"
#endif

#define VK_CHECK(call)                                                                                       \
    do {                                                                                                     \
        VkResult result_ = (call);                                                                           \
        if (result_ != VK_SUCCESS) {                                                                         \
            fprintf(stderr, "%s:%d: %s failed with VkResult %d\n", __FILE__, __LINE__, #call, (int)result_); \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)

typedef struct SampleInfo {
    const char* name;
    const char* description;
} SampleInfo;

typedef struct Options {
    size_t sample;  // into the SampleInfo list, the first one is the default
    const char* lang;
    const char* spv_dir;
    int device_index;  // -1 picks the first one that supports VK_KHR_shader_abort
    int reflect;       // set by sample.c from the chosen sample
    int strip;         // set by sample.c from the chosen sample
} Options;

static void print_usage(const char* program, const SampleInfo* pSamples, size_t sampleCount) {
    printf("usage: %s [sample] [options]\n\nsamples:\n", program);
    for (size_t i = 0; i < sampleCount; ++i) {
        printf("  %-10s %s%s\n", pSamples[i].name, pSamples[i].description, i == 0 ? " (default)" : "");
    }
    printf(
        "\noptions:\n"
        "  --lang glsl|slang   which compiled shader to use (default glsl)\n"
        "  --device N          use physical device N\n"
        "  --spv-dir DIR       where samples_glsl.spv and samples_slang.spv are (default %s)\n"
        "  --help              this\n"
        "\nThe samples abort a shader on purpose, which loses the VkDevice.\n",
        SAMPLES_SPV_DIR);
}

static int parse_options(int argc, char** argv, const SampleInfo* pSamples, size_t sampleCount, Options* pOptions) {
    pOptions->sample = 0;
    pOptions->lang = "glsl";
    pOptions->spv_dir = SAMPLES_SPV_DIR;
    pOptions->device_index = -1;
    pOptions->reflect = 0;
    pOptions->strip = 0;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const char* next = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(argv[0], pSamples, sampleCount);
            exit(0);

        } else if (next != NULL && strcmp(arg, "--lang") == 0) {
            pOptions->lang = next, i++;
        } else if (next != NULL && strcmp(arg, "--device") == 0) {
            pOptions->device_index = atoi(next), i++;
        } else if (next != NULL && strcmp(arg, "--spv-dir") == 0) {
            pOptions->spv_dir = next, i++;
        } else if (arg[0] != '-') {
            size_t s = 0;
            while (s < sampleCount && strcmp(pSamples[s].name, arg) != 0) s++;
            if (s == sampleCount) {
                fprintf(stderr, "unknown sample '%s'\n\n", arg);
                return 0;
            }
            pOptions->sample = s;
        } else {
            fprintf(stderr, "unknown or incomplete option '%s'\n\n", arg);
            return 0;
        }
    }
    if (strcmp(pOptions->lang, "glsl") != 0 && strcmp(pOptions->lang, "slang") != 0) {
        fprintf(stderr, "--lang must be glsl or slang\n\n");
        return 0;
    }
    return 1;
}

static uint32_t* load_spirv(const Options* pOptions, size_t* pSize) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/samples_%s.spv", pOptions->spv_dir, pOptions->lang);
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "can't open %s (use --spv-dir to say where the .spv files are)\n", path);
        exit(1);
    }
    fseek(fp, 0, SEEK_END);
    const long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint32_t* pCode = (size >= 20 && size % 4 == 0) ? (uint32_t*)malloc((size_t)size) : NULL;
    if (pCode == NULL || fread(pCode, 1, (size_t)size, fp) != (size_t)size) {
        fprintf(stderr, "%s is not a SPIR-V file\n", path);
        exit(1);
    }
    fclose(fp);
    *pSize = (size_t)size;
    return pCode;
}

static int has_capability(const uint32_t* pCode, size_t codeSize, uint32_t capability) {
    const uint32_t kOpCapability = 17;
    for (size_t i = 5; i < codeSize / 4 && (pCode[i] & 0xFFFF) == kOpCapability; i += pCode[i] >> 16) {
        if (pCode[i + 1] == capability) return 1;
    }
    return 0;
}

typedef struct Features {
    VkPhysicalDeviceFeatures2 features2;
    VkPhysicalDeviceVulkan12Features vulkan12;  // shaderInt8
    VkPhysicalDeviceShaderAbortFeaturesKHR shader_abort;
    VkPhysicalDeviceShaderConstantDataFeaturesKHR constant_data;
    VkPhysicalDeviceFaultFeaturesKHR device_fault;
} Features;

static void chain_features(Features* pFeatures) {
    memset(pFeatures, 0, sizeof(*pFeatures));
    pFeatures->features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    pFeatures->vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    pFeatures->shader_abort.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ABORT_FEATURES_KHR;
    pFeatures->constant_data.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CONSTANT_DATA_FEATURES_KHR;
    pFeatures->device_fault.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_KHR;
    pFeatures->features2.pNext = &pFeatures->vulkan12;
    pFeatures->vulkan12.pNext = &pFeatures->shader_abort;
    pFeatures->shader_abort.pNext = &pFeatures->constant_data;
    pFeatures->constant_data.pNext = &pFeatures->device_fault;
}

static int has_extension(VkPhysicalDevice gpu, const char* pName) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, NULL);
    VkExtensionProperties* pProps = (VkExtensionProperties*)calloc(count ? count : 1, sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(gpu, NULL, &count, pProps);
    int found = 0;
    for (uint32_t i = 0; i < count && !found; ++i) found = strcmp(pProps[i].extensionName, pName) == 0;
    free(pProps);
    return found;
}

static const char* const kDeviceExtensions[] = {
    VK_KHR_SHADER_ABORT_EXTENSION_NAME,          // OpAbortKHR
    VK_KHR_SHADER_CONSTANT_DATA_EXTENSION_NAME,  // required by VK_KHR_shader_abort, used for the strings by glslang
    VK_KHR_DEVICE_FAULT_EXTENSION_NAME,          // required by VK_KHR_shader_abort, how the message gets back
};

// Returns NULL if |gpu| can run the shader and fills in pEnable, else returns what is missing.
// Only what the shader declares is turned on, as a real application would.
static const char* enable_features(VkPhysicalDevice gpu, const uint32_t* pCode, size_t codeSize, int stripped,
                                   Features* pEnable) {
    const uint32_t kCapabilityInt8 = 39;
    const uint32_t kCapabilityConstantDataKHR = 5146;
    const int needs_int8 = has_capability(pCode, codeSize, kCapabilityInt8);  // glslang's strings are int8 arrays
    const int needs_constant_data = has_capability(pCode, codeSize, kCapabilityConstantDataKHR);  // glslang only

    Features supported;
    chain_features(&supported);
    if (stripped) {
        // A stripped shader needs none of the abort extensions. glslang's strings are still in it though (as unused
        // int8 arrays), so it can still need shaderInt8.
        supported.vulkan12.pNext = NULL;
    } else {
        for (size_t i = 0; i < sizeof(kDeviceExtensions) / sizeof(kDeviceExtensions[0]); ++i) {
            if (!has_extension(gpu, kDeviceExtensions[i])) return kDeviceExtensions[i];
        }
    }
    vkGetPhysicalDeviceFeatures2(gpu, &supported.features2);

    if (!stripped && !supported.shader_abort.shaderAbort) return "shaderAbort";
    if (!stripped && !supported.device_fault.deviceFault) return "deviceFault";
    if (needs_constant_data && !supported.constant_data.shaderConstantData) return "shaderConstantData";
    if (needs_int8 && !supported.vulkan12.shaderInt8) return "shaderInt8";

    chain_features(pEnable);
    pEnable->vulkan12.pNext = stripped ? NULL : pEnable->vulkan12.pNext;
    pEnable->vulkan12.shaderInt8 = needs_int8 ? VK_TRUE : VK_FALSE;
    pEnable->shader_abort.shaderAbort = stripped ? VK_FALSE : VK_TRUE;
    pEnable->device_fault.deviceFault = stripped ? VK_FALSE : VK_TRUE;
    pEnable->constant_data.shaderConstantData = needs_constant_data ? VK_TRUE : VK_FALSE;
    return NULL;
}

static VkPhysicalDevice pick_device(VkInstance instance, const Options* pOptions, const uint32_t* pCode,
                                    size_t codeSize, Features* pEnable) {
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    VkPhysicalDevice gpus[16];
    if (count > 16) count = 16;
    VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, gpus));

    for (uint32_t i = 0; i < count; ++i) {
        if (pOptions->device_index >= 0 && (uint32_t)pOptions->device_index != i) continue;
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpus[i], &props);
        if (props.apiVersion < VK_API_VERSION_1_2) {
            printf("device %u (%s): needs Vulkan 1.2\n", i, props.deviceName);
            continue;
        }
        const char* pMissing = enable_features(gpus[i], pCode, codeSize, pOptions->strip, pEnable);
        if (pMissing != NULL) {
            printf("device %u (%s): no %s\n", i, props.deviceName, pMissing);
            continue;
        }
        printf("using device %u: %s\n", i, props.deviceName);
        return gpus[i];
    }
    return VK_NULL_HANDLE;
}

#endif  // SAMPLES_UTIL_H

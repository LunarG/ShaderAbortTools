// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>

#include "shader_abort_message.h"
#include "shader_abort_reflect.h"
#include "shader_abort_strip.h"
#include "util.h"

typedef enum Sample {
    SAMPLE_BASIC = 0,
    SAMPLE_FORMAT = 1,
    SAMPLE_TYPES = 2,
    SAMPLE_MULTIPLE = 3,
    SAMPLE_EACH = 4,
    SAMPLE_ASSERT = 5,
    SAMPLE_STRIP = 6,
    SAMPLE_REFLECT = 7,
} Sample;

static const SampleInfo kSamples[] = {
    {"basic", "a message with no arguments, read with shader_abort_message_text()"},
    {"format", "a printf style message, with arguments from push constants"},
    {"types", "vectors, signed, hex, width and precision"},
    {"multiple", "4 invocations abort, prints every message the device reports"},
    {"each", "the 'multiple' shader, read with shader_abort_for_each_message()"},
    {"assert", "the SHADER_ASSERT macros from shader_abort.glsl / .slang"},
    {"strip", "strip the aborts out of the shader, then run it on a device without VK_KHR_shader_abort"},
    {"reflect", "print where every abort's message values are, from the SPIR-V, without using the GPU"},
};
static const size_t kSampleCount = sizeof(kSamples) / sizeof(kSamples[0]);

// Callback for shader_abort_for_each_message()
static void custom_callback(const char* pText, void* pUserData) {
    size_t* pCount = (size_t*)pUserData;
    (*pCount)++;
    printf("shader abort #%zu: %s\n", *pCount, pText);
}

// Reading the messages back (shader_abort_message.h)
static void print_messages(VkDevice device, Sample sample) {
    PFN_vkGetDeviceFaultDebugInfoKHR pfnGetDeviceFaultDebugInfoKHR =
        (PFN_vkGetDeviceFaultDebugInfoKHR)vkGetDeviceProcAddr(device, "vkGetDeviceFaultDebugInfoKHR");
    if (pfnGetDeviceFaultDebugInfoKHR == NULL) {
        fprintf(stderr, "vkGetDeviceFaultDebugInfoKHR is not available\n");
        return;
    }

    VkDeviceFaultShaderAbortMessageInfoKHR abort_info;
    memset(&abort_info, 0, sizeof(abort_info));
    abort_info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_SHADER_ABORT_MESSAGE_INFO_KHR;

    VkDeviceFaultDebugInfoKHR debug_info;
    memset(&debug_info, 0, sizeof(debug_info));
    debug_info.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_DEBUG_INFO_KHR;
    debug_info.pNext = &abort_info;

    // 1st call: pMessageData is NULL, so this only fills in messageDataSize
    VK_CHECK(pfnGetDeviceFaultDebugInfoKHR(device, &debug_info));
    if (abort_info.messageDataSize == 0) {
        printf("the device was lost, but not by a shader abort (no message)\n");
        return;
    }

    // 2nd call: get the data
    // (apps will want to handle allocation here themselves)
    abort_info.pMessageData = malloc((size_t)abort_info.messageDataSize);
    VK_CHECK(pfnGetDeviceFaultDebugInfoKHR(device, &debug_info));

    if (sample == SAMPLE_BASIC) {
        // A message that is just a string needs no formatting, this points straight into pMessageData
        ShaderAbortMessage message;
        if (shader_abort_first_message(abort_info.pMessageData, abort_info.messageDataSize, &message)) {
            const char* pText = shader_abort_message_text(&message);
            printf("shader abort: %s\n", pText ? pText : "(the first message has arguments)");
        }

    } else if (sample == SAMPLE_EACH) {
        size_t count = 0;
        shader_abort_for_each_message(abort_info.pMessageData, abort_info.messageDataSize, custom_callback, &count);
        printf("(%zu message%s reported)\n", count, count == 1 ? "" : "s");

    } else {
        // Walk every (size, payload) pair. A
        // device has to report the first abort, and may report more.
        uint64_t offset = 0;
        ShaderAbortMessage message;
        ShaderAbortMessage previous = {0, NULL};
        size_t count = 0;
        // An assert usually fails in many invocations at once,
        // so a message with the same bytes as the one before it is only counted.
        size_t repeats = 0;
        while (shader_abort_next_message(abort_info.pMessageData, abort_info.messageDataSize, &offset, &message)) {
            count++;
            if (message.size == previous.size && memcmp(message.pPayload, previous.pPayload, message.size) == 0) {
                repeats++;
                continue;
            }
            if (repeats > 0) {
                printf("  (repeated %zu more times)\n", repeats);
            }
            repeats = 0;
            previous = message;

            // Like snprintf() get the length first, so nothing is cut off
            const size_t length = shader_abort_format_message(&message, NULL, 0);
            char* pText = (char*)malloc(length + 1);
            shader_abort_format_message(&message, pText, length + 1);
            printf("shader abort: %s\n", pText);
            free(pText);
        }

        if (repeats > 0) {
            printf("  (repeated %zu more times)\n", repeats);
        }
        printf("(%zu message%s reported)\n", count, count == 1 ? "" : "s");
    }

    free(abort_info.pMessageData);
}

static int run_sample(const Options* pOptions, const uint32_t* pCode, size_t codeSize) {
    VkApplicationInfo app_info;
    memset(&app_info, 0, sizeof(app_info));
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "sample";
    app_info.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo instance_ci;
    memset(&instance_ci, 0, sizeof(instance_ci));
    instance_ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_ci.pApplicationInfo = &app_info;
    VkInstance instance;
    VK_CHECK(vkCreateInstance(&instance_ci, NULL, &instance));

    Features enable;
    VkPhysicalDevice gpu = pick_device(instance, pOptions, pCode, codeSize, &enable);
    if (gpu == VK_NULL_HANDLE) {
        printf("no device supports VK_KHR_shader_abort with what this shader needs\n");
        vkDestroyInstance(instance, NULL);
        return 1;
    }

    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &family_count, NULL);
    VkQueueFamilyProperties families[32];  // safe assumption
    if (family_count > 32) {
        family_count = 32;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &family_count, families);
    uint32_t family = 0;
    while (family < family_count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
        family++;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_ci;
    memset(&queue_ci, 0, sizeof(queue_ci));
    queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_ci.queueFamilyIndex = family;
    queue_ci.queueCount = 1;
    queue_ci.pQueuePriorities = &priority;

    VkDeviceCreateInfo device_ci;
    memset(&device_ci, 0, sizeof(device_ci));
    device_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_ci.pNext = &enable.features2;  // the features from enable_features()
    device_ci.queueCreateInfoCount = 1;
    device_ci.pQueueCreateInfos = &queue_ci;
    if (!pOptions->strip) {
        // Stripping, so don't need any of the extensions now
        device_ci.enabledExtensionCount = (uint32_t)(sizeof(kDeviceExtensions) / sizeof(kDeviceExtensions[0]));
        device_ci.ppEnabledExtensionNames = kDeviceExtensions;
    }
    VkDevice device;
    VK_CHECK(vkCreateDevice(gpu, &device_ci, NULL, &device));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);

    // Data to have dynamic input into the shader
    typedef struct PushConstants {
        uint32_t sample_id;
        uint32_t index;
        uint32_t count;
        float value;
    } PushConstants;

    VkPushConstantRange push_range = {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo layout_ci;
    memset(&layout_ci, 0, sizeof(layout_ci));
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.pushConstantRangeCount = 1;
    layout_ci.pPushConstantRanges = &push_range;
    VkPipelineLayout pipeline_layout;
    VK_CHECK(vkCreatePipelineLayout(device, &layout_ci, NULL, &pipeline_layout));

    VkShaderModuleCreateInfo module_ci;
    memset(&module_ci, 0, sizeof(module_ci));
    module_ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_ci.codeSize = codeSize;
    module_ci.pCode = pCode;
    VkShaderModule shader_module;
    VK_CHECK(vkCreateShaderModule(device, &module_ci, NULL, &shader_module));

    VkComputePipelineCreateInfo pipeline_ci;
    memset(&pipeline_ci, 0, sizeof(pipeline_ci));
    pipeline_ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_ci.stage.module = shader_module;
    pipeline_ci.stage.pName = "main";
    pipeline_ci.layout = pipeline_layout;
    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, NULL, &pipeline));

    VkCommandPoolCreateInfo pool_ci;
    memset(&pool_ci, 0, sizeof(pool_ci));
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.queueFamilyIndex = family;
    VkCommandPool command_pool;
    VK_CHECK(vkCreateCommandPool(device, &pool_ci, NULL, &command_pool));

    VkCommandBufferAllocateInfo cb_ai;
    memset(&cb_ai, 0, sizeof(cb_ai));
    cb_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cb_ai.commandPool = command_pool;
    cb_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cb_ai.commandBufferCount = 1;
    VkCommandBuffer cb;
    VK_CHECK(vkAllocateCommandBuffers(device, &cb_ai, &cb));

    VkCommandBufferBeginInfo begin_info;
    memset(&begin_info, 0, sizeof(begin_info));
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cb, &begin_info));
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    // Hard coded push constant data
    PushConstants push = {(uint32_t)pOptions->sample, 7, 4, 1.5f};
    vkCmdPushConstants(cb, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &push);
    vkCmdDispatch(cb, 1, 1, 1);
    VK_CHECK(vkEndCommandBuffer(cb));

    VkFenceCreateInfo fence_ci;
    memset(&fence_ci, 0, sizeof(fence_ci));
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    VK_CHECK(vkCreateFence(device, &fence_ci, NULL, &fence));

    VkSubmitInfo submit;
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;

    printf("about to call vkQueueSubmit: device lost is %sexpected...\n", pOptions->strip ? "not " : "");

    // After an abort, any call can be the one that returns VK_ERROR_DEVICE_LOST
    VkResult result = vkQueueSubmit(queue, 1, &submit, fence);
    if (result == VK_SUCCESS) {
        result = vkWaitForFences(device, 1, &fence, VK_TRUE, 10ull * 1000 * 1000 * 1000);  // 10 seconds
    }

    int exit_code = 0;
    if (result == VK_ERROR_DEVICE_LOST) {
        printf("VK_ERROR_DEVICE_LOST\n");
        print_messages(device, (Sample)pOptions->sample);
    } else if (result == VK_SUCCESS) {
        printf("the shader finished without aborting\n");
    } else if (result == VK_TIMEOUT) {
        printf("the dispatch did not finish within 10 seconds\n");
        exit_code = 1;
    } else {
        printf("unexpected VkResult %d\n", (int)result);
        exit_code = 1;
    }

    // Destroying objects is still allowed (and needed) after the device is lost.
    // A real application would now recreate the VkDevice and everything made from it.
    vkDestroyFence(device, fence, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyPipeline(device, pipeline, NULL);
    vkDestroyShaderModule(device, shader_module, NULL);
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return exit_code;
}

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, kSamples, kSampleCount, &options)) {
        print_usage(argv[0], kSamples, kSampleCount);
        return 1;
    }

    options.reflect = options.sample == SAMPLE_REFLECT;
    options.strip = options.sample == SAMPLE_STRIP;

    size_t code_size = 0;
    uint32_t* pCode = load_spirv(&options, &code_size);

    // shader_abort_strip.h
    if (options.strip && has_shader_abort(pCode, code_size)) {
        // What an application does when the device doesn't support VK_KHR_shader_abort (shader_abort_strip.h).
        // The output is never bigger than the input.
        uint32_t* pStripped = (uint32_t*)malloc(code_size);
        size_t stripped_size = 0;
        if (pStripped == NULL || strip_abort_extensions(pCode, code_size, pStripped, &stripped_size) != 0) {
            fprintf(stderr, "failed to strip the shader\n");
            return 1;
        }
        printf("stripped the aborts out of the shader (%zu bytes -> %zu bytes)\n", code_size, stripped_size);
        free(pCode);
        pCode = pStripped;
        code_size = stripped_size;
    }

    int exit_code = 0;
    if (options.reflect) {
        // shader_abort_reflect.h
        //
        // shader_abort_reflect_print() is the built in "printf" of the reflection. To print it your own way, or use the
        // offsets in a tool, call shader_abort_reflect_create() and walk pAborts/pMembers, the same way it does.
        exit_code = shader_abort_reflect_print(pCode, code_size);
    } else {
        exit_code = run_sample(&options, pCode, code_size);
    }

    free(pCode);
    return exit_code;
}

// SPDX-FileCopyrightText: 2026 Spencer Fricke spencer@lunarg.com
// SPDX-License-Identifier: MIT

// Compiled into samples_slang.spv by the commands in sample/README.md ("Changing the shaders")

// The SHADER_ASSERT macros
#include "shader_abort.slang"

struct PushConstants {
    uint sample_id;  // which sample to run, matches the Sample enum in sample.c
    uint index;      // constants in sample.c
    uint count;
    float value;
};
[[vk::push_constant]] PushConstants pc;

static const uint SAMPLE_BASIC = 0;
static const uint SAMPLE_FORMAT = 1;
static const uint SAMPLE_TYPES = 2;
static const uint SAMPLE_MULTIPLE = 3;
static const uint SAMPLE_EACH = 4;
static const uint SAMPLE_ASSERT = 5;
static const uint SAMPLE_STRIP = 6;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint invocation : SV_GroupIndex)
{
    if (pc.sample_id == SAMPLE_BASIC) {
        // The simplest abort there is, a message with no arguments
        if (invocation == 0) {
            abort("Hello from the basic sample!");
        }

    } else if (pc.sample_id == SAMPLE_FORMAT) {
        // Arguments work like printf
        if (invocation == 0) {
            abort("index %u is out of range (count %u), value was %f", pc.index, pc.count, pc.value);
        }

    } else if (pc.sample_id == SAMPLE_TYPES) {
        // More of the format specifiers (vectors, signed, hex with flags/width, precision, etc)
        if (invocation == 0) {
            abort("vector %v3u, negative %d, hex 0x%08x, rounded %.2f", uint3(pc.index, pc.count, invocation),
                  -int(pc.count), pc.index, pc.value);
        }

    } else if (pc.sample_id == SAMPLE_MULTIPLE || pc.sample_id == SAMPLE_EACH) {
        // 4 invocations abort (0, 16, 32, 48).
        // Only the first abort has to be reported, a device can report more.
        if ((invocation % 16) == 0) {
            abort("invocation %u aborted", invocation);
        }

    } else if (pc.sample_id == SAMPLE_ASSERT) {
        // The macros from shader_abort.slang.
        SHADER_ASSERT_BOUNDS(pc.index, pc.count);
        SHADER_ASSERT(pc.value >= 0.0, "value must not be negative, it was %f", pc.value);

    } else if (pc.sample_id == SAMPLE_STRIP) {
        // Does nothing.
        // The 'strip' sample runs this after stripping the aborts out of the shader.
    }
}

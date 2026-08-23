/* kernels_regret_opencl.c - OpenCL ragged regret primitives (GPU-06). */

#include "kernels_regret_opencl.h"

/* Kept as a separately linkable source fragment so the OpenCL adapter can
 * append it to its existing program without duplicating the kernel text. */
static const char pe_regret_opencl_source[] =
"__kernel void pe_strategy_batch_kernel(\n"
"    __global const float *regrets, __global const uint *offsets,\n"
"    __global const ushort *action_counts, __global float *strategies,\n"
"    uint infoset_count)\n"
"{\n"
"    uint infoset = get_global_id(0);\n"
"    if (infoset >= infoset_count) return;\n"
"    uint begin = offsets[infoset];\n"
"    uint end = offsets[infoset + 1];\n"
"    ushort actions = action_counts[infoset];\n"
"    if (actions == 0 || begin + (uint)actions > end) return;\n"
"    float positive = 0.0f;\n"
"    for (ushort action = 0; action < actions; ++action) {\n"
"        float regret = regrets[begin + action];\n"
"        if (regret > 0.0f) positive += regret;\n"
"    }\n"
"    for (ushort action = 0; action < actions; ++action) {\n"
"        float regret = regrets[begin + action];\n"
"        strategies[begin + action] = positive > 0.0f\n"
"            ? (regret > 0.0f ? regret / positive : 0.0f)\n"
"            : 1.0f / (float)actions;\n"
"    }\n"
"    for (uint slot = begin + actions; slot < end; ++slot)\n"
"        strategies[slot] = 0.0f;\n"
"}\n"
"\n"
"__kernel void pe_apply_update_batch_kernel(\n"
"    __global float *regrets, __global float *averages,\n"
"    __global const uint *slots, __global const float *regret_deltas,\n"
"    __global const float *average_deltas, uint update_count)\n"
"{\n"
"    uint update = get_global_id(0);\n"
"    if (update >= update_count) return;\n"
"    uint slot = slots[update];\n"
"    /* Updates are reduced by slot before launch; one work item owns each\n"
"       reduced slot, so float atomics are unnecessary here. */\n"
"    regrets[slot] += regret_deltas[update];\n"
"    averages[slot] += average_deltas[update];\n"
"}\n";

const char *pe_regret_opencl_kernel_source(void)
{
    return pe_regret_opencl_source;
}

/*
 * pe_regret_metal_source.h - the Metal shading-language source, as a string.
 *
 * Compiled at context creation with newLibraryWithSource:. Shipping a
 * precompiled .metallib would save a few milliseconds once per process and
 * cost a build-time dependency on the Metal toolchain in every configuration
 * that touches this target; the trade is not worth it for two short kernels.
 *
 * The arithmetic is the same as common/pe_regret_kernels.inc. It cannot share
 * that file -- Metal is neither CUDA nor HIP -- so the parity test against
 * cpu_ref is what keeps the two honest.
 */

#ifndef POKER_EVAL_PE_REGRET_METAL_SOURCE_H
#define POKER_EVAL_PE_REGRET_METAL_SOURCE_H

static const char *const kPeRegretMetalSource =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"\n"
"kernel void pe_strategy_batch_kernel(\n"
"    device const float   *regrets       [[buffer(0)]],\n"
"    device const uint    *offsets       [[buffer(1)]],\n"
"    device const ushort  *action_counts [[buffer(2)]],\n"
"    device float         *strategies    [[buffer(3)]],\n"
"    constant uint        &infoset_count [[buffer(4)]],\n"
"    uint infoset [[thread_position_in_grid]])\n"
"{\n"
"    if (infoset >= infoset_count) return;\n"
"    uint begin = offsets[infoset];\n"
"    uint end = offsets[infoset + 1u];\n"
"    ushort actions = action_counts[infoset];\n"
"    if (actions == 0u || begin + (uint)actions > end) return;\n"
"    float positive = 0.0f;\n"
"    for (ushort action = 0u; action < actions; ++action) {\n"
"        float regret = regrets[begin + action];\n"
"        if (regret > 0.0f) positive += regret;\n"
"    }\n"
"    for (ushort action = 0u; action < actions; ++action) {\n"
"        float regret = regrets[begin + action];\n"
"        strategies[begin + action] = positive > 0.0f\n"
"            ? (regret > 0.0f ? regret / positive : 0.0f)\n"
"            : 1.0f / (float)actions;\n"
"    }\n"
"    for (uint slot = begin + actions; slot < end; ++slot)\n"
"        strategies[slot] = 0.0f;\n"
"}\n"
"\n"
"kernel void pe_apply_update_batch_kernel(\n"
"    device atomic_float  *regrets        [[buffer(0)]],\n"
"    device atomic_float  *averages       [[buffer(1)]],\n"
"    device const uint    *slots          [[buffer(2)]],\n"
"    device const float   *regret_deltas  [[buffer(3)]],\n"
"    device const float   *average_deltas [[buffer(4)]],\n"
"    constant uint        &update_count   [[buffer(5)]],\n"
"    uint update [[thread_position_in_grid]])\n"
"{\n"
"    if (update >= update_count) return;\n"
"    uint slot = slots[update];\n"
"    atomic_fetch_add_explicit(&regrets[slot], regret_deltas[update],\n"
"                              memory_order_relaxed);\n"
"    atomic_fetch_add_explicit(&averages[slot], average_deltas[update],\n"
"                              memory_order_relaxed);\n"
"}\n";

#endif /* POKER_EVAL_PE_REGRET_METAL_SOURCE_H */

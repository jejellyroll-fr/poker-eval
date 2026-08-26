/*
 * pe_regret_kernels.inc - the ragged strategy and regret-update primitives,
 * written once for every device runtime whose kernel language is CUDA-shaped.
 *
 * CUDA and HIP differ, for this code, only in the spelling of the runtime
 * calls and of the exported symbols. Keeping two copies meant two places to
 * fix the same bug and two chances for the backends to drift apart while both
 * still passed their own parity test. The translation unit that includes this
 * file supplies the spelling; the arithmetic lives here alone.
 *
 * The includer must define, before including:
 *
 *   PE_GPU_FN(name)      exported symbol, e.g. pe_regret_cuda_##name
 *   PE_GPU_KFN(name)     internal launcher symbol
 *   PE_GPU_CTX           the context struct tag
 *   PE_GPU_BATCH         the update-batch struct tag
 *   peGpu*               the runtime calls listed in pe_gpu_runtime.h
 *
 * This is an .inc rather than a header because it defines symbols: including
 * it twice in one binary is a link error, which is the intended protection.
 */

/* `offsets` has infoset_count + 1 entries. Each infoset owns the ragged span
 * [offsets[i], offsets[i + 1]) in the regret and strategy arrays. */
PE_GPU_GLOBAL static void pe_strategy_batch_kernel(
    const float *regrets, const uint32_t *offsets,
    const uint16_t *action_counts, float *strategies,
    uint32_t infoset_count)
{
    uint32_t infoset = blockIdx.x * blockDim.x + threadIdx.x;
    if (infoset >= infoset_count)
        return;

    uint32_t begin = offsets[infoset];
    uint32_t end = offsets[infoset + 1u];
    uint16_t actions = action_counts[infoset];
    float positive = 0.0f;
    if (actions == 0u || begin + (uint32_t)actions > end)
        return;

    for (uint16_t action = 0u; action < actions; ++action)
    {
        float regret = regrets[begin + action];
        if (regret > 0.0f)
            positive += regret;
    }
    for (uint16_t action = 0u; action < actions; ++action)
    {
        float regret = regrets[begin + action];
        strategies[begin + action] = positive > 0.0f
            ? (regret > 0.0f ? regret / positive : 0.0f)
            : 1.0f / (float)actions;
    }
    for (uint32_t slot = begin + actions; slot < end; ++slot)
        strategies[slot] = 0.0f;
}

/* Updates are expected to be reduced by logical slot before launch. Atomic
 * adds keep the primitive safe for callers that submit duplicate slots. */
PE_GPU_GLOBAL static void pe_apply_update_batch_kernel(
    float *regrets, float *averages, const uint32_t *slots,
    const float *regret_deltas, const float *average_deltas,
    uint32_t update_count)
{
    uint32_t update = blockIdx.x * blockDim.x + threadIdx.x;
    if (update >= update_count)
        return;
    uint32_t slot = slots[update];
    PE_GPU_ATOMIC_ADD(&regrets[slot], regret_deltas[update]);
    PE_GPU_ATOMIC_ADD(&averages[slot], average_deltas[update]);
}

struct PE_GPU_CTX
{
    peGpuStream_t stream;
};

extern "C" {

void PE_GPU_KFN(launch_strategy_batch)(
    const float *regrets, const uint32_t *offsets,
    const uint16_t *action_counts, float *strategies,
    uint32_t infoset_count, peGpuStream_t stream)
{
    const int threads = 256;
    if (infoset_count == 0u)
        return;
    const int blocks = (int)((infoset_count + (uint32_t)threads - 1u) /
                             (uint32_t)threads);
    PE_GPU_LAUNCH(pe_strategy_batch_kernel, blocks, threads, stream,
                  regrets, offsets, action_counts, strategies, infoset_count);
}

void PE_GPU_KFN(launch_apply_update_batch)(
    float *regrets, float *averages, const uint32_t *slots,
    const float *regret_deltas, const float *average_deltas,
    uint32_t update_count, peGpuStream_t stream)
{
    const int threads = 256;
    if (update_count == 0u)
        return;
    const int blocks = (int)((update_count + (uint32_t)threads - 1u) /
                             (uint32_t)threads);
    PE_GPU_LAUNCH(pe_apply_update_batch_kernel, blocks, threads, stream,
                  regrets, averages, slots, regret_deltas, average_deltas,
                  update_count);
}

PE_GPU_CTX *PE_GPU_FN(create)(void)
{
    PE_GPU_CTX *ctx = new (std::nothrow) PE_GPU_CTX;
    if (ctx == nullptr || peGpuStreamCreate(&ctx->stream) != peGpuSuccess)
    {
        delete ctx;
        return nullptr;
    }
    return ctx;
}

void PE_GPU_FN(destroy)(PE_GPU_CTX *ctx)
{
    if (ctx == nullptr)
        return;
    peGpuStreamDestroy(ctx->stream);
    delete ctx;
}

int PE_GPU_FN(strategy_batch)(PE_GPU_CTX *ctx,
                              const pe_infoset_batch_t *in,
                              pe_strategy_batch_t *out)
{
    float *device_regrets = nullptr;
    float *device_strategies = nullptr;
    uint32_t *device_offsets = nullptr;
    uint16_t *device_actions = nullptr;
    size_t total;
    peGpuError_t status;

    if (ctx == nullptr || in == nullptr || out == nullptr ||
        (in->count != 0u &&
         (in->offsets == nullptr || in->action_counts == nullptr ||
          in->regrets == nullptr)) ||
        (out->capacity != 0u && out->strategies == nullptr))
        return -1;
    if (in->count == 0u)
    {
        out->count = 0u;
        out->offsets = in->offsets;
        return 0;
    }
    total = in->offsets[in->count];
    if (out->capacity < total || out->strategies == nullptr)
        return -1;
    for (size_t i = 0u; i < in->count; ++i)
    {
        uint32_t begin = in->offsets[i];
        uint32_t end = in->offsets[i + 1u];
        if (end < begin || (uint32_t)in->action_counts[i] > end - begin)
            return -1;
        for (uint16_t action = 0u; action < in->action_counts[i]; ++action)
            if (!isfinite(in->regrets[begin + action]))
                return -1;
    }
    status = peGpuMalloc((void **)&device_regrets, total * sizeof(float));
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMalloc((void **)&device_strategies, total * sizeof(float));
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMalloc((void **)&device_offsets,
                         (in->count + 1u) * sizeof(uint32_t));
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMalloc((void **)&device_actions,
                         in->count * sizeof(uint16_t));
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(device_regrets, in->regrets,
                              total * sizeof(float), peGpuMemcpyHostToDevice,
                              ctx->stream);
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(device_offsets, in->offsets,
                              (in->count + 1u) * sizeof(uint32_t),
                              peGpuMemcpyHostToDevice, ctx->stream);
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(device_actions, in->action_counts,
                              in->count * sizeof(uint16_t),
                              peGpuMemcpyHostToDevice, ctx->stream);
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemsetAsync(device_strategies, 0, total * sizeof(float),
                              ctx->stream);
    if (status != peGpuSuccess)
        goto fail;
    PE_GPU_KFN(launch_strategy_batch)(device_regrets, device_offsets,
                                      device_actions, device_strategies,
                                      (uint32_t)in->count, ctx->stream);
    status = peGpuGetLastError();
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(out->strategies, device_strategies,
                              total * sizeof(float), peGpuMemcpyDeviceToHost,
                              ctx->stream);
    if (status != peGpuSuccess ||
        peGpuStreamSynchronize(ctx->stream) != peGpuSuccess)
        goto fail;
    out->count = in->count;
    out->offsets = in->offsets;
    peGpuFree(device_actions);
    peGpuFree(device_offsets);
    peGpuFree(device_strategies);
    peGpuFree(device_regrets);
    return 0;
fail:
    peGpuFree(device_actions);
    peGpuFree(device_offsets);
    peGpuFree(device_strategies);
    peGpuFree(device_regrets);
    return -1;
}

int PE_GPU_FN(apply_update_slots)(
    PE_GPU_CTX *ctx, float *regrets, float *averages,
    size_t value_count, const PE_GPU_BATCH *batch)
{
    float *device_regrets = nullptr;
    float *device_averages = nullptr;
    uint32_t *device_slots = nullptr;
    float *device_regret_deltas = nullptr;
    float *device_average_deltas = nullptr;
    peGpuError_t status;

    if (ctx == nullptr || regrets == nullptr || averages == nullptr ||
        batch == nullptr || (batch->count != 0u &&
                             (batch->slots == nullptr ||
                              batch->regret_deltas == nullptr ||
                              batch->average_deltas == nullptr)) ||
        batch->count > UINT32_MAX)
        return -1;
    for (size_t i = 0u; i < batch->count; ++i)
    {
        uint32_t slot = batch->slots[i];
        if (slot >= value_count || !isfinite(regrets[slot]) ||
            !isfinite(averages[slot]) ||
            !isfinite(batch->regret_deltas[i]) ||
            !isfinite(batch->average_deltas[i]))
            return -1;
    }
    if (batch->count == 0u)
        return 0;
    status = peGpuMalloc((void **)&device_regrets, value_count * sizeof(float));
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMalloc((void **)&device_averages,
                         value_count * sizeof(float));
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMalloc((void **)&device_slots,
                         batch->count * sizeof(uint32_t));
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMalloc((void **)&device_regret_deltas,
                         batch->count * sizeof(float));
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMalloc((void **)&device_average_deltas,
                         batch->count * sizeof(float));
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(device_regrets, regrets,
                              value_count * sizeof(float),
                              peGpuMemcpyHostToDevice, ctx->stream);
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(device_averages, averages,
                              value_count * sizeof(float),
                              peGpuMemcpyHostToDevice, ctx->stream);
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(device_slots, batch->slots,
                              batch->count * sizeof(uint32_t),
                              peGpuMemcpyHostToDevice, ctx->stream);
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(device_regret_deltas, batch->regret_deltas,
                              batch->count * sizeof(float),
                              peGpuMemcpyHostToDevice, ctx->stream);
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(device_average_deltas, batch->average_deltas,
                              batch->count * sizeof(float),
                              peGpuMemcpyHostToDevice, ctx->stream);
    if (status != peGpuSuccess)
        goto fail;
    PE_GPU_KFN(launch_apply_update_batch)(
        device_regrets, device_averages, device_slots, device_regret_deltas,
        device_average_deltas, (uint32_t)batch->count, ctx->stream);
    status = peGpuGetLastError();
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(regrets, device_regrets,
                              value_count * sizeof(float),
                              peGpuMemcpyDeviceToHost, ctx->stream);
    if (status != peGpuSuccess)
        goto fail;
    status = peGpuMemcpyAsync(averages, device_averages,
                              value_count * sizeof(float),
                              peGpuMemcpyDeviceToHost, ctx->stream);
    if (status != peGpuSuccess ||
        peGpuStreamSynchronize(ctx->stream) != peGpuSuccess)
        goto fail;
    peGpuFree(device_average_deltas);
    peGpuFree(device_regret_deltas);
    peGpuFree(device_slots);
    peGpuFree(device_averages);
    peGpuFree(device_regrets);
    return 0;
fail:
    peGpuFree(device_average_deltas);
    peGpuFree(device_regret_deltas);
    peGpuFree(device_slots);
    peGpuFree(device_averages);
    peGpuFree(device_regrets);
    return -1;
}

int PE_GPU_FN(sync)(PE_GPU_CTX *ctx)
{
    return ctx == nullptr ||
                   peGpuStreamSynchronize(ctx->stream) != peGpuSuccess
               ? -1
               : 0;
}

}

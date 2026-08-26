/*
 * pe_regret_metal.m - the whole Objective-C surface of the Metal backend.
 *
 * Everything above this file is C and sees only pe_regret_metal.h. The point
 * of keeping the bridge this small is that the solver never learns what a
 * MTLDevice is.
 *
 * Unified memory is the reason this backend is worth having on Apple silicon
 * rather than treating the GPU as a PCIe device. Buffers are created with
 * newBufferWithBytesNoCopy: over the caller's own pages where alignment
 * allows, so the regret arrays are not copied to a device and back -- the GPU
 * reads and writes the host allocation directly. Where the pointer is not page
 * aligned, which the C allocator gives no way to require, a shared-storage
 * buffer is used instead and the copy is a memcpy within one memory system,
 * not a transfer across a bus.
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <poker_eval/gpu/pe_regret_metal.h>
#include "pe_regret_metal_source.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Held as CFTypeRef, which is what CFBridgingRetain returns and what
   CFRelease takes, so the ownership transfer needs no cast in either
   direction. The struct is opaque to every caller. */
struct pe_regret_metal_context_t
{
    CFTypeRef device;    /* id<MTLDevice>                */
    CFTypeRef queue;     /* id<MTLCommandQueue>          */
    CFTypeRef strategy;  /* id<MTLComputePipelineState>  */
    CFTypeRef update;    /* id<MTLComputePipelineState>  */
    size_t page_size;
};

/* A buffer over caller memory when the pointer is page aligned, a shared copy
   otherwise. `owned` says which, so the result knows whether to copy back. */
typedef struct
{
    id<MTLBuffer> buffer;
    int owned;
    void *host;
    size_t length;
} pe_metal_span_t;

static pe_metal_span_t pe_metal_wrap(id<MTLDevice> device, void *host,
                                     size_t length, size_t page_size)
{
    pe_metal_span_t span = {nil, 0, host, length};

    if (length == 0u)
        return span;
    if (((uintptr_t)host % page_size) == 0u)
    {
        size_t rounded = ((length + page_size - 1u) / page_size) * page_size;
        span.buffer = [device newBufferWithBytesNoCopy:host
                                                length:rounded
                                               options:MTLResourceStorageModeShared
                                           deallocator:nil];
        if (span.buffer != nil)
            return span;
    }
    span.buffer = [device newBufferWithBytes:host
                                      length:length
                                     options:MTLResourceStorageModeShared];
    span.owned = 1;
    return span;
}

static id<MTLBuffer> pe_metal_input(id<MTLDevice> device, const void *host,
                                    size_t length)
{
    if (length == 0u)
        return nil;
    return [device newBufferWithBytes:host
                               length:length
                              options:MTLResourceStorageModeShared];
}

static void pe_metal_writeback(pe_metal_span_t *span)
{
    if (span->owned && span->buffer != nil && span->length != 0u)
        memcpy(span->host, [span->buffer contents], span->length);
}

pe_regret_metal_context_t *pe_regret_metal_create(void)
{
    pe_regret_metal_context_t *ctx;
    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue;
        id<MTLLibrary> library;
        NSError *error = nil;
        id<MTLComputePipelineState> strategy;
        id<MTLComputePipelineState> update;

        if (device == nil)
            return NULL;
        queue = [device newCommandQueue];
        if (queue == nil)
            return NULL;
        library = [device
            newLibraryWithSource:[NSString stringWithUTF8String:
                                      kPeRegretMetalSource]
                         options:nil
                           error:&error];
        if (library == nil)
            return NULL;
        strategy = [device newComputePipelineStateWithFunction:
            [library newFunctionWithName:@"pe_strategy_batch_kernel"]
                                                         error:&error];
        update = [device newComputePipelineStateWithFunction:
            [library newFunctionWithName:@"pe_apply_update_batch_kernel"]
                                                       error:&error];
        if (strategy == nil || update == nil)
            return NULL;
        ctx = (pe_regret_metal_context_t *)calloc(1u, sizeof(*ctx));
        if (ctx == NULL)
            return NULL;
        /* Each retain here is balanced by a CFRelease in destroy. */
        ctx->device = CFBridgingRetain(device);
        ctx->queue = CFBridgingRetain(queue);
        ctx->strategy = CFBridgingRetain(strategy);
        ctx->update = CFBridgingRetain(update);
        ctx->page_size = (size_t)getpagesize();
    }
    return ctx;
}

void pe_regret_metal_destroy(pe_regret_metal_context_t *ctx)
{
    if (ctx == NULL)
        return;
    if (ctx->update != NULL)
        CFRelease(ctx->update);
    if (ctx->strategy != NULL)
        CFRelease(ctx->strategy);
    if (ctx->queue != NULL)
        CFRelease(ctx->queue);
    if (ctx->device != NULL)
        CFRelease(ctx->device);
    free(ctx);
}

/* One dispatch, sized so that a short batch still gets a full grid. */
static void pe_metal_dispatch(id<MTLComputeCommandEncoder> encoder,
                              id<MTLComputePipelineState> pipeline,
                              NSUInteger count)
{
    NSUInteger width = [pipeline maxTotalThreadsPerThreadgroup];
    if (width > count)
        width = count;
    if (width == 0u)
        width = 1u;
    [encoder dispatchThreads:MTLSizeMake(count, 1, 1)
       threadsPerThreadgroup:MTLSizeMake(width, 1, 1)];
}

int pe_regret_metal_strategy_batch(pe_regret_metal_context_t *ctx,
                                   const pe_infoset_batch_t *in,
                                   pe_strategy_batch_t *out)
{
    size_t total;
    int rc = -1;

    if (ctx == NULL || in == NULL || out == NULL ||
        (in->count != 0u &&
         (in->offsets == NULL || in->action_counts == NULL ||
          in->regrets == NULL)) ||
        (out->capacity != 0u && out->strategies == NULL))
        return -1;
    if (in->count == 0u)
    {
        out->count = 0u;
        out->offsets = in->offsets;
        return 0;
    }
    total = in->offsets[in->count];
    if (out->capacity < total || out->strategies == NULL)
        return -1;
    /* Same validation as every other backend, so a batch refused on the CPU
       is refused here too rather than producing a device-specific answer. */
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

    @autoreleasepool
    {
        id<MTLDevice> device = (__bridge id<MTLDevice>)ctx->device;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)ctx->queue;
        id<MTLComputePipelineState> pipeline =
            (__bridge id<MTLComputePipelineState>)ctx->strategy;
        id<MTLBuffer> regrets = pe_metal_input(
            device, in->regrets, total * sizeof(float));
        id<MTLBuffer> offsets = pe_metal_input(
            device, in->offsets, (in->count + 1u) * sizeof(uint32_t));
        id<MTLBuffer> actions = pe_metal_input(
            device, in->action_counts, in->count * sizeof(uint16_t));
        pe_metal_span_t strategies = pe_metal_wrap(
            device, out->strategies, total * sizeof(float), ctx->page_size);
        uint32_t infoset_count = (uint32_t)in->count;
        id<MTLCommandBuffer> commands;
        id<MTLComputeCommandEncoder> encoder;

        if (regrets == nil || offsets == nil || actions == nil ||
            strategies.buffer == nil)
            return -1;
        memset([strategies.buffer contents], 0, total * sizeof(float));
        commands = [queue commandBuffer];
        encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:regrets offset:0 atIndex:0];
        [encoder setBuffer:offsets offset:0 atIndex:1];
        [encoder setBuffer:actions offset:0 atIndex:2];
        [encoder setBuffer:strategies.buffer offset:0 atIndex:3];
        [encoder setBytes:&infoset_count length:sizeof(infoset_count) atIndex:4];
        pe_metal_dispatch(encoder, pipeline, in->count);
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if ([commands status] == MTLCommandBufferStatusCompleted)
        {
            pe_metal_writeback(&strategies);
            out->count = in->count;
            out->offsets = in->offsets;
            rc = 0;
        }
    }
    return rc;
}

int pe_regret_metal_apply_update_slots(
    pe_regret_metal_context_t *ctx, float *regrets, float *averages,
    size_t value_count, const pe_regret_metal_update_batch_t *batch)
{
    int rc = -1;

    if (ctx == NULL || regrets == NULL || averages == NULL ||
        batch == NULL || (batch->count != 0u &&
                          (batch->slots == NULL ||
                           batch->regret_deltas == NULL ||
                           batch->average_deltas == NULL)) ||
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

    @autoreleasepool
    {
        id<MTLDevice> device = (__bridge id<MTLDevice>)ctx->device;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)ctx->queue;
        id<MTLComputePipelineState> pipeline =
            (__bridge id<MTLComputePipelineState>)ctx->update;
        pe_metal_span_t regret_span = pe_metal_wrap(
            device, regrets, value_count * sizeof(float), ctx->page_size);
        pe_metal_span_t average_span = pe_metal_wrap(
            device, averages, value_count * sizeof(float), ctx->page_size);
        id<MTLBuffer> slots = pe_metal_input(
            device, batch->slots, batch->count * sizeof(uint32_t));
        id<MTLBuffer> regret_deltas = pe_metal_input(
            device, batch->regret_deltas, batch->count * sizeof(float));
        id<MTLBuffer> average_deltas = pe_metal_input(
            device, batch->average_deltas, batch->count * sizeof(float));
        uint32_t update_count = (uint32_t)batch->count;
        id<MTLCommandBuffer> commands;
        id<MTLComputeCommandEncoder> encoder;

        if (regret_span.buffer == nil || average_span.buffer == nil ||
            slots == nil || regret_deltas == nil || average_deltas == nil)
            return -1;
        commands = [queue commandBuffer];
        encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBuffer:regret_span.buffer offset:0 atIndex:0];
        [encoder setBuffer:average_span.buffer offset:0 atIndex:1];
        [encoder setBuffer:slots offset:0 atIndex:2];
        [encoder setBuffer:regret_deltas offset:0 atIndex:3];
        [encoder setBuffer:average_deltas offset:0 atIndex:4];
        [encoder setBytes:&update_count length:sizeof(update_count) atIndex:5];
        pe_metal_dispatch(encoder, pipeline, batch->count);
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if ([commands status] == MTLCommandBufferStatusCompleted)
        {
            pe_metal_writeback(&regret_span);
            pe_metal_writeback(&average_span);
            rc = 0;
        }
    }
    return rc;
}

int pe_regret_metal_sync(pe_regret_metal_context_t *ctx)
{
    /* Every dispatch above already waits for its command buffer, so there is
       nothing outstanding to wait for here. */
    return ctx == NULL ? -1 : 0;
}

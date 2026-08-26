/*
 * pe_work_protocol.h - versioned binary framing for distributed work (DIST-03)
 */

#ifndef POKER_EVAL_PE_WORK_PROTOCOL_H
#define POKER_EVAL_PE_WORK_PROTOCOL_H

#include <poker_eval/solver/pe_work_unit.h>
#include <poker_eval/solver/pe_solver_config.h>
#include <poker_eval/solver/pe_runtime.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_WORK_PROTOCOL_VERSION 1u
#define PE_WORK_PROTOCOL_HEADER_SIZE 16u
#define PE_WORK_PROTOCOL_MAX_PAYLOAD (16u * 1024u * 1024u)
#define PE_WORK_PROTOCOL_RESULT_FIXED_SIZE 80u

typedef intptr_t pe_work_socket_t;
#define PE_WORK_SOCKET_INVALID ((pe_work_socket_t)-1)

typedef enum pe_work_message_type_t
{
    PE_WORK_MESSAGE_CAPABILITIES = 1,
    PE_WORK_MESSAGE_UNIT = 2,
    PE_WORK_MESSAGE_RESULT = 3
} pe_work_message_type_t;

typedef struct pe_work_result_t
{
    uint64_t public_state;
    uint64_t iteration_begin;
    uint64_t iteration_end;
    uint64_t iterations;
    uint64_t infosets_trained;
    pe_compute_kind_t backend;
    int constraints_satisfied;
    double exploitability;
    double worst_margin;
    double mean_margin;
    /* Optional backend-specific delta, borrowed when decoded. */
    const uint8_t *delta;
    size_t delta_size;
} pe_work_result_t;

/** Validate a result independently of its frame representation. */
int pe_work_result_validate(const pe_work_result_t *result);

/** Initialize/tear down the platform socket runtime (no-op on POSIX). */
int pe_work_transport_init(void);
void pe_work_transport_cleanup(void);

/** Send one already encoded frame, handling short writes. */
int pe_work_socket_send_frame(pe_work_socket_t socket,
                              const uint8_t *frame,
                              size_t frame_size);

/** Receive one frame into caller storage, handling short reads. */
int pe_work_socket_recv_frame(pe_work_socket_t socket,
                              uint8_t *buffer,
                              size_t capacity,
                              size_t *out_size);

/** Close a socket created by the caller. */
int pe_work_socket_close(pe_work_socket_t socket);

/** Send/receive the two control payloads with owned temporary buffers. */
int pe_work_socket_send_capabilities(
    pe_work_socket_t socket,
    const pe_runtime_capabilities_t *runtime);
int pe_work_socket_recv_capabilities(
    pe_work_socket_t socket,
    pe_runtime_capabilities_t *out);
int pe_work_socket_send_work_unit(pe_work_socket_t socket,
                                  const pe_work_unit_t *unit);
int pe_work_socket_recv_work_unit(pe_work_socket_t socket,
                                  pe_work_unit_t *out);

/**
 * Return the complete frame size, or zero when the payload is invalid or the
 * size calculation would overflow.
 */
size_t pe_work_frame_size(size_t payload_size);

/**
 * Encode one frame into caller-owned storage. The frame layout is:
 * magic[4], version[1], type[1], reserved[2], payload_size_be[8], payload.
 */
int pe_work_frame_encode(pe_work_message_type_t type,
                         const uint8_t *payload,
                         size_t payload_size,
                         uint8_t *out,
                         size_t capacity,
                         size_t *out_size);

/**
 * Validate a complete frame and expose its borrowed payload. No allocation or
 * byte-order conversion is performed by this function.
 */
int pe_work_frame_decode(const uint8_t *frame,
                         size_t frame_size,
                         pe_work_message_type_t *out_type,
                         const uint8_t **out_payload,
                         size_t *out_payload_size);

/** Encode a WorkUnit descriptor as a NUL-terminated UNIT frame payload. */
int pe_work_frame_encode_work_unit(const pe_work_unit_t *unit,
                                   uint8_t *out,
                                   size_t capacity,
                                   size_t *out_size);

/** Decode a UNIT frame into an initialized or empty WorkUnit destination. */
int pe_work_frame_decode_work_unit(const uint8_t *frame,
                                   size_t frame_size,
                                   pe_work_unit_t *out);

/** Encode a RESULT frame; floating-point fields preserve their exact bits. */
int pe_work_frame_encode_result(const pe_work_result_t *result,
                                uint8_t *out,
                                size_t capacity,
                                size_t *out_size);

/** Decode a RESULT frame; `delta` in `out` borrows bytes from `frame`. */
int pe_work_frame_decode_result(const uint8_t *frame,
                                size_t frame_size,
                                pe_work_result_t *out);

/** Encode a runtime capability descriptor as a NUL-terminated frame. */
int pe_work_frame_encode_capabilities(
    const pe_runtime_capabilities_t *runtime,
    uint8_t *out,
    size_t capacity,
    size_t *out_size);

/** Decode a CAPABILITIES frame into a runtime descriptor. */
int pe_work_frame_decode_capabilities(
    const uint8_t *frame,
    size_t frame_size,
    pe_runtime_capabilities_t *out);

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_WORK_PROTOCOL_H */

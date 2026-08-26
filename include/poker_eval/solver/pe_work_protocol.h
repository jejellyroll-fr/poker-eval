/*
 * pe_work_protocol.h - versioned binary framing for distributed work (DIST-03)
 */

#ifndef POKER_EVAL_PE_WORK_PROTOCOL_H
#define POKER_EVAL_PE_WORK_PROTOCOL_H

#include <poker_eval/solver/pe_work_unit.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PE_WORK_PROTOCOL_VERSION 1u
#define PE_WORK_PROTOCOL_HEADER_SIZE 16u
#define PE_WORK_PROTOCOL_MAX_PAYLOAD (16u * 1024u * 1024u)

typedef enum pe_work_message_type_t
{
    PE_WORK_MESSAGE_CAPABILITIES = 1,
    PE_WORK_MESSAGE_UNIT = 2,
    PE_WORK_MESSAGE_RESULT = 3
} pe_work_message_type_t;

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

#ifdef __cplusplus
}
#endif

#endif /* POKER_EVAL_PE_WORK_PROTOCOL_H */

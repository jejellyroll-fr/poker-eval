/*
 * work_protocol.c - versioned binary framing for distributed work (DIST-03)
 */

#include <poker_eval/solver/pe_work_protocol.h>

#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static const uint8_t pe_work_magic[4] = {'P', 'E', 'W', '1'};

static int socket_read_all(pe_work_socket_t socket,
                           uint8_t *buffer, size_t size)
{
    size_t offset = 0u;
    while (offset < size) {
        size_t remaining = size - offset;
        int requested = (remaining > (size_t)INT_MAX)
            ? INT_MAX : (int)remaining;
#if defined(_WIN32)
        int received = recv((SOCKET)socket, (char *)(buffer + offset),
                            requested, 0);
#else
        ssize_t received = recv((int)socket, buffer + offset,
                                (size_t)requested, 0);
        if (received < 0 && errno == EINTR)
            continue;
#endif
        if (received <= 0)
            return -1;
        offset += (size_t)received;
    }
    return 0;
}

static int socket_write_all(pe_work_socket_t socket,
                            const uint8_t *buffer, size_t size)
{
    size_t offset = 0u;
    while (offset < size) {
        size_t remaining = size - offset;
        int requested = (remaining > (size_t)INT_MAX)
            ? INT_MAX : (int)remaining;
#if defined(_WIN32)
        int sent = send((SOCKET)socket, (const char *)(buffer + offset),
                        requested, 0);
#else
        ssize_t sent = send((int)socket, buffer + offset,
                            (size_t)requested, 0);
        if (sent < 0 && errno == EINTR)
            continue;
#endif
        if (sent <= 0)
            return -1;
        offset += (size_t)sent;
    }
    return 0;
}

static int valid_type(pe_work_message_type_t type)
{
    return type >= PE_WORK_MESSAGE_CAPABILITIES &&
           type <= PE_WORK_MESSAGE_RESULT;
}

static void put_u64_be(uint8_t *out, uint64_t value)
{
    size_t i;
    for (i = 0u; i < 8u; ++i)
        out[i] = (uint8_t)(value >> (56u - 8u * i));
}

static uint64_t get_u64_be(const uint8_t *in)
{
    size_t i;
    uint64_t value = 0u;
    for (i = 0u; i < 8u; ++i)
        value = (value << 8u) | (uint64_t)in[i];
    return value;
}

static int valid_header(const uint8_t *header, size_t *out_payload_size)
{
    uint64_t encoded_size;
    size_t i;

    for (i = 0u; i < sizeof(pe_work_magic); ++i) {
        if (header[i] != pe_work_magic[i])
            return -1;
    }
    if (header[4] != PE_WORK_PROTOCOL_VERSION || header[6] != 0u ||
        header[7] != 0u || !valid_type((pe_work_message_type_t)header[5]))
        return -1;
    encoded_size = get_u64_be(header + 8u);
    if (encoded_size > PE_WORK_PROTOCOL_MAX_PAYLOAD ||
        encoded_size > SIZE_MAX - PE_WORK_PROTOCOL_HEADER_SIZE)
        return -1;
    *out_payload_size = (size_t)encoded_size;
    return 0;
}

static void put_double_be(uint8_t *out, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put_u64_be(out, bits);
}

static double get_double_be(const uint8_t *in)
{
    uint64_t bits = get_u64_be(in);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

int pe_work_result_validate(const pe_work_result_t *result)
{
    if (!result || result->backend <= PE_COMPUTE_AUTO ||
        result->backend >= PE_COMPUTE_COUNT ||
        (result->constraints_satisfied != 0 &&
         result->constraints_satisfied != 1) ||
        result->iteration_end < result->iteration_begin ||
        !isfinite(result->exploitability) ||
        !isfinite(result->worst_margin) ||
        !isfinite(result->mean_margin) ||
        !isfinite(result->units_per_s) || result->units_per_s < 0.0 ||
        result->delta_size > PE_WORK_PROTOCOL_MAX_PAYLOAD -
                             PE_WORK_PROTOCOL_RESULT_FIXED_SIZE ||
        (result->delta_size != 0u && !result->delta))
        return -1;
    return 0;
}

void pe_work_result_release(pe_work_result_t *result)
{
    if (!result)
        return;
    if (result->delta_owned)
        free((void *)(uintptr_t)result->delta);
    result->delta = NULL;
    result->delta_size = 0u;
    result->delta_owned = 0;
}

size_t pe_work_frame_size(size_t payload_size)
{
    if (payload_size > PE_WORK_PROTOCOL_MAX_PAYLOAD ||
        payload_size > SIZE_MAX - PE_WORK_PROTOCOL_HEADER_SIZE)
        return 0u;
    return PE_WORK_PROTOCOL_HEADER_SIZE + payload_size;
}

int pe_work_transport_init(void)
{
#if defined(_WIN32)
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void pe_work_transport_cleanup(void)
{
#if defined(_WIN32)
    WSACleanup();
#endif
}

int pe_work_socket_send_frame(pe_work_socket_t socket,
                              const uint8_t *frame,
                              size_t frame_size)
{
    pe_work_message_type_t type;
    const uint8_t *payload;
    size_t payload_size;

    if (socket == PE_WORK_SOCKET_INVALID ||
        pe_work_frame_decode(frame, frame_size, &type, &payload,
                             &payload_size) != 0)
        return -1;
    (void)type;
    (void)payload;
    (void)payload_size;
    return socket_write_all(socket, frame, frame_size);
}

int pe_work_socket_recv_frame(pe_work_socket_t socket,
                              uint8_t *buffer,
                              size_t capacity,
                              size_t *out_size)
{
    uint8_t header[PE_WORK_PROTOCOL_HEADER_SIZE];
    pe_work_message_type_t type;
    const uint8_t *payload;
    size_t payload_size;
    size_t frame_size;

    if (socket == PE_WORK_SOCKET_INVALID || !buffer || !out_size ||
        capacity < PE_WORK_PROTOCOL_HEADER_SIZE ||
        socket_read_all(socket, header, sizeof(header)) != 0 ||
        valid_header(header, &payload_size) != 0)
        return -1;
    frame_size = PE_WORK_PROTOCOL_HEADER_SIZE + payload_size;
    if (frame_size > capacity ||
        socket_read_all(socket, buffer + PE_WORK_PROTOCOL_HEADER_SIZE,
                        payload_size) != 0)
        return -1;
    memcpy(buffer, header, sizeof(header));
    if (pe_work_frame_decode(buffer, frame_size, &type, &payload,
                             &payload_size) != 0)
        return -1;
    *out_size = frame_size;
    return 0;
}

int pe_work_socket_close(pe_work_socket_t socket)
{
    if (socket == PE_WORK_SOCKET_INVALID)
        return -1;
#if defined(_WIN32)
    return closesocket((SOCKET)socket) == 0 ? 0 : -1;
#else
    return close((int)socket) == 0 ? 0 : -1;
#endif
}

pe_work_socket_t pe_work_tcp_listen(uint16_t port, uint16_t *out_bound_port)
{
#if defined(_WIN32)
    SOCKET fd;
    int address_length = (int)sizeof(struct sockaddr_in);
#else
    int fd;
    socklen_t address_length = (socklen_t)sizeof(struct sockaddr_in);
#endif
    int reuse = 1;
    struct sockaddr_in address;

    if (out_bound_port)
        *out_bound_port = 0u;
#if defined(_WIN32)
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET)
#else
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
#endif
        return PE_WORK_SOCKET_INVALID;
#if defined(_WIN32)
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
                     (int)sizeof(reuse));
#else
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse,
                     (socklen_t)sizeof(reuse));
#endif
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(fd, 16) != 0) {
        (void)pe_work_socket_close((pe_work_socket_t)fd);
        return PE_WORK_SOCKET_INVALID;
    }
    if (out_bound_port &&
        getsockname(fd, (struct sockaddr *)&address, &address_length) == 0)
        *out_bound_port = ntohs(address.sin_port);
    return (pe_work_socket_t)fd;
}

pe_work_socket_t pe_work_tcp_accept(pe_work_socket_t listener)
{
#if defined(_WIN32)
    SOCKET fd;
#else
    int fd;
#endif
    if (listener == PE_WORK_SOCKET_INVALID)
        return PE_WORK_SOCKET_INVALID;
#if defined(_WIN32)
    fd = accept((SOCKET)listener, NULL, NULL);
    return fd == INVALID_SOCKET ? PE_WORK_SOCKET_INVALID :
           (pe_work_socket_t)fd;
#else
    fd = accept((int)listener, NULL, NULL);
    return fd < 0 ? PE_WORK_SOCKET_INVALID : (pe_work_socket_t)fd;
#endif
}

pe_work_socket_t pe_work_tcp_connect(const char *host, uint16_t port)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *current;
    char service[8];
    pe_work_socket_t connected = PE_WORK_SOCKET_INVALID;

    if (!host || !*host)
        return PE_WORK_SOCKET_INVALID;
    (void)snprintf(service, sizeof(service), "%u", (unsigned)port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    if (getaddrinfo(host, service, &hints, &addresses) != 0)
        return PE_WORK_SOCKET_INVALID;
    for (current = addresses; current != NULL; current = current->ai_next) {
#if defined(_WIN32)
        SOCKET fd = socket(current->ai_family, current->ai_socktype,
                           current->ai_protocol);
        if (fd == INVALID_SOCKET)
            continue;
        if (connect(fd, current->ai_addr, (int)current->ai_addrlen) == 0) {
            connected = (pe_work_socket_t)fd;
            break;
        }
        (void)closesocket(fd);
#else
        int fd = socket(current->ai_family, current->ai_socktype,
                        current->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, current->ai_addr, current->ai_addrlen) == 0) {
            connected = (pe_work_socket_t)fd;
            break;
        }
        (void)close(fd);
#endif
    }
    freeaddrinfo(addresses);
    return connected;
}

int pe_work_socket_send_capabilities(
    pe_work_socket_t socket,
    const pe_runtime_capabilities_t *runtime)
{
    uint8_t *buffer;
    size_t capacity = PE_WORK_PROTOCOL_HEADER_SIZE +
                      PE_RUNTIME_DESCRIPTOR_MAX + 1u;
    size_t frame_size;
    int rc;

    buffer = (uint8_t *)malloc(capacity);
    if (!buffer)
        return -1;
    rc = pe_work_frame_encode_capabilities(runtime, buffer, capacity,
                                            &frame_size);
    if (rc == 0)
        rc = pe_work_socket_send_frame(socket, buffer, frame_size);
    free(buffer);
    return rc;
}

int pe_work_socket_recv_capabilities(
    pe_work_socket_t socket,
    pe_runtime_capabilities_t *out)
{
    uint8_t *buffer;
    size_t capacity = PE_WORK_PROTOCOL_HEADER_SIZE +
                      PE_RUNTIME_DESCRIPTOR_MAX + 1u;
    size_t frame_size;
    int rc;

    buffer = (uint8_t *)malloc(capacity);
    if (!buffer)
        return -1;
    rc = pe_work_socket_recv_frame(socket, buffer, capacity, &frame_size);
    if (rc == 0)
        rc = pe_work_frame_decode_capabilities(buffer, frame_size, out);
    free(buffer);
    return rc;
}

int pe_work_socket_send_work_unit(pe_work_socket_t socket,
                                  const pe_work_unit_t *unit)
{
    uint8_t *buffer;
    size_t capacity = PE_WORK_PROTOCOL_HEADER_SIZE +
                      PE_WORK_UNIT_DESCRIPTOR_MAX + 1u;
    size_t frame_size;
    int rc;

    buffer = (uint8_t *)malloc(capacity);
    if (!buffer)
        return -1;
    rc = pe_work_frame_encode_work_unit(unit, buffer, capacity, &frame_size);
    if (rc == 0)
        rc = pe_work_socket_send_frame(socket, buffer, frame_size);
    free(buffer);
    return rc;
}

int pe_work_socket_recv_work_unit(pe_work_socket_t socket,
                                  pe_work_unit_t *out)
{
    uint8_t *buffer;
    size_t capacity = PE_WORK_PROTOCOL_HEADER_SIZE +
                      PE_WORK_UNIT_DESCRIPTOR_MAX + 1u;
    size_t frame_size;
    int rc;

    buffer = (uint8_t *)malloc(capacity);
    if (!buffer)
        return -1;
    rc = pe_work_socket_recv_frame(socket, buffer, capacity, &frame_size);
    if (rc == 0)
        rc = pe_work_frame_decode_work_unit(buffer, frame_size, out);
    free(buffer);
    return rc;
}

int pe_work_socket_send_result(pe_work_socket_t socket,
                               const pe_work_result_t *result)
{
    uint8_t *buffer;
    size_t capacity;
    size_t frame_size;
    int rc;

    if (pe_work_result_validate(result) != 0 ||
        result->delta_size > SIZE_MAX - PE_WORK_PROTOCOL_HEADER_SIZE -
                              PE_WORK_PROTOCOL_RESULT_FIXED_SIZE)
        return -1;
    capacity = PE_WORK_PROTOCOL_HEADER_SIZE +
               PE_WORK_PROTOCOL_RESULT_FIXED_SIZE + result->delta_size;
    buffer = (uint8_t *)malloc(capacity);
    if (!buffer)
        return -1;
    rc = pe_work_frame_encode_result(result, buffer, capacity, &frame_size);
    if (rc == 0)
        rc = pe_work_socket_send_frame(socket, buffer, frame_size);
    free(buffer);
    return rc;
}

int pe_work_frame_encode(pe_work_message_type_t type,
                         const uint8_t *payload,
                         size_t payload_size,
                         uint8_t *out,
                         size_t capacity,
                         size_t *out_size)
{
    size_t frame_size;
    size_t i;

    if (!out || !out_size || !valid_type(type) ||
        (payload_size != 0u && !payload))
        return -1;
    frame_size = pe_work_frame_size(payload_size);
    if (frame_size == 0u || capacity < frame_size)
        return -1;

    for (i = 0u; i < sizeof(pe_work_magic); ++i)
        out[i] = pe_work_magic[i];
    out[4] = PE_WORK_PROTOCOL_VERSION;
    out[5] = (uint8_t)type;
    out[6] = 0u;
    out[7] = 0u;
    put_u64_be(out + 8u, (uint64_t)payload_size);
    for (i = 0u; i < payload_size; ++i)
        out[PE_WORK_PROTOCOL_HEADER_SIZE + i] = payload[i];
    *out_size = frame_size;
    return 0;
}

int pe_work_frame_decode(const uint8_t *frame,
                         size_t frame_size,
                         pe_work_message_type_t *out_type,
                         const uint8_t **out_payload,
                         size_t *out_payload_size)
{
    uint64_t encoded_size;
    size_t i;

    if (!frame || !out_type || !out_payload || !out_payload_size ||
        frame_size < PE_WORK_PROTOCOL_HEADER_SIZE)
        return -1;
    for (i = 0u; i < sizeof(pe_work_magic); ++i) {
        if (frame[i] != pe_work_magic[i])
            return -1;
    }
    if (frame[4] != PE_WORK_PROTOCOL_VERSION || frame[6] != 0u ||
        frame[7] != 0u || !valid_type((pe_work_message_type_t)frame[5]))
        return -1;

    encoded_size = get_u64_be(frame + 8u);
    if (encoded_size > PE_WORK_PROTOCOL_MAX_PAYLOAD ||
        encoded_size != (uint64_t)(frame_size - PE_WORK_PROTOCOL_HEADER_SIZE))
        return -1;

    *out_type = (pe_work_message_type_t)frame[5];
    *out_payload = frame + PE_WORK_PROTOCOL_HEADER_SIZE;
    *out_payload_size = (size_t)encoded_size;
    return 0;
}

int pe_work_frame_encode_work_unit(const pe_work_unit_t *unit,
                                   uint8_t *out,
                                   size_t capacity,
                                   size_t *out_size)
{
    size_t descriptor_size;
    size_t payload_size;
    size_t frame_size;

    if (!unit || !out || !out_size)
        return -1;
    descriptor_size = pe_work_unit_to_string(unit, NULL, 0u);
    if (descriptor_size == 0u || descriptor_size == SIZE_MAX)
        return -1;
    payload_size = descriptor_size + 1u;
    frame_size = pe_work_frame_size(payload_size);
    if (frame_size == 0u || capacity < frame_size)
        return -1;
    if (pe_work_unit_to_string(unit, (char *)(out + PE_WORK_PROTOCOL_HEADER_SIZE),
                               payload_size) != descriptor_size)
        return -1;
    return pe_work_frame_encode(PE_WORK_MESSAGE_UNIT,
                                out + PE_WORK_PROTOCOL_HEADER_SIZE,
                                payload_size, out, capacity, out_size);
}

int pe_work_frame_decode_work_unit(const uint8_t *frame,
                                   size_t frame_size,
                                   pe_work_unit_t *out)
{
    pe_work_message_type_t type;
    const uint8_t *payload;
    size_t payload_size;

    if (!out || pe_work_frame_decode(frame, frame_size, &type, &payload,
                                     &payload_size) != 0 ||
        type != PE_WORK_MESSAGE_UNIT || payload_size == 0u ||
        payload[payload_size - 1u] != '\0')
        return -1;
    return pe_work_unit_from_string((const char *)payload, out);
}

int pe_work_frame_encode_result(const pe_work_result_t *result,
                                uint8_t *out,
                                size_t capacity,
                                size_t *out_size)
{
    size_t payload_size;
    size_t frame_size;
    uint8_t *payload;

    if (!out || !out_size || pe_work_result_validate(result) != 0)
        return -1;
    if (result->delta_size > PE_WORK_PROTOCOL_MAX_PAYLOAD -
                             PE_WORK_PROTOCOL_RESULT_FIXED_SIZE)
        return -1;
    payload_size = PE_WORK_PROTOCOL_RESULT_FIXED_SIZE + result->delta_size;
    frame_size = pe_work_frame_size(payload_size);
    if (frame_size == 0u || capacity < frame_size)
        return -1;

    payload = out + PE_WORK_PROTOCOL_HEADER_SIZE;
    put_u64_be(payload, result->public_state);
    put_u64_be(payload + 8u, result->iteration_begin);
    put_u64_be(payload + 16u, result->iteration_end);
    put_u64_be(payload + 24u, result->iterations);
    put_u64_be(payload + 32u, result->infosets_trained);
    payload[40] = (uint8_t)result->backend;
    payload[41] = (uint8_t)result->constraints_satisfied;
    memset(payload + 42u, 0, 6u);
    put_double_be(payload + 48u, result->exploitability);
    put_double_be(payload + 56u, result->worst_margin);
    put_double_be(payload + 64u, result->mean_margin);
    put_u64_be(payload + 72u, (uint64_t)result->delta_size);
    put_u64_be(payload + 80u, result->elapsed_ns);
    put_double_be(payload + 88u, result->units_per_s);
    if (result->delta_size != 0u)
        memcpy(payload + PE_WORK_PROTOCOL_RESULT_FIXED_SIZE, result->delta,
               result->delta_size);
    return pe_work_frame_encode(PE_WORK_MESSAGE_RESULT, payload, payload_size,
                                out, capacity, out_size);
}

int pe_work_frame_decode_result(const uint8_t *frame,
                                size_t frame_size,
                                pe_work_result_t *out)
{
    pe_work_message_type_t type;
    const uint8_t *payload;
    size_t payload_size;
    uint64_t delta_size;

    if (!out || pe_work_frame_decode(frame, frame_size, &type, &payload,
                                     &payload_size) != 0 ||
        type != PE_WORK_MESSAGE_RESULT ||
        payload_size < PE_WORK_PROTOCOL_RESULT_FIXED_SIZE)
        return -1;
    delta_size = get_u64_be(payload + 72u);
    if (delta_size > PE_WORK_PROTOCOL_MAX_PAYLOAD -
                     PE_WORK_PROTOCOL_RESULT_FIXED_SIZE ||
        delta_size != (uint64_t)(payload_size -
                                 PE_WORK_PROTOCOL_RESULT_FIXED_SIZE) ||
        payload[42] != 0u || payload[43] != 0u || payload[44] != 0u ||
        payload[45] != 0u || payload[46] != 0u || payload[47] != 0u ||
        payload[40] <= PE_COMPUTE_AUTO ||
        payload[40] >= PE_COMPUTE_COUNT ||
        (payload[41] != 0u && payload[41] != 1u))
        return -1;

    out->public_state = get_u64_be(payload);
    out->iteration_begin = get_u64_be(payload + 8u);
    out->iteration_end = get_u64_be(payload + 16u);
    out->iterations = get_u64_be(payload + 24u);
    out->infosets_trained = get_u64_be(payload + 32u);
    out->backend = (pe_compute_kind_t)payload[40];
    out->constraints_satisfied = (int)payload[41];
    out->exploitability = get_double_be(payload + 48u);
    out->worst_margin = get_double_be(payload + 56u);
    out->mean_margin = get_double_be(payload + 64u);
    out->elapsed_ns = get_u64_be(payload + 80u);
    out->units_per_s = get_double_be(payload + 88u);
    out->delta = payload + PE_WORK_PROTOCOL_RESULT_FIXED_SIZE;
    out->delta_size = (size_t)delta_size;
    out->delta_owned = 0;
    if (pe_work_result_validate(out) != 0)
        return -1;
    return 0;
}

int pe_work_frame_encode_capabilities(
    const pe_runtime_capabilities_t *runtime,
    uint8_t *out,
    size_t capacity,
    size_t *out_size)
{
    size_t descriptor_size;
    size_t payload_size;
    size_t frame_size;

    if (!runtime || !out || !out_size)
        return -1;
    descriptor_size = pe_runtime_descriptor_to_string(runtime, NULL, 0u);
    if (descriptor_size == 0u || descriptor_size == SIZE_MAX)
        return -1;
    payload_size = descriptor_size + 1u;
    frame_size = pe_work_frame_size(payload_size);
    if (frame_size == 0u || capacity < frame_size)
        return -1;
    if (pe_runtime_descriptor_to_string(
            runtime, (char *)(out + PE_WORK_PROTOCOL_HEADER_SIZE),
            payload_size) != descriptor_size)
        return -1;
    return pe_work_frame_encode(PE_WORK_MESSAGE_CAPABILITIES,
                                out + PE_WORK_PROTOCOL_HEADER_SIZE,
                                payload_size, out, capacity, out_size);
}

int pe_work_frame_decode_capabilities(
    const uint8_t *frame,
    size_t frame_size,
    pe_runtime_capabilities_t *out)
{
    pe_work_message_type_t type;
    const uint8_t *payload;
    size_t payload_size;

    if (!out || pe_work_frame_decode(frame, frame_size, &type, &payload,
                                     &payload_size) != 0 ||
        type != PE_WORK_MESSAGE_CAPABILITIES || payload_size == 0u ||
        payload[payload_size - 1u] != '\0')
        return -1;
    return pe_runtime_descriptor_from_string((const char *)payload, out);
}

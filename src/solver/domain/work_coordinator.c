/*
 * work_coordinator.c - heterogeneous worker registry and dispatcher
 */

#include <poker_eval/solver/pe_work_coordinator.h>

#include <math.h>
#include <float.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct worker_candidate_t
{
    uint32_t worker_id;
    pe_compute_kind_t backend;
    double rate;
    size_t units;
    long double fraction;
} worker_candidate_t;

static int finite_double(double value)
{
    return value <= DBL_MAX && value >= -DBL_MAX;
}

static double effective_rate(const pe_runtime_backend_info_t *backend)
{
    double rate = backend->update_elements_per_s;
    if (!(rate > 0.0) || !finite_double(rate))
        rate = backend->strategy_elements_per_s;
    if (!(rate > 0.0) || !finite_double(rate))
        rate = backend->terminal_elements_per_s;
    if (!(rate > 0.0) || !finite_double(rate))
        rate = 1.0;
    return rate;
}

static int backend_usable(const pe_runtime_backend_info_t *backend,
                          pe_compute_kind_t kind)
{
    if (!backend || !backend->compiled || !backend->runtime_available ||
        !backend->validated)
        return 0;
    if (pe_compute_kind_is_gpu(kind) &&
        !(backend->capabilities & PE_CAP_GPU_TERMINAL_EVAL))
        return 0;
    return 1;
}

static pe_compute_kind_t worker_backend(
    const pe_runtime_capabilities_t *runtime)
{
    pe_compute_kind_t best = PE_COMPUTE_AUTO;
    double best_rate = 0.0;
    size_t i;

    for (i = 1u; i < PE_COMPUTE_COUNT; ++i) {
        const pe_runtime_backend_info_t *info = &runtime->backends[i];
        double rate;
        if (!backend_usable(info, (pe_compute_kind_t)i))
            continue;
        rate = effective_rate(info);
        if (best == PE_COMPUTE_AUTO || rate > best_rate) {
            best = (pe_compute_kind_t)i;
            best_rate = rate;
        }
    }
    return best;
}

void pe_work_coordinator_init(pe_work_coordinator_t *coordinator)
{
    if (coordinator)
        memset(coordinator, 0, sizeof(*coordinator));
}

int pe_work_coordinator_register(pe_work_coordinator_t *coordinator,
                                 uint32_t worker_id,
                                 const pe_runtime_capabilities_t *runtime)
{
    size_t i;

    if (!coordinator || !runtime || worker_id == 0u)
        return -1;
    for (i = 0u; i < coordinator->worker_count; ++i) {
        if (coordinator->workers[i].worker_id == worker_id) {
            coordinator->workers[i].runtime = *runtime;
            return 0;
        }
    }
    if (coordinator->worker_count >= PE_WORK_COORDINATOR_MAX_WORKERS)
        return -1;
    coordinator->workers[coordinator->worker_count].worker_id = worker_id;
    coordinator->workers[coordinator->worker_count].runtime = *runtime;
    ++coordinator->worker_count;
    return 0;
}

int pe_work_coordinator_accept_announcement(
    pe_work_coordinator_t *coordinator,
    uint32_t worker_id,
    pe_work_socket_t socket)
{
    pe_runtime_capabilities_t runtime;

    if (!coordinator || worker_id == 0u ||
        socket == PE_WORK_SOCKET_INVALID ||
        pe_work_socket_recv_capabilities(socket, &runtime) != 0)
        return -1;
    return pe_work_coordinator_register(coordinator, worker_id, &runtime);
}

int pe_work_coordinator_accept_tcp(pe_work_coordinator_t *coordinator,
                                   pe_work_socket_t listener,
                                   uint32_t worker_id,
                                   pe_work_worker_channel_t *out_channel)
{
    pe_work_socket_t socket;
    if (!coordinator || !out_channel || worker_id == 0u)
        return -1;
    socket = pe_work_tcp_accept(listener);
    if (socket == PE_WORK_SOCKET_INVALID)
        return -1;
    if (pe_work_coordinator_accept_announcement(coordinator, worker_id,
                                                socket) != 0) {
        (void)pe_work_socket_close(socket);
        return -1;
    }
    out_channel->worker_id = worker_id;
    out_channel->socket = socket;
    return 0;
}

int pe_work_coordinator_accept_tcp_batch(
    pe_work_coordinator_t *coordinator,
    pe_work_socket_t listener,
    uint32_t first_worker_id,
    pe_work_worker_channel_t *out_channels,
    size_t capacity,
    size_t *out_count)
{
    size_t i;

    if (out_count)
        *out_count = 0u;
    if (!coordinator || listener == PE_WORK_SOCKET_INVALID ||
        !out_channels || capacity == 0u || first_worker_id == 0u ||
        capacity > (size_t)UINT32_MAX - (size_t)first_worker_id + 1u)
        return -1;
    for (i = 0u; i < capacity; ++i)
    {
        if (pe_work_coordinator_accept_tcp(
                coordinator, listener, first_worker_id + (uint32_t)i,
                &out_channels[i]) != 0)
        {
            size_t accepted;
            for (accepted = 0u; accepted < i; ++accepted)
            {
                (void)pe_work_socket_close(out_channels[accepted].socket);
                (void)pe_work_coordinator_unregister(
                    coordinator, out_channels[accepted].worker_id);
                out_channels[accepted] = (pe_work_worker_channel_t){0};
            }
            return -1;
        }
    }
    if (out_count)
        *out_count = capacity;
    return 0;
}

int pe_work_coordinator_unregister(pe_work_coordinator_t *coordinator,
                                   uint32_t worker_id)
{
    size_t i;

    if (!coordinator || worker_id == 0u)
        return -1;
    for (i = 0u; i < coordinator->worker_count; ++i) {
        if (coordinator->workers[i].worker_id == worker_id) {
            size_t last = coordinator->worker_count - 1u;
            if (i != last)
                coordinator->workers[i] = coordinator->workers[last];
            coordinator->workers[last] = (pe_work_worker_t){0};
            --coordinator->worker_count;
            return 0;
        }
    }
    return -1;
}

int pe_work_coordinator_schedule(
    const pe_work_coordinator_t *coordinator,
    size_t total_units,
    pe_work_worker_assignment_t *out,
    size_t capacity)
{
    worker_candidate_t candidates[PE_WORK_COORDINATOR_MAX_WORKERS];
    size_t candidate_count = 0u;
    size_t i;
    size_t assigned = 0u;
    long double total_rate = 0.0L;

    if (!coordinator || (!out && capacity != 0u))
        return -1;
    if (total_units == 0u)
        return 0;

    for (i = 0u; i < coordinator->worker_count; ++i) {
        pe_compute_kind_t backend = worker_backend(
            &coordinator->workers[i].runtime);
        const pe_runtime_backend_info_t *info;
        if (backend <= PE_COMPUTE_AUTO || backend >= PE_COMPUTE_COUNT)
            continue;
        info = &coordinator->workers[i].runtime.backends[backend];
        if (!backend_usable(info, backend))
            continue;
        candidates[candidate_count].worker_id =
            coordinator->workers[i].worker_id;
        candidates[candidate_count].backend = backend;
        candidates[candidate_count].rate = effective_rate(info);
        candidates[candidate_count].units = 0u;
        candidates[candidate_count].fraction = 0.0L;
        total_rate += (long double)candidates[candidate_count].rate;
        ++candidate_count;
    }
    if (candidate_count == 0u || candidate_count > capacity)
        return candidate_count == 0u ? 0 : -1;

    for (i = 0u; i < candidate_count; ++i) {
        long double ideal = ((long double)total_units *
                             (long double)candidates[i].rate) / total_rate;
        long double floored = floorl(ideal);
        candidates[i].units = (size_t)floored;
        candidates[i].fraction = ideal - floored;
        assigned += candidates[i].units;
    }
    while (assigned < total_units) {
        size_t best = candidate_count;
        for (i = 0u; i < candidate_count; ++i) {
            if (best == candidate_count ||
                candidates[i].fraction > candidates[best].fraction ||
                (!(candidates[i].fraction < candidates[best].fraction) &&
                 !(candidates[i].fraction > candidates[best].fraction) &&
                 candidates[i].worker_id < candidates[best].worker_id))
                best = i;
        }
        if (best == candidate_count)
            return -1;
        ++candidates[best].units;
        candidates[best].fraction = -1.0L;
        ++assigned;
    }

    assigned = 0u;
    for (i = 0u; i < candidate_count; ++i) {
        if (candidates[i].units == 0u)
            continue;
        out[assigned].worker_id = candidates[i].worker_id;
        out[assigned].backend = candidates[i].backend;
        out[assigned].first_unit = 0u;
        if (assigned != 0u)
            out[assigned].first_unit = out[assigned - 1u].first_unit +
                                       out[assigned - 1u].unit_count;
        out[assigned].unit_count = candidates[i].units;
        out[assigned].units_per_s = candidates[i].rate;
        ++assigned;
    }
    return (int)assigned;
}

int pe_work_coordinator_dispatch(
    const pe_work_coordinator_t *coordinator,
    const pe_work_unit_t *units,
    size_t unit_count,
    const pe_work_worker_channel_t *channels,
    size_t channel_count,
    pe_work_worker_assignment_t *out,
    size_t capacity)
{
    pe_work_worker_assignment_t assignments[PE_WORK_COORDINATOR_MAX_WORKERS];
    size_t i;
    int assignment_count;

    if (!coordinator || (unit_count != 0u && !units) ||
        (!channels && channel_count != 0u))
        return -1;
    assignment_count = pe_work_coordinator_schedule(
        coordinator, unit_count, assignments,
        PE_WORK_COORDINATOR_MAX_WORKERS);
    if (assignment_count < 0 || (size_t)assignment_count > capacity ||
        (assignment_count != 0 && !out))
        return -1;

    for (i = 0u; i < (size_t)assignment_count; ++i) {
        size_t channel_index;
        int found = 0;
        for (channel_index = 0u; channel_index < channel_count;
             ++channel_index) {
            if (channels[channel_index].worker_id ==
                assignments[i].worker_id) {
                found = 1;
                break;
            }
        }
        if (!found)
            return -1;
    }

    for (i = 0u; i < (size_t)assignment_count; ++i) {
        size_t channel_index;
        size_t unit_index;
        for (channel_index = 0u; channel_index < channel_count;
             ++channel_index)
            if (channels[channel_index].worker_id ==
                assignments[i].worker_id)
                break;
        for (unit_index = assignments[i].first_unit;
             unit_index < assignments[i].first_unit +
                           assignments[i].unit_count; ++unit_index) {
            if (pe_work_socket_send_work_unit(channels[channel_index].socket,
                                               &units[unit_index]) != 0)
                return -1;
        }
        out[i] = assignments[i];
    }
    return assignment_count;
}

static int coordinator_channel_index(
    const pe_work_worker_assignment_t *assignment,
    const pe_work_worker_channel_t *channels,
    size_t channel_count,
    size_t *out_index)
{
    size_t i;

    for (i = 0u; i < channel_count; ++i)
        if (channels[i].worker_id == assignment->worker_id)
        {
            *out_index = i;
            return 0;
        }
    return -1;
}

int pe_work_coordinator_dispatch_and_collect(
    const pe_work_coordinator_t *coordinator,
    const pe_work_unit_t *units,
    size_t unit_count,
    const pe_work_worker_channel_t *channels,
    size_t channel_count,
    pe_work_worker_assignment_t *out,
    size_t capacity,
    pe_work_reducer_t *reducer)
{
    uint8_t *frame;
    size_t frame_capacity = PE_WORK_PROTOCOL_HEADER_SIZE +
                            PE_WORK_PROTOCOL_MAX_PAYLOAD;
    int assignment_count;
    size_t i;

    if (!reducer || !units || unit_count == 0u || !channels ||
        channel_count == 0u)
        return -1;
    assignment_count = pe_work_coordinator_schedule(
        coordinator, unit_count, out, capacity);
    if (assignment_count <= 0)
        return assignment_count;

    frame = (uint8_t *)malloc(frame_capacity);
    if (!frame)
        return -1;
    for (i = 0u; i < (size_t)assignment_count; ++i)
    {
        const pe_work_worker_assignment_t *assignment = &out[i];
        size_t channel_index;
        size_t j;

        if (coordinator_channel_index(assignment, channels, channel_count,
                                      &channel_index) != 0)
        {
            free(frame);
            return -1;
        }
        for (j = 0u; j < assignment->unit_count; ++j)
        {
            const pe_work_unit_t *unit = &units[assignment->first_unit + j];
            pe_work_result_t result;
            size_t frame_size;

            if (pe_work_socket_send_work_unit(channels[channel_index].socket,
                                              unit) != 0 ||
                pe_work_socket_recv_frame(channels[channel_index].socket,
                                          frame, frame_capacity,
                                          &frame_size) != 0)
            {
                free(frame);
                return -1;
            }
            if (pe_work_frame_decode_result(frame, frame_size, &result) != 0 ||
                result.public_state != unit->public_state ||
                result.iteration_begin != unit->iteration_begin ||
                result.iteration_end != unit->iteration_end ||
                result.backend != assignment->backend ||
                pe_work_reducer_accept(reducer, assignment->worker_id,
                                       &result) != 0)
            {
                free(frame);
                return -1;
            }
        }
    }
    free(frame);
    return assignment_count;
}

int pe_work_coordinator_collect_results(
    const pe_work_unit_t *units,
    size_t unit_count,
    const pe_work_worker_assignment_t *assignments,
    size_t assignment_count,
    const pe_work_worker_channel_t *channels,
    size_t channel_count,
    pe_work_reducer_t *reducer)
{
    uint8_t *frame;
    size_t frame_capacity = PE_WORK_PROTOCOL_HEADER_SIZE +
                            PE_WORK_PROTOCOL_MAX_PAYLOAD;
    size_t i;
    int rc = 0;

    if (!units || !assignments || !channels || !reducer ||
        unit_count == 0u || assignment_count == 0u || channel_count == 0u)
        return -1;
    frame = (uint8_t *)malloc(frame_capacity);
    if (!frame)
        return -1;
    for (i = 0u; i < assignment_count && rc == 0; ++i) {
        const pe_work_worker_assignment_t *assignment = &assignments[i];
        size_t channel_index;
        size_t j;
        if (assignment->first_unit > unit_count ||
            assignment->unit_count > unit_count - assignment->first_unit)
            rc = -1;
        channel_index = 0u;
        while (rc == 0 && channel_index < channel_count &&
               channels[channel_index].worker_id != assignment->worker_id)
            ++channel_index;
        if (rc == 0 && channel_index == channel_count)
            rc = -1;
        for (j = 0u; rc == 0 && j < assignment->unit_count; ++j) {
            pe_work_result_t result;
            size_t frame_size;
            const pe_work_unit_t *unit = &units[assignment->first_unit + j];
            if (pe_work_socket_recv_frame(channels[channel_index].socket, frame,
                                          frame_capacity, &frame_size) != 0 ||
                pe_work_frame_decode_result(frame, frame_size, &result) != 0 ||
                result.public_state != unit->public_state ||
                result.iteration_begin != unit->iteration_begin ||
                result.iteration_end != unit->iteration_end ||
                result.backend != assignment->backend ||
                pe_work_reducer_accept(reducer, assignment->worker_id,
                                       &result) != 0)
                rc = -1;
        }
    }
    free(frame);
    return rc;
}

int pe_work_coordinator_shutdown(
    const pe_work_worker_channel_t *channels, size_t channel_count)
{
    size_t i;

    if (!channels && channel_count != 0u)
        return -1;
    for (i = 0u; i < channel_count; ++i)
        if (channels[i].socket == PE_WORK_SOCKET_INVALID ||
            pe_work_socket_send_shutdown(channels[i].socket) != 0)
            return -1;
    return 0;
}

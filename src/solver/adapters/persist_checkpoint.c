/*
 * persist_checkpoint.c - Portable v2 checkpoint adapter (API-04)
 */

#include <poker_eval/solver/pe_persist.h>

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PE_CHECKPOINT_VERSION 2u
#define PE_CHECKPOINT_ENDIAN  0x01020304u
#define PE_CHECKPOINT_MAX_ENTRIES 10000000u
#define PE_CHECKPOINT_MAGIC "PECHKPT2"

typedef struct
{
    uint64_t key;
    uint16_t actions;
    uint16_t combos;
    int8_t street;
    uint8_t flags;
    uint8_t present;
    size_t slots;
    double *values[PE_VALUES_COUNT];
} checkpoint_entry_t;

static int write_bytes(FILE *file, const void *data, size_t count)
{
    return fwrite(data, 1u, count, file) == count ? 0 : -1;
}

static int read_bytes(FILE *file, void *data, size_t count)
{
    return fread(data, 1u, count, file) == count ? 0 : -1;
}

static int write_u16(FILE *file, uint16_t value)
{
    unsigned char bytes[2] = {(unsigned char)value,
                              (unsigned char)(value >> 8)};
    return write_bytes(file, bytes, sizeof(bytes));
}

static int write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4] = {(unsigned char)value,
                              (unsigned char)(value >> 8),
                              (unsigned char)(value >> 16),
                              (unsigned char)(value >> 24)};
    return write_bytes(file, bytes, sizeof(bytes));
}

static int write_u64(FILE *file, uint64_t value)
{
    unsigned char bytes[8];
    unsigned i;
    for (i = 0u; i < 8u; ++i)
        bytes[i] = (unsigned char)(value >> (i * 8u));
    return write_bytes(file, bytes, sizeof(bytes));
}

static int read_u16(FILE *file, uint16_t *out)
{
    unsigned char bytes[2];
    if (!out || read_bytes(file, bytes, sizeof(bytes)) != 0)
        return -1;
    *out = (uint16_t)((uint16_t)bytes[0] |
                      (uint16_t)((uint16_t)bytes[1] << 8));
    return 0;
}

static int read_u32(FILE *file, uint32_t *out)
{
    unsigned char bytes[4];
    if (!out || read_bytes(file, bytes, sizeof(bytes)) != 0)
        return -1;
    *out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return 0;
}

static int read_u64(FILE *file, uint64_t *out)
{
    unsigned char bytes[8];
    uint64_t value = 0u;
    unsigned i;
    if (!out || read_bytes(file, bytes, sizeof(bytes)) != 0)
        return -1;
    for (i = 0u; i < 8u; ++i)
        value |= (uint64_t)bytes[i] << (i * 8u);
    *out = value;
    return 0;
}

static int write_double(FILE *file, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return write_u64(file, bits);
}

static int read_double(FILE *file, double *out)
{
    uint64_t bits;
    if (!out || read_u64(file, &bits) != 0)
        return -1;
    memcpy(out, &bits, sizeof(*out));
    return 0;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t count)
{
    const unsigned char *bytes = (const unsigned char *)data;
    while (count-- != 0u)
    {
        hash ^= *bytes++;
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t hash_config(const pe_solver_config_t *config)
{
    uint64_t hash = 1469598103934665603ull;
#define HASH_FIELD(field) hash = hash_bytes(hash, &(field), sizeof(field))
    HASH_FIELD(config->algorithm.preset);
    HASH_FIELD(config->algorithm.traversal);
    HASH_FIELD(config->algorithm.regret);
    HASH_FIELD(config->algorithm.policy);
    HASH_FIELD(config->algorithm.averaging);
    HASH_FIELD(config->algorithm.pruning);
    HASH_FIELD(config->algorithm.dcfr_alpha);
    HASH_FIELD(config->algorithm.dcfr_beta);
    HASH_FIELD(config->algorithm.dcfr_gamma);
    HASH_FIELD(config->algorithm.exponential_lambda);
    HASH_FIELD(config->algorithm.averaging_delay);
    HASH_FIELD(config->algorithm.outcome_epsilon);
    HASH_FIELD(config->execution.backend);
    HASH_FIELD(config->execution.stages);
    HASH_FIELD(config->execution.precision);
    HASH_FIELD(config->execution.cpu_threads);
    HASH_FIELD(config->execution.deterministic);
    HASH_FIELD(config->execution.sample_batch_size);
    HASH_FIELD(config->execution.terminal_batch_size);
    HASH_FIELD(config->execution.update_batch_size);
    HASH_FIELD(config->execution.max_ram_bytes);
    HASH_FIELD(config->problem.expected_infosets);
    HASH_FIELD(config->problem.expected_actions);
    HASH_FIELD(config->problem.expected_combos);
    HASH_FIELD(config->seed);
    HASH_FIELD(config->max_iterations);
    HASH_FIELD(config->target_exploitability_mbb);
    HASH_FIELD(config->exploitability_interval);
#undef HASH_FIELD
    return hash;
}

static void free_entries(checkpoint_entry_t *entries, size_t count)
{
    size_t i;
    if (!entries)
        return;
    for (i = 0u; i < count; ++i)
    {
        unsigned which;
        for (which = 0u; which < PE_VALUES_COUNT; ++which)
            free(entries[i].values[which]);
    }
    free(entries);
}

static int checkpoint_save(void *self, const pe_persist_target_t *target,
                           const pe_solver_config_t *config,
                           const pe_storage_ops_t *storage, void *storage_self,
                           uint64_t iteration)
{
    FILE *file;
    size_t count;
    size_t id;
    uint64_t hash;
    (void)self;

    if (!target || !target->path || !*target->path || !config ||
        !storage || !storage_self || !storage->count || !storage->key_at ||
        !storage->shape || !storage->values_const)
        return -1;
    count = storage->count(storage_self);
    if (count > PE_CHECKPOINT_MAX_ENTRIES)
        return -1;
    file = fopen(target->path, "wb");
    if (!file)
        return -1;
    hash = hash_config(config);
    if (write_bytes(file, PE_CHECKPOINT_MAGIC, 8u) != 0 ||
        write_u32(file, PE_CHECKPOINT_VERSION) != 0 ||
        write_u32(file, PE_CHECKPOINT_ENDIAN) != 0 ||
        write_u64(file, iteration) != 0 ||
        write_u64(file, (uint64_t)count) != 0 ||
        write_u64(file, hash) != 0 ||
        write_u64(file, 0u) != 0)
        goto fail;

    for (id = 0u; id < count; ++id)
    {
        uint64_t key;
        uint16_t actions;
        uint16_t combos;
        int8_t street;
        uint8_t array_mask = 0u;
        uint8_t metadata_flags = 0u;
        uint8_t which;
        size_t slots;
        if (storage->key_at(storage_self, (pe_infoset_id_t)id, &key) != 0 ||
            storage->shape(storage_self, (pe_infoset_id_t)id, &actions,
                           &combos, &street) != 0 || actions == 0u || combos == 0u)
            goto fail;
        slots = (size_t)actions * (size_t)combos;
        for (which = 0u; which < PE_VALUES_COUNT; ++which)
            if (storage->values_const(storage_self, (pe_infoset_id_t)id,
                                      (pe_value_array_t)which, NULL) != NULL)
                array_mask |= (uint8_t)(1u << which);
        if (write_u64(file, key) != 0 || write_u16(file, actions) != 0 ||
            write_u16(file, combos) != 0 || write_bytes(file, &street, 1u) != 0 ||
            write_bytes(file, &array_mask, 1u) != 0 || write_u16(file, 0u) != 0 ||
            write_u64(file, (uint64_t)slots) != 0)
            goto fail;
        if (storage->get_flags &&
            storage->get_flags(storage_self, (pe_infoset_id_t)id,
                               &metadata_flags) != 0)
            goto fail;
        /* The flags byte in the record is written above as an array mask; the
           metadata flags follow it so old readers never confuse the two. */
        if (write_bytes(file, &metadata_flags, 1u) != 0)
            goto fail;
        for (which = 0u; which < PE_VALUES_COUNT; ++which)
        {
            const double *values = storage->values_const(
                storage_self, (pe_infoset_id_t)id, (pe_value_array_t)which, NULL);
            size_t slot;
            if ((array_mask & (uint8_t)(1u << which)) == 0u)
                continue;
            if (!values)
                goto fail;
            for (slot = 0u; slot < slots; ++slot)
                if (!isfinite(values[slot]) || write_double(file, values[slot]) != 0)
                    goto fail;
        }
    }
    if (fclose(file) != 0)
        return -1;
    return 0;
fail:
    fclose(file);
    return -1;
}

static int checkpoint_load(void *self, const pe_persist_source_t *source,
                           const pe_solver_config_t *config,
                           const pe_storage_ops_t *storage, void *storage_self,
                           uint64_t *out_iteration)
{
    FILE *file;
    char magic[8];
    uint32_t version;
    uint32_t endian;
    uint64_t iteration;
    uint64_t count64;
    uint64_t stored_hash;
    uint64_t reserved;
    checkpoint_entry_t *entries = NULL;
    size_t count = 0u;
    size_t i;
    (void)self;

    if (!source || !source->path || !*source->path || !config ||
        !storage || !storage_self || !storage->resolve || !storage->shape ||
        !storage->values || !storage->set_flags)
        return -1;
    file = fopen(source->path, "rb");
    if (!file)
        return -1;
    if (read_bytes(file, magic, sizeof(magic)) != 0 ||
        read_u32(file, &version) != 0 || read_u32(file, &endian) != 0 ||
        read_u64(file, &iteration) != 0 || read_u64(file, &count64) != 0 ||
        read_u64(file, &stored_hash) != 0 || read_u64(file, &reserved) != 0 ||
        memcmp(magic, PE_CHECKPOINT_MAGIC, sizeof(magic)) != 0 ||
        version != PE_CHECKPOINT_VERSION || endian != PE_CHECKPOINT_ENDIAN ||
        reserved != 0u || stored_hash != hash_config(config) ||
        count64 > PE_CHECKPOINT_MAX_ENTRIES || count64 > SIZE_MAX)
        goto fail;
    count = (size_t)count64;
    entries = (checkpoint_entry_t *)calloc(count, sizeof(*entries));
    if (count != 0u && !entries)
        goto fail;

    for (i = 0u; i < count; ++i)
    {
        uint16_t reserved16;
        uint8_t array_mask;
        uint8_t metadata_flags;
        uint8_t which;
        size_t slot;
        if (read_u64(file, &entries[i].key) != 0 ||
            read_u16(file, &entries[i].actions) != 0 ||
            read_u16(file, &entries[i].combos) != 0 ||
            read_bytes(file, &entries[i].street, 1u) != 0 ||
            read_bytes(file, &array_mask, 1u) != 0 ||
            read_u16(file, &reserved16) != 0 || read_u64(file, &count64) != 0 ||
            read_bytes(file, &metadata_flags, 1u) != 0 ||
            reserved16 != 0u || entries[i].actions == 0u ||
            entries[i].combos == 0u ||
            count64 != (uint64_t)entries[i].actions * entries[i].combos ||
            array_mask == 0u)
            goto fail;
        entries[i].present = array_mask;
        entries[i].flags = metadata_flags;
        entries[i].slots = (size_t)count64;
        for (which = 0u; which < PE_VALUES_COUNT; ++which)
        {
            if ((array_mask & (uint8_t)(1u << which)) == 0u)
                continue;
            entries[i].values[which] = (double *)malloc(
                entries[i].slots * sizeof(double));
            if (!entries[i].values[which])
                goto fail;
            for (slot = 0u; slot < entries[i].slots; ++slot)
                if (read_double(file, &entries[i].values[which][slot]) != 0 ||
                    !isfinite(entries[i].values[which][slot]))
                    goto fail;
        }
    }
    if (fclose(file) != 0)
    {
        file = NULL;
        goto fail;
    }
    for (i = 0u; i < count; ++i)
    {
        pe_infoset_id_t id = storage->resolve(
            storage_self, entries[i].key, entries[i].actions,
            entries[i].combos, entries[i].street);
        uint16_t stored_actions;
        uint16_t stored_combos;
        uint8_t which;
        if (id == PE_INFOSET_ID_INVALID ||
            storage->shape(storage_self, id, &stored_actions,
                           &stored_combos, NULL) != 0 ||
            stored_actions != entries[i].actions ||
            stored_combos != entries[i].combos ||
            storage->set_flags(storage_self, id, entries[i].flags, 0u) != 0)
            goto apply_fail;
        for (which = 0u; which < PE_VALUES_COUNT; ++which)
        {
            double *values;
            if ((entries[i].present & (uint8_t)(1u << which)) == 0u)
                continue;
            values = storage->values(storage_self, id,
                                     (pe_value_array_t)which, NULL);
            if (!values)
                goto apply_fail;
            memcpy(values, entries[i].values[which],
                   entries[i].slots * sizeof(double));
        }
    }
    free_entries(entries, count);
    if (out_iteration)
        *out_iteration = iteration;
    return 0;
apply_fail:
    free_entries(entries, count);
    return -1;
fail:
    if (file)
        fclose(file);
    free_entries(entries, count);
    return -1;
}

const pe_persist_ops_t *pe_persist_checkpoint_ops(void)
{
    static const pe_persist_ops_t ops = {
        "checkpoint-v2",
        checkpoint_save,
        checkpoint_load
    };
    return &ops;
}

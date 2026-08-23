/*
 * monker_tree_reader.c - MonkerSolver .tree header reader (MKR-01)
 */

#include <poker_eval/solver/pe_monker.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/engine/solvers/cfr/mpf_tree.h>

#define PE_MONKER_MAX_NODES 1000000u
#define PE_MONKER_MAX_DEPTH 1024u

static int read_bytes(FILE *file, unsigned char *out, size_t count)
{
    return fread(out, 1u, count, file) == count ? 0 : -1;
}

static int64_t decode_i64(const unsigned char *bytes)
{
    uint64_t value = 0u;
    unsigned i;

    for (i = 0u; i < 8u; ++i)
        value |= (uint64_t)bytes[i] << (8u * i);
    return (int64_t)value;
}

static int32_t decode_i32(const unsigned char *bytes)
{
    uint32_t value = 0u;
    unsigned i;

    for (i = 0u; i < 4u; ++i)
        value |= (uint32_t)bytes[i] << (8u * i);
    return (int32_t)value;
}

static double decode_f64(const unsigned char *bytes)
{
    uint64_t bits = (uint64_t)decode_i64(bytes);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int read_i64(FILE *file, int64_t *out)
{
    unsigned char bytes[8];
    if (read_bytes(file, bytes, sizeof(bytes)) != 0)
        return -1;
    *out = decode_i64(bytes);
    return 0;
}

static int read_i32(FILE *file, int32_t *out)
{
    unsigned char bytes[4];
    if (read_bytes(file, bytes, sizeof(bytes)) != 0)
        return -1;
    *out = decode_i32(bytes);
    return 0;
}

static int read_f64(FILE *file, double *out)
{
    unsigned char bytes[8];
    if (read_bytes(file, bytes, sizeof(bytes)) != 0)
        return -1;
    *out = decode_f64(bytes);
    return 0;
}

static int valid_f64(double value)
{
    return value == value && value > -1.0e300 && value < 1.0e300;
}

pe_monker_status_t pe_monker_tree_read_header(
    const char *path, pe_monker_tree_header_t *out)
{
    FILE *file;
    int64_t signature;
    int32_t internal_format;
    int32_t player_count;
    int32_t first_to_act;
    int32_t street;
    int i;

    if (!path || !out)
        return PE_MONKER_ERR_NULL_ARGUMENT;

    file = fopen(path, "rb");
    if (!file)
        return PE_MONKER_ERR_OPEN;

    memset(out, 0, sizeof(*out));
    if (read_i64(file, &signature) != 0 ||
        read_i32(file, &internal_format) != 0 ||
        read_i32(file, &player_count) != 0 ||
        read_i32(file, &first_to_act) != 0 ||
        read_i32(file, &street) != 0)
    {
        fclose(file);
        return PE_MONKER_ERR_TRUNCATED;
    }

    if (signature < 33486 || signature > 33490)
    {
        fclose(file);
        return PE_MONKER_ERR_BAD_SIGNATURE;
    }
    if (player_count < 1 || player_count > (int32_t)PE_MONKER_MAX_PLAYERS ||
        street < 0 || street > 4 ||
        first_to_act < -1 || first_to_act >= player_count)
    {
        fclose(file);
        return PE_MONKER_ERR_INVALID_HEADER;
    }

    out->signature = signature;
    out->internal_format = internal_format;
    out->player_count = (uint32_t)player_count;
    out->first_to_act = first_to_act;
    out->street = street;

    if (street == 0)
    {
        for (i = 0; i < player_count; ++i)
            if (read_f64(file, &out->committed[i]) != 0 ||
                !valid_f64(out->committed[i]))
            {
                fclose(file);
                return PE_MONKER_ERR_TRUNCATED;
            }
    }
    if (read_f64(file, &out->dead_money) != 0 || !valid_f64(out->dead_money))
    {
        fclose(file);
        return PE_MONKER_ERR_TRUNCATED;
    }
    for (i = 0; i < player_count; ++i)
        if (read_f64(file, &out->stacks[i]) != 0 || !valid_f64(out->stacks[i]))
        {
            fclose(file);
            return PE_MONKER_ERR_TRUNCATED;
        }

    fclose(file);
    return PE_MONKER_OK;
}

const char *pe_monker_status_string(pe_monker_status_t status)
{
    switch (status)
    {
    case PE_MONKER_OK: return "ok";
    case PE_MONKER_ERR_NULL_ARGUMENT: return "null argument";
    case PE_MONKER_ERR_OPEN: return "cannot open file";
    case PE_MONKER_ERR_IO: return "I/O error";
    case PE_MONKER_ERR_TRUNCATED: return "truncated header";
    case PE_MONKER_ERR_BAD_SIGNATURE: return "unsupported signature";
    case PE_MONKER_ERR_INVALID_HEADER: return "invalid header";
    case PE_MONKER_ERR_INVALID_ACTION: return "invalid action code";
    case PE_MONKER_ERR_INVALID_TOPOLOGY: return "invalid node topology";
    case PE_MONKER_ERR_DEPTH_LIMIT: return "node depth limit exceeded";
    case PE_MONKER_ERR_TOO_LARGE: return "tree is too large";
    default: return "unknown Monker error";
    }
}

typedef struct
{
    unsigned char incoming_action;
    unsigned char child_slot;
    unsigned char child_count;
    int parent;
} pe_monker_node_record_t;

typedef struct
{
    pe_monker_node_record_t *items;
    size_t count;
    size_t capacity;
} pe_monker_node_records_t;

static int append_record(pe_monker_node_records_t *records,
                         pe_monker_node_record_t record,
                         size_t *out_index)
{
    pe_monker_node_record_t *grown;
    size_t capacity;

    if (records->count >= PE_MONKER_MAX_NODES)
        return -2;
    if (records->count == records->capacity)
    {
        capacity = records->capacity == 0u ? 64u : records->capacity * 2u;
        if (capacity > PE_MONKER_MAX_NODES)
            capacity = PE_MONKER_MAX_NODES;
        grown = (pe_monker_node_record_t *)realloc(
            records->items, capacity * sizeof(*grown));
        if (!grown)
            return -1;
        records->items = grown;
        records->capacity = capacity;
    }
    *out_index = records->count;
    records->items[records->count++] = record;
    return 0;
}

static int parse_node_stream(const unsigned char *bytes, size_t length,
                             size_t *offset, int parent, unsigned depth,
                             unsigned child_slot,
                             pe_monker_node_records_t *records)
{
    pe_monker_node_record_t record;
    size_t index;
    unsigned child;
    int rc;

    if (depth > PE_MONKER_MAX_DEPTH)
        return -3;
    if (length - *offset < 2u)
        return -4;

    record.incoming_action = bytes[(*offset)++];
    record.child_count = bytes[(*offset)++];
    record.child_slot = (unsigned char)child_slot;
    record.parent = parent;
    if (record.child_count > MPF_TREE_ACTION_MAX)
        return -5;
    rc = append_record(records, record, &index);
    if (rc != 0)
        return rc;

    for (child = 0u; child < record.child_count; ++child)
    {
        rc = parse_node_stream(bytes, length, offset, (int)index,
                               depth + 1u, child, records);
        if (rc != 0)
            return rc;
    }
    return 0;
}

static char *copy_node_id(size_t index)
{
    char buffer[32];
    int length = snprintf(buffer, sizeof(buffer), "mkr_node_%zu", index);
    char *out;

    if (length < 0 || (size_t)length >= sizeof(buffer))
        return NULL;
    out = (char *)malloc((size_t)length + 1u);
    if (!out)
        return NULL;
    memcpy(out, buffer, (size_t)length + 1u);
    return out;
}

static int decode_action_code(unsigned code, mpf_tree_action_type_t *out_type)
{
    if (code == 0u)
        *out_type = MPF_TREE_ACTION_FOLD;
    else if (code == 1u)
        *out_type = MPF_TREE_ACTION_CALL;
    else if ((code >= 2u && code <= 7u) || code == 9u || code == 10u ||
             code >= 40000u)
        *out_type = MPF_TREE_ACTION_RAISE;
    else
        return -1;
    return 0;
}

static double action_size(unsigned code)
{
    if (code >= 40000u)
        return (double)(code - 40000u) / 100.0;
    switch (code)
    {
    case 2u: return 1.0;
    case 3u: return 2.0;
    case 4u: return 0.5;
    case 5u: return -1.0;
    case 6u: return 1.0;
    case 7u: return 0.25;
    case 9u: return 0.75;
    case 10u: return 0.0;
    default: return 0.0;
    }
}

static void discard_tree(mpf_tree_def_t *tree, size_t initialized)
{
    size_t i;

    if (!tree)
        return;
    if (tree->nodes)
    {
        for (i = 0u; i < initialized; ++i)
        {
            size_t j;
            free(tree->nodes[i].id);
            free(tree->nodes[i].bet_sizes);
            for (j = 0u; j < (size_t)tree->nodes[i].action_count; ++j)
                free(tree->nodes[i].actions[j].next_id);
            free(tree->nodes[i].actions);
            free(tree->nodes[i].locked_strategy);
#if !defined(_WIN32)
            pthread_mutex_destroy(&tree->nodes[i].cache_lock);
#endif
        }
        free(tree->nodes);
    }
    free(tree->root_id);
    free(tree);
}

static int skip_header(FILE *file, const pe_monker_tree_header_t *header)
{
    uint64_t bytes = 8u + 4u * 4u + 8u +
                     8u * (uint64_t)header->player_count;
    if (header->street == 0)
        bytes += 8u * (uint64_t)header->player_count;
    return fseek(file, (long)bytes, SEEK_SET) == 0 ? 0 : -1;
}

pe_monker_status_t pe_monker_tree_load(const char *path,
                                       mpf_tree_def_t **out_tree)
{
    pe_monker_tree_header_t header;
    pe_monker_node_records_t records = {0};
    mpf_tree_def_t *tree = NULL;
    FILE *file = NULL;
    unsigned char *bytes = NULL;
    long file_size;
    size_t payload_length;
    size_t offset = 0u;
    size_t i;
    int parse_rc;
    pe_monker_status_t header_status;

    if (!path || !out_tree)
        return PE_MONKER_ERR_NULL_ARGUMENT;
    *out_tree = NULL;

    header_status = pe_monker_tree_read_header(path, &header);
    if (header_status != PE_MONKER_OK)
        return header_status;
    file = fopen(path, "rb");
    if (!file)
        return PE_MONKER_ERR_OPEN;
    if (skip_header(file, &header) != 0 || fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return PE_MONKER_ERR_IO;
    }
    file_size = ftell(file);
    if (file_size < 0)
    {
        fclose(file);
        return PE_MONKER_ERR_IO;
    }
    if ((uint64_t)file_size < 8u + 4u * 4u + 8u +
                             8u * (uint64_t)header.player_count +
                             (header.street == 0 ?
                              8u * (uint64_t)header.player_count : 0u))
    {
        fclose(file);
        return PE_MONKER_ERR_TRUNCATED;
    }
    if (skip_header(file, &header) != 0)
    {
        fclose(file);
        return PE_MONKER_ERR_IO;
    }
    payload_length = (size_t)file_size - (size_t)ftell(file);
    bytes = (unsigned char *)malloc(payload_length == 0u ? 1u : payload_length);
    if (!bytes || fread(bytes, 1u, payload_length, file) != payload_length)
    {
        free(bytes);
        fclose(file);
        return PE_MONKER_ERR_TRUNCATED;
    }
    fclose(file);

    if (payload_length == 0u)
        parse_rc = -4;
    else
        parse_rc = parse_node_stream(bytes, payload_length, &offset,
                                     -1, 0u, 0u, &records);
    free(bytes);
    if (parse_rc != 0 || offset != payload_length)
    {
        free(records.items);
        if (parse_rc == -2 || parse_rc == -1)
            return parse_rc == -2 ? PE_MONKER_ERR_TOO_LARGE : PE_MONKER_ERR_IO;
        if (parse_rc == -3)
            return PE_MONKER_ERR_DEPTH_LIMIT;
        if (parse_rc == -5)
            return PE_MONKER_ERR_INVALID_TOPOLOGY;
        if (parse_rc == -4)
            return PE_MONKER_ERR_TRUNCATED;
        return PE_MONKER_ERR_INVALID_TOPOLOGY;
    }

    tree = (mpf_tree_def_t *)calloc(1u, sizeof(*tree));
    if (!tree)
    {
        free(records.items);
        return PE_MONKER_ERR_IO;
    }
    tree->version = header.internal_format;
    tree->node_count = (int)records.count;
    tree->root_index = 0;
    tree->nodes = (mpf_tree_node_t *)calloc(records.count, sizeof(*tree->nodes));
    if (!tree->nodes)
    {
        free(records.items);
        free(tree);
        return PE_MONKER_ERR_IO;
    }
    for (i = 0u; i < records.count; ++i)
    {
        mpf_tree_node_t *node = &tree->nodes[i];
        unsigned action;
        unsigned child;

#if !defined(_WIN32)
        if (pthread_mutex_init(&node->cache_lock, NULL) != 0)
        {
            discard_tree(tree, i);
            free(records.items);
            return PE_MONKER_ERR_IO;
        }
#endif
        node->id = copy_node_id(i);
        node->street = (mpf_street_t)header.street;
        node->acting_player = header.first_to_act >= 0 ? header.first_to_act : 0;
        node->type = records.items[i].child_count == 0u
                         ? MPF_TREE_NODE_TERMINAL : MPF_TREE_NODE_PLAYER;
        node->action_count = records.items[i].child_count;
        if (!node->id)
        {
            discard_tree(tree, i + 1u);
            free(records.items);
            return PE_MONKER_ERR_IO;
        }
        if (node->action_count == 0)
            continue;
        node->actions = (mpf_tree_action_t *)calloc(
            (size_t)node->action_count, sizeof(*node->actions));
        if (!node->actions)
        {
            discard_tree(tree, i + 1u);
            free(records.items);
            return PE_MONKER_ERR_IO;
        }
        for (child = 0u; child < records.count; ++child)
        {
            if (records.items[child].parent != (int)i)
                continue;
            action = records.items[child].incoming_action;
            if (decode_action_code(action, &node->actions[
                    records.items[child].child_slot].type) != 0)
            {
                discard_tree(tree, i + 1u);
                free(records.items);
                return PE_MONKER_ERR_INVALID_ACTION;
            }
            node->actions[records.items[child].child_slot].next_index = (int)child;
            node->actions[records.items[child].child_slot].lock_freq = -1.0;
            if (node->actions[records.items[child].child_slot].type ==
                MPF_TREE_ACTION_RAISE)
            {
                int size_index = node->bet_size_count;
                double *grown = (double *)realloc(
                    node->bet_sizes, (size_t)(size_index + 1) * sizeof(*grown));
                if (!grown)
                {
                    discard_tree(tree, i + 1u);
                    free(records.items);
                    return PE_MONKER_ERR_IO;
                }
                node->bet_sizes = grown;
                node->bet_sizes[size_index] = action_size(action);
                node->bet_size_count++;
                node->actions[records.items[child].child_slot].size_index = size_index;
                if (action == 10u)
                    node->use_pot_sizing = 1;
            }
        }
    }
    tree->root_id = copy_node_id(0u);
    if (!tree->root_id)
    {
        discard_tree(tree, records.count);
        free(records.items);
        return PE_MONKER_ERR_IO;
    }
    free(records.items);
    *out_tree = tree;
    return PE_MONKER_OK;
}

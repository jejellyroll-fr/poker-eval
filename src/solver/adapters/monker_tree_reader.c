/*
 * monker_tree_reader.c - .tree header reader (MKR-01)
 */

#include <poker_eval/solver/pe_monker.h>

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <poker_eval/engine/solvers/cfr/mpf_tree.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/solver/pe_combinations.h>

#include "../domain/finite_double.h"

#define PE_MONKER_MAX_NODES 1000000u
#define PE_MONKER_MAX_DEPTH 1024u

pe_monker_status_t pe_monker_combo_layout_from_count(
    uint32_t combo_count, pe_monker_combo_layout_t *out)
{
    if (!out)
        return PE_MONKER_ERR_NULL_ARGUMENT;
    switch (combo_count)
    {
    case 1326u:
        out->game = game_holdem;
        out->hole_cards = 2u;
        break;
    case 270725u:
        out->game = game_omaha;
        out->hole_cards = 4u;
        break;
    case 2598960u:
        out->game = game_omaha5;
        out->hole_cards = 5u;
        break;
    case 20358520u:
        out->game = game_omaha6;
        out->hole_cards = 6u;
        break;
    default:
        return PE_MONKER_ERR_INVALID_HEADER;
    }
    out->combo_count = combo_count;
    return PE_MONKER_OK;
}

static int read_bytes(FILE *file, unsigned char *out, size_t count)
{
    return fread(out, 1u, count, file) == count ? 0 : -1;
}

/*
 * The .tree format uses java.io.DataOutputStream-compatible big-endian fields.
 * The byte order is a property of the format, not of the host machine, so
 * these decoders are fixed big-endian rather than host-order.
 */
static int64_t decode_i64(const unsigned char *bytes)
{
    uint64_t value = 0u;
    unsigned i;

    for (i = 0u; i < 8u; ++i)
        value = (value << 8u) | (uint64_t)bytes[i];
    return (int64_t)value;
}

static int32_t decode_i32(const unsigned char *bytes)
{
    uint32_t value = 0u;
    unsigned i;

    for (i = 0u; i < 4u; ++i)
        value = (value << 8u) | (uint32_t)bytes[i];
    return (int32_t)value;
}

/* Java `char` is a 16-bit UTF-16 code unit, and DataOutputStream.writeChar
   emits it as two big-endian bytes. The node stream is written with writeChar,
   which is why an action code such as 40100 fits in it at all. */
static unsigned decode_u16(const unsigned char *bytes)
{
    return ((unsigned)bytes[0] << 8u) | (unsigned)bytes[1];
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


/*
 * Committed, dead money and stacks are int32 in the file. They are surfaced as
 * double because they are money, but the value stored is the raw file integer:
 * nothing in the format states its scale, and the observed magnitudes (a 4000
 * dead pot against 200000 stacks) are consistent with both hundredths and raw
 * chips. Guessing a divisor here would silently rescale every imported tree.
 */
static int read_money(FILE *file, double *out)
{
    int32_t raw;
    if (read_i32(file, &raw) != 0)
        return -1;
    *out = (double)raw;
    return 0;
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
            if (read_money(file, &out->committed[i]) != 0)
            {
                fclose(file);
                return PE_MONKER_ERR_TRUNCATED;
            }
    }
    if (read_money(file, &out->dead_money) != 0)
    {
        fclose(file);
        return PE_MONKER_ERR_TRUNCATED;
    }
    for (i = 0; i < player_count; ++i)
        if (read_money(file, &out->stacks[i]) != 0)
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

/* The root carries no incoming action; this marks the field as absent so a
   sentinel can never be mistaken for a real code. */
#define PE_MONKER_NO_ACTION 0xFFFFFFFFu

typedef struct
{
    unsigned incoming_action;
    unsigned child_count;
    unsigned char child_slot;
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

/*
 * The node stream is preorder. Each node writes its child count as a Java
 * `char` — two big-endian bytes — and each *edge* writes its action code the
 * same way, immediately before the child it leads to. The root has no edge
 * into it and therefore no action code: reading one for the root would shift
 * the whole stream by two bytes and still parse, which is why the caller
 * passes PE_MONKER_NO_ACTION rather than letting this function guess.
 */
static int parse_node_stream(const unsigned char *bytes, size_t length,
                             size_t *offset, unsigned incoming_action,
                             int parent, unsigned depth, unsigned child_slot,
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

    record.incoming_action = incoming_action;
    record.child_count = decode_u16(bytes + *offset);
    *offset += 2u;
    record.child_slot = (unsigned char)child_slot;
    record.parent = parent;
    if (record.child_count > MPF_TREE_ACTION_MAX)
        return -5;
    rc = append_record(records, record, &index);
    if (rc != 0)
        return rc;

    for (child = 0u; child < record.child_count; ++child)
    {
        unsigned action;
        if (length - *offset < 2u)
            return -4;
        action = decode_u16(bytes + *offset);
        *offset += 2u;
        rc = parse_node_stream(bytes, length, offset, action, (int)index,
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
    for (int i = 0; i <= length; ++i)
        out[i] = buffer[i];
    return out;
}

static int action_size(unsigned code, double *out_size);

static int decode_action_code(unsigned code, mpf_tree_action_type_t *out_type)
{
    double ignored;
    if (code == 0u)
        *out_type = MPF_TREE_ACTION_FOLD;
    else if (code == 1u)
        *out_type = MPF_TREE_ACTION_CALL;
    else if (action_size(code, &ignored) == 0)
        *out_type = MPF_TREE_ACTION_RAISE;
    else
        return -1;
    return 0;
}

/*
 * Bet sizes carried by an action code.
 *
 * Only three families are evidenced. Codes at or above 40000 encode a pot
 * percentage, covered by the serialized action definitions
 * (a "75%" sizing writes 40075) and by the codes present in compatible trees
 * (40050, 40075, 40100). Code 5 is the minimum raise, from the same file
 * ("min" writes 5). Code 3 is all-in: the GG all-in-or-fold tree contains no
 * action code other than 0, 1 and 3, and by construction offers nothing but
 * folding and shoving.
 *
 * Every other small code is a guess, and a guess here does not fail — it
 * produces a tree that solves a game nobody asked for. They are rejected
 * instead, so a file that uses one is a named error rather than a wrong
 * answer. None of the shipped trees reaches this path.
 */
#define PE_MONKER_SIZE_ALL_IN (-1.0)
#define PE_MONKER_SIZE_MIN_RAISE (-2.0)

static int action_size(unsigned code, double *out_size)
{
    if (code >= 40000u)
    {
        *out_size = (double)(code - 40000u) / 100.0;
        return 0;
    }
    if (code == 3u)
    {
        *out_size = PE_MONKER_SIZE_ALL_IN;
        return 0;
    }
    if (code == 5u)
    {
        *out_size = PE_MONKER_SIZE_MIN_RAISE;
        return 0;
    }
    return -1;
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

/*
 * int64 signature, four int32 (format, players, first to act, street), then
 * int32 committed per player only at street 0, int32 dead money, and int32 per
 * player of stacks. 36 bytes for a two-handed postflop tree.
 */
static pe_monker_status_t load_payload(const char *path,
                                       const pe_monker_tree_header_t *header,
                                       unsigned char **out_bytes,
                                       size_t *out_length);

static int skip_header(FILE *file, const pe_monker_tree_header_t *header)
{
    uint64_t bytes = 8u + 4u * 4u + 4u +
                     4u * (uint64_t)header->player_count;
    if (header->street == 0)
        bytes += 4u * (uint64_t)header->player_count;
    return fseek(file, (long)bytes, SEEK_SET) == 0 ? 0 : -1;
}

pe_monker_status_t pe_monker_tree_load(const char *path,
                                       mpf_tree_def_t **out_tree)
{
    pe_monker_tree_header_t header;
    pe_monker_node_records_t records = {0};
    mpf_tree_def_t *tree = NULL;
    unsigned char *bytes = NULL;
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
    /* The header arithmetic lives in exactly one place. It used to be written
       out a second time here, which is how the two copies could disagree. */
    header_status = load_payload(path, &header, &bytes, &payload_length);
    if (header_status != PE_MONKER_OK)
        return header_status;

    if (payload_length == 0u)
        parse_rc = -4;
    else
        parse_rc = parse_node_stream(bytes, payload_length, &offset,
                                     PE_MONKER_NO_ACTION, -1, 0u, 0u,
                                     &records);
    /*
     * The node stream is followed by a one-byte "ranges present" flag, and by
     * the range block itself when that flag is set. Requiring the nodes to
     * consume the payload exactly rejected every real file; ignoring the tail
     * entirely would accept a stream that stopped early. What is checked is
     * that the tail is precisely the trailer the format defines.
     */
    if (parse_rc == 0)
    {
        size_t remaining = payload_length - offset;
        unsigned flag;
        if (remaining < 1u)
            parse_rc = -4;
        else
        {
            flag = bytes[offset];
            if (flag > 1u)
                parse_rc = -6;
            else if (flag == 0u && remaining != 1u)
                parse_rc = -6;
            else if (flag == 1u &&
                     ((remaining - 1u) == 0u ||
                      (remaining - 1u) % ((size_t)header.player_count * 4u) != 0u))
                parse_rc = -6;
        }
    }
    free(bytes);
    if (parse_rc != 0)
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
        node->type = records.items[i].child_count == 0u
                         ? MPF_TREE_NODE_TERMINAL : MPF_TREE_NODE_PLAYER;
        if (node->type == MPF_TREE_NODE_TERMINAL)
            node->acting_player = -1;
        else if (i == 0u)
            node->acting_player =
                header.first_to_act >= 0 ? header.first_to_act : 0;
        else
        {
            int parent = records.items[i].parent;
            int parent_player = parent >= 0
                                    ? tree->nodes[parent].acting_player
                                    : -1;
            /* The binary Monker tree stores one betting street. A child
             * after a player action belongs to the next player; public-card
             * transitions are supplied by the surrounding street game. */
            node->acting_player =
                parent_player >= 0
                    ? (parent_player + 1) % (int)header.player_count
                    : (header.first_to_act >= 0 ? header.first_to_act : 0);
        }
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
                if (action >= 40000u)
                    node->use_pot_sizing = 1;
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
                if (action_size(action, &node->bet_sizes[size_index]) != 0)
                {
                    discard_tree(tree, i + 1u);
                    free(records.items);
                    return PE_MONKER_ERR_INVALID_ACTION;
                }
                node->bet_size_count++;
                node->actions[records.items[child].child_slot].size_index = size_index;
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

static pe_monker_status_t load_payload(const char *path,
                                       const pe_monker_tree_header_t *header,
                                       unsigned char **out_bytes,
                                       size_t *out_length)
{
    FILE *file;
    long file_size;
    long payload_start;
    size_t payload_length;
    unsigned char *bytes;

    file = fopen(path, "rb");
    if (!file)
        return PE_MONKER_ERR_OPEN;
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return PE_MONKER_ERR_IO;
    }
    file_size = ftell(file);
    if (file_size < 0 || skip_header(file, header) != 0)
    {
        fclose(file);
        return PE_MONKER_ERR_IO;
    }
    payload_start = ftell(file);
    if (payload_start < 0 || file_size < payload_start)
    {
        fclose(file);
        return PE_MONKER_ERR_TRUNCATED;
    }
    payload_length = (size_t)(file_size - payload_start);
    bytes = (unsigned char *)malloc(payload_length == 0u ? 1u : payload_length);
    if (!bytes || fread(bytes, 1u, payload_length, file) != payload_length)
    {
        free(bytes);
        fclose(file);
        return PE_MONKER_ERR_TRUNCATED;
    }
    fclose(file);
    *out_bytes = bytes;
    *out_length = payload_length;
    return PE_MONKER_OK;
}

static pe_range_t *allocate_fixed_range(uint32_t combo_count,
                                        unsigned combo_cards)
{
    pe_monker_combo_layout_t layout;
    pe_range_t *range = (pe_range_t *)calloc(1u, sizeof(*range));
    if (!range)
        return NULL;
    if (pe_monker_combo_layout_from_count(combo_count, &layout) !=
            PE_MONKER_OK || layout.hole_cards != combo_cards)
    {
        free(range);
        return NULL;
    }
    range->game_type = layout.game;
    range->capacity = combo_count;
    range->count = combo_count;
    range->combos = (pe_combo_t *)calloc(combo_count, sizeof(*range->combos));
    if (!range->combos)
    {
        free(range);
        return NULL;
    }
    return range;
}

void pe_monker_range_set_free(pe_monker_range_set_t *ranges)
{
    uint32_t player;

    if (!ranges)
        return;
    for (player = 0u; player < ranges->player_count; ++player)
        pe_range_free(ranges->players ? ranges->players[player] : NULL);
    free(ranges->players);
    memset(ranges, 0, sizeof(*ranges));
}

pe_monker_status_t pe_monker_tree_read_ranges(const char *path,
                                              pe_monker_range_set_t *out)
{
    pe_monker_tree_header_t header;
    pe_monker_node_records_t records = {0};
    unsigned char *bytes = NULL;
    size_t length = 0u;
    size_t offset = 0u;
    uint32_t combo_count;
    uint32_t player;
    unsigned combo_cards;
    int32_t present;
    int parse_rc;
    pe_monker_status_t status;

    if (!path || !out)
        return PE_MONKER_ERR_NULL_ARGUMENT;
    memset(out, 0, sizeof(*out));
    status = pe_monker_tree_read_header(path, &header);
    if (status != PE_MONKER_OK)
        return status;
    status = load_payload(path, &header, &bytes, &length);
    if (status != PE_MONKER_OK)
        return status;
    if (length == 0u)
    {
        free(bytes);
        return PE_MONKER_OK;
    }
    parse_rc = parse_node_stream(bytes, length, &offset, PE_MONKER_NO_ACTION,
                                 -1, 0u, 0u, &records);
    free(records.items);
    if (parse_rc != 0)
    {
        free(bytes);
        if (parse_rc == -3)
            return PE_MONKER_ERR_DEPTH_LIMIT;
        if (parse_rc == -4)
            return PE_MONKER_ERR_TRUNCATED;
        return PE_MONKER_ERR_INVALID_TOPOLOGY;
    }
    /* DataOutputStream.writeBoolean emits a single byte. Reading four here
       consumed the first int32 of the range block whenever ranges were
       present, and rejected every rangeless tree — which is all of them, since
       a rangeless tree ends on exactly this one byte. */
    if (length - offset < 1u)
    {
        free(bytes);
        return PE_MONKER_OK;
    }
    present = (int32_t)bytes[offset];
    offset += 1u;
    if (present == 0)
    {
        status = offset == length ? PE_MONKER_OK : PE_MONKER_ERR_INVALID_HEADER;
        free(bytes);
        return status;
    }
    if (present != 1 || header.player_count == 0u ||
        (length - offset) % ((size_t)header.player_count * sizeof(int32_t)) != 0u)
    {
        free(bytes);
        return PE_MONKER_ERR_INVALID_HEADER;
    }
    combo_count = (uint32_t)((length - offset) /
                             ((size_t)header.player_count * sizeof(int32_t)));
    {
        pe_monker_combo_layout_t layout;
        if (pe_monker_combo_layout_from_count(combo_count, &layout) !=
            PE_MONKER_OK)
        {
            free(bytes);
            return PE_MONKER_ERR_INVALID_HEADER;
        }
        combo_cards = layout.hole_cards;
    }

    out->players = (pe_range_t **)calloc(header.player_count,
                                         sizeof(*out->players));
    if (!out->players)
    {
        free(bytes);
        return PE_MONKER_ERR_IO;
    }
    out->player_count = header.player_count;
    out->combo_count = combo_count;
    for (player = 0u; player < out->player_count; ++player)
    {
        uint32_t combo;
        double total = 0.0;
        pe_range_t *range = allocate_fixed_range(combo_count, combo_cards);
        if (!range)
        {
            free(bytes);
            pe_monker_range_set_free(out);
            return PE_MONKER_ERR_IO;
        }
        out->players[player] = range;
        for (combo = 0u; combo < combo_count; ++combo)
        {
            int32_t fixed = decode_i32(bytes + offset);
            unsigned cards[4];
            size_t index = (size_t)combo;
            offset += sizeof(int32_t);
            if (fixed < 0 ||
                pe_comb_unrank(52u, combo_cards, combo, cards) != PE_SOLVER_OK)
            {
                free(bytes);
                pe_monker_range_set_free(out);
                return PE_MONKER_ERR_INVALID_HEADER;
            }
            StdDeck_CardMask_RESET(range->combos[index].hand);
            for (unsigned card = 0u; card < combo_cards; ++card)
                StdDeck_CardMask_SET(range->combos[index].hand, cards[card]);
            range->combos[index].weight =
                (double)fixed / 2147483647.0;
            total += range->combos[index].weight;
        }
        if (!(total > 0.0) || !pe_finite_double(total))
        {
            free(bytes);
            pe_monker_range_set_free(out);
            return PE_MONKER_ERR_INVALID_HEADER;
        }
        range->total_weight = total;
    }
    free(bytes);
    return PE_MONKER_OK;
}

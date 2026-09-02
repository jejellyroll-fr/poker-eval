/*
 * test_pe_tree_outline.c - the collapsible outline over an edited tree.
 *
 * Three things are worth being sure of and none of them can be seen from a
 * screenshot: that folding hides exactly the subtree under the folded node
 * and nothing else, that a path key identifies a node stably while indices
 * move, and that the chip state at a row is the one the path actually
 * produces. The last is the part a GUI would show as a confident wrong
 * number, so it is checked against arithmetic done by hand.
 */

#include "pe_tree_outline.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition, ...)                                      \
    do                                                             \
    {                                                              \
        if (!(condition))                                          \
        {                                                          \
            fprintf(stderr, "FAILED %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                          \
            fputc('\n', stderr);                                   \
            failures++;                                            \
        }                                                          \
    } while (0)

/* The editor starts every player node with fold / call. Add a pot-raise at
   the root and one more raise below it; the second subtree makes reveal's
   path-only behavior observable even when sibling branches are leaves. */
static void build_tree(pe_tree_editor_t *editor, int *out_raise_child)
{
    int child = -1;
    int nested = -1;
    pe_tree_editor_init(editor, 2, MPF_STREET_PREFLOP);
    CHECK(pe_tree_editor_add_action(editor, 0, MPF_TREE_ACTION_RAISE, 1.0,
                                    &child), "raise could not be added");
    *out_raise_child = child;
    CHECK(pe_tree_editor_add_action(editor, child, MPF_TREE_ACTION_RAISE, 0.5,
                                    &nested), "nested raise could not be added");
}

static void test_expanded_shows_everything(void)
{
    pe_tree_editor_t editor;
    pe_tree_outline_t outline;
    int raise_child = -1;

    build_tree(&editor, &raise_child);
    pe_tree_outline_init(&outline, NULL);
    /* root + two terminal branches + raise + two terminals + nested raise +
       its two terminal branches = 9 */
    CHECK(pe_tree_outline_build(&outline, &editor) == 9,
          "a fully expanded tree shows %d rows, expected 9",
          outline.row_count);
    CHECK(outline.rows[0].depth == 0 && outline.rows[0].parent_row == -1,
          "the root is not at depth 0 with no parent");
    CHECK(outline.truncated == 0, "a six-row tree reported truncation");
}

static void test_folding_hides_only_that_subtree(void)
{
    pe_tree_editor_t editor;
    pe_tree_outline_t outline;
    int raise_child = -1;
    int row;

    build_tree(&editor, &raise_child);
    pe_tree_outline_init(&outline, NULL);
    pe_tree_outline_build(&outline, &editor);
    row = pe_tree_outline_row_of_node(&outline, raise_child);
    CHECK(row > 0, "the raise child is not in the outline");
    if (row < 0)
        return;
    CHECK(pe_tree_outline_toggle(&outline, row), "the raise child would not fold");
    pe_tree_outline_build(&outline, &editor);
    /* Its two children go, nothing else does. */
    CHECK(outline.row_count == 4,
          "folding one node left %d rows, expected 4", outline.row_count);
    CHECK(pe_tree_outline_row_of_node(&outline, raise_child) >= 0,
          "folding a node hid the node itself");

    row = pe_tree_outline_row_of_node(&outline, raise_child);
    CHECK(pe_tree_outline_toggle(&outline, row), "the raise child would not unfold");
    pe_tree_outline_build(&outline, &editor);
    CHECK(outline.row_count == 9,
          "unfolding restored %d rows, expected 9", outline.row_count);

    /* A leaf has nothing to fold, and saying otherwise would give the UI a
       fold marker that does nothing. */
    row = pe_tree_outline_row_of_node(&outline, 1);
    CHECK(row >= 0 && outline.rows[row].child_count == 0,
          "node 1 was expected to be a leaf");
    CHECK(pe_tree_outline_toggle(&outline, row) == 0,
          "a leaf accepted a fold");
}

static void test_collapse_all_keeps_the_root(void)
{
    pe_tree_editor_t editor;
    pe_tree_outline_t outline;
    int raise_child = -1;

    build_tree(&editor, &raise_child);
    pe_tree_outline_init(&outline, NULL);
    pe_tree_outline_collapse_all(&outline, &editor);
    pe_tree_outline_build(&outline, &editor);
    /* The root stays open so the tree is never invisible: root + its three. */
    CHECK(outline.row_count == 4,
          "collapse all left %d rows, expected the root and its 3 actions",
          outline.row_count);
    pe_tree_outline_expand_all(&outline);
    pe_tree_outline_build(&outline, &editor);
    CHECK(outline.row_count == 9, "expand all left %d rows, expected 9",
          outline.row_count);
}

static void test_reveal_opens_the_ancestors(void)
{
    pe_tree_editor_t editor;
    pe_tree_outline_t outline;
    int raise_child = -1;
    int grandchild;
    int row;

    build_tree(&editor, &raise_child);
    pe_tree_outline_init(&outline, NULL);
    pe_tree_outline_build(&outline, &editor);
    row = pe_tree_outline_row_of_node(&outline, raise_child);
    grandchild = outline.rows[row + 1].node_index;

    pe_tree_outline_collapse_all(&outline, &editor);
    pe_tree_outline_build(&outline, &editor);
    CHECK(pe_tree_outline_row_of_node(&outline, grandchild) < 0,
          "the grandchild was visible while everything was folded");

    row = pe_tree_outline_reveal(&outline, &editor, grandchild);
    CHECK(row >= 0, "reveal could not surface the grandchild");
    CHECK(pe_tree_outline_row_of_node(&outline, grandchild) == row,
          "reveal returned a row that does not hold the node");
    /* Only the path is opened, not the whole tree. */
    CHECK(outline.row_count < 9,
          "reveal opened %d rows; it should open the path, not everything",
          outline.row_count);
}

/*
 * The path key is the node's identity. Adding an action renumbers nothing
 * here, but a node's index is still an implementation detail, so what a
 * selection is remembered by has to be the path.
 */
static void test_path_keys_identify_nodes(void)
{
    pe_tree_editor_t editor;
    pe_tree_outline_t outline;
    int raise_child = -1;
    int row;

    build_tree(&editor, &raise_child);
    pe_tree_outline_init(&outline, NULL);
    pe_tree_outline_build(&outline, &editor);

    CHECK(outline.rows[0].path_key[0] == '\0',
          "the root path key is '%s', expected empty",
          outline.rows[0].path_key);
    row = pe_tree_outline_row_of_path(&outline, "r1.0000");
    CHECK(row >= 0 && outline.rows[row].node_index == raise_child,
          "the pot raise is not reachable by its path key");
    /* Distinct lines have distinct keys, including the two folds, which are
       different nodes reached by different paths. */
    CHECK(pe_tree_outline_row_of_path(&outline, "f") >= 0,
          "the root fold has no path key");
    CHECK(pe_tree_outline_row_of_path(&outline, "r1.0000:f") >= 0,
          "the fold behind the raise has no path key");
    CHECK(pe_tree_outline_row_of_path(&outline, "r1.0000:f") !=
              pe_tree_outline_row_of_path(&outline, "f"),
          "two different lines share a row");
    CHECK(pe_tree_outline_row_of_path(&outline, "nope") < 0,
          "an unknown path key matched a row");
}

/*
 * Chips, checked against arithmetic done by hand.
 *
 * Defaults are 100 behind each and 1.5 in the middle. P1 raises 1x pot:
 * nothing to call, so the raise is 1.5, the pot becomes 3.0 and P1 has 98.5
 * behind. P2 then calls: it owes 1.5, the pot becomes 4.5, both have 98.5.
 */
static void test_chip_state_follows_the_path(void)
{
    pe_tree_editor_t editor;
    pe_tree_outline_t outline;
    int raise_child = -1;
    int row;

    build_tree(&editor, &raise_child);
    pe_tree_outline_init(&outline, NULL);
    pe_tree_outline_build(&outline, &editor);

    CHECK(fabs(outline.rows[0].chips.pot - 1.5) < 1e-9,
          "the root pot is %.4f, expected 1.5", outline.rows[0].chips.pot);
    CHECK(fabs(outline.rows[0].chips.spr - 100.0 / 1.5) < 1e-9,
          "the root SPR is %.4f, expected %.4f",
          outline.rows[0].chips.spr, 100.0 / 1.5);

    row = pe_tree_outline_row_of_path(&outline, "r1.0000");
    CHECK(row >= 0, "the raise row is missing");
    if (row < 0)
        return;
    CHECK(fabs(outline.rows[row].chips.pot - 3.0) < 1e-9,
          "after a pot raise the pot is %.4f, expected 3.0",
          outline.rows[row].chips.pot);
    CHECK(fabs(outline.rows[row].chips.stacks[0] - 98.5) < 1e-9,
          "the raiser has %.4f behind, expected 98.5",
          outline.rows[row].chips.stacks[0]);
    CHECK(fabs(outline.rows[row].chips.to_call - 1.5) < 1e-9,
          "the player facing a pot raise owes %.4f, expected 1.5",
          outline.rows[row].chips.to_call);

    row = pe_tree_outline_row_of_path(&outline, "r1.0000:c");
    CHECK(row >= 0, "the call behind the raise is missing");
    if (row < 0)
        return;
    CHECK(fabs(outline.rows[row].chips.pot - 4.5) < 1e-9,
          "after the call the pot is %.4f, expected 4.5",
          outline.rows[row].chips.pot);
    CHECK(fabs(outline.rows[row].chips.stacks[1] - 98.5) < 1e-9,
          "the caller has %.4f behind, expected 98.5",
          outline.rows[row].chips.stacks[1]);
    /* Folding costs nothing, so the pot is the one that was faced. */
    row = pe_tree_outline_row_of_path(&outline, "r1.0000:f");
    CHECK(row >= 0 && fabs(outline.rows[row].chips.pot - 3.0) < 1e-9,
          "folding changed the pot");
}

/* A raise larger than the stack is an all-in for the remainder, not a
   negative stack. */
static void test_oversized_raise_is_capped(void)
{
    pe_tree_editor_t editor;
    pe_tree_outline_t outline;
    pe_tree_outline_config_t config = pe_tree_outline_default_config(2);
    int child = -1;
    int row;

    config.starting_stack = 10.0;
    pe_tree_editor_init(&editor, 2, MPF_STREET_PREFLOP);
    CHECK(pe_tree_editor_add_action(&editor, 0, MPF_TREE_ACTION_RAISE, 50.0,
                                    &child), "huge raise could not be added");
    pe_tree_outline_init(&outline, &config);
    pe_tree_outline_build(&outline, &editor);
    row = pe_tree_outline_row_of_node(&outline, child);
    CHECK(row >= 0, "the huge raise is missing");
    if (row < 0)
        return;
    CHECK(outline.rows[row].chips.stacks[0] >= 0.0,
          "an oversized raise left a stack of %.4f",
          outline.rows[row].chips.stacks[0]);
    CHECK(fabs(outline.rows[row].chips.stacks[0]) < 1e-9,
          "an oversized raise should be all-in, %.4f left",
          outline.rows[row].chips.stacks[0]);
    CHECK(outline.rows[row].chips.all_in,
          "the all-in flag was not raised");
    CHECK(fabs(outline.rows[row].chips.pot - 11.5) < 1e-9,
          "an all-in for 10 into 1.5 makes a pot of %.4f, expected 11.5",
          outline.rows[row].chips.pot);
}

static void test_render_marks_folds(void)
{
    pe_tree_editor_t editor;
    pe_tree_outline_t outline;
    char buffer[4096];
    int raise_child = -1;
    int row;

    build_tree(&editor, &raise_child);
    pe_tree_outline_init(&outline, NULL);
    pe_tree_outline_build(&outline, &editor);
    CHECK(pe_tree_outline_render(&outline, buffer, sizeof(buffer)) > 0,
          "an expanded outline rendered nothing");
    CHECK(strstr(buffer, "[-]") != NULL,
          "an expanded parent has no open marker:\n%s", buffer);

    row = pe_tree_outline_row_of_node(&outline, raise_child);
    pe_tree_outline_toggle(&outline, row);
    pe_tree_outline_build(&outline, &editor);
    pe_tree_outline_render(&outline, buffer, sizeof(buffer));
    CHECK(strstr(buffer, "[+]") != NULL,
          "a folded parent has no closed marker:\n%s", buffer);

    /* A buffer too small must truncate and stay terminated, not overrun. */
    {
        char small[24];
        memset(small, 'x', sizeof(small));
        pe_tree_outline_render(&outline, small, sizeof(small));
        CHECK(small[sizeof(small) - 1] == '\0',
              "a truncated render is not NUL terminated");
    }
}

static void test_empty_and_null_inputs(void)
{
    pe_tree_outline_t outline;
    pe_tree_editor_t editor;
    char buffer[64];

    pe_tree_outline_init(&outline, NULL);
    memset(&editor, 0, sizeof(editor));
    CHECK(pe_tree_outline_build(&outline, &editor) == 0,
          "an empty editor produced rows");
    CHECK(pe_tree_outline_build(&outline, NULL) == 0,
          "a NULL editor produced rows");
    CHECK(pe_tree_outline_build(NULL, &editor) == 0,
          "a NULL outline produced rows");
    CHECK(pe_tree_outline_row_of_node(&outline, 0) < 0,
          "an empty outline matched a node");
    CHECK(pe_tree_outline_row_of_path(&outline, NULL) < 0,
          "a NULL path matched a row");
    CHECK(pe_tree_outline_toggle(&outline, 0) == 0,
          "an empty outline accepted a toggle");
    CHECK(pe_tree_outline_reveal(&outline, &editor, 0) < 0,
          "reveal succeeded on an empty tree");
    CHECK(pe_tree_outline_render(&outline, buffer, sizeof(buffer)) == 0,
          "an empty outline rendered text");
}

int main(void)
{
    test_expanded_shows_everything();
    test_folding_hides_only_that_subtree();
    test_collapse_all_keeps_the_root();
    test_reveal_opens_the_ancestors();
    test_path_keys_identify_nodes();
    test_chip_state_follows_the_path();
    test_oversized_raise_is_capped();
    test_render_marks_folds();
    test_empty_and_null_inputs();
    if (failures != 0)
        return 1;
    puts("test_pe_tree_outline: all tests passed");
    return 0;
}

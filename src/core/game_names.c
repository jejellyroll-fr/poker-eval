/*
 * game_names.c - Single table mapping game names to enum_game_t
 *
 * See game_names.h.  The table is deliberately data-only: adding a game is
 * one line here, and every front end (pokenum, the Python binding, future
 * ones) picks it up.
 *
 * This program gives you software freedom; you can copy, convey,
 * propagate, redistribute and/or modify this program under the terms of
 * the GNU General Public License (GPL) as published by the Free Software
 * Foundation (FSF), either version 3 of the License, or (at your option)
 * any later version of the GPL published by the FSF.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program in a file in the toplevel directory called "GPLv3".
 * If not, see <http://www.gnu.org/licenses/>.
 */
#include <ctype.h>
#include <string.h>

#include <poker_eval/core/game_names.h>

typedef struct {
    const char *name; /* display spelling; matching is normalised */
    enum_game_t game;
    int canonical;
} game_name_row_t;

static const game_name_row_t game_name_rows[] = {
    /* Hold'em and flop games. */
    {"holdem", game_holdem, 1},
    {"h", game_holdem, 0},
    {"texasholdem", game_holdem, 0},
    {"holdem8", game_holdem8, 1},
    {"h8", game_holdem8, 0},
    {"omaha", game_omaha, 1},
    {"o", game_omaha, 0},
    {"omaha5", game_omaha5, 1},
    {"o5", game_omaha5, 0},
    {"omaha6", game_omaha6, 1},
    {"o6", game_omaha6, 0},
    {"omaha8", game_omaha8, 1},
    {"o8", game_omaha8, 0},
    {"omaha85", game_omaha85, 1},
    {"o85", game_omaha85, 0},
    {"omaha86", game_omaha86, 1},
    {"sdholdem", game_sdholdem, 1},
    {"shortdeck", game_sdholdem, 0},
    {"shortdeckholdem", game_sdholdem, 0},
    {"sd", game_sdholdem, 0},
    {"doubleflop_holdem", game_doubleflop_holdem, 1},
    {"dh", game_doubleflop_holdem, 0},
    {"pineapple", game_pineapple, 1},
    {"pa", game_pineapple, 0},
    {"pineapple8", game_pineapple8, 1},
    {"pa8", game_pineapple8, 0},
    {"pineapple_crazy", game_pineapple_crazy, 1},
    {"pacrazy", game_pineapple_crazy, 0},
    {"pineapple_lazy", game_pineapple_lazy, 1},
    {"palazy", game_pineapple_lazy, 0},
    {"courchevel", game_courchevel, 1},
    {"cv", game_courchevel, 0},
    {"courchevel8", game_courchevel8, 1},
    {"cv8", game_courchevel8, 0},
    {"irish", game_irish, 1},
    {"ir", game_irish, 0},
    {"manila", game_manila, 1},
    {"mn", game_manila, 0},
    {"italianmanila", game_manila, 0},
    {"italian", game_italian, 1},

    /* Stud games. */
    {"7stud", game_7stud, 1},
    {"7s", game_7stud, 0},
    {"sevencardstud", game_7stud, 0},
    {"7stud8", game_7stud8, 1},
    {"7s8", game_7stud8, 0},
    {"7studnsq", game_7studnsq, 1},
    {"7snsq", game_7studnsq, 0},
    {"razz", game_razz, 1},
    {"r", game_razz, 0},
    {"astud", game_astud, 1},
    {"asianstud", game_astud, 0},
    {"spanishstud", game_astud, 0},
    /* Sökö / Canadian Stud.  The UTF-8 spellings normalise to "soko". */
    {"soko", game_soko, 1},
    {"sökö", game_soko, 0},
    {"canadianstud", game_soko, 0},
    {"scandinavianstud", game_soko, 0},

    /* Draw and lowball games. */
    {"5draw", game_5draw, 1},
    {"5d", game_5draw, 0},
    {"5draw8", game_5draw8, 1},
    {"5d8", game_5draw8, 0},
    {"5drawnsq", game_5drawnsq, 1},
    {"5dnsq", game_5drawnsq, 0},
    {"5draw_std", game_5draw_std, 1},
    {"5carddraw", game_5draw_std, 0},
    {"5draw8_std", game_5draw8_std, 1},
    {"5drawnsq_std", game_5drawnsq_std, 1},
    {"lowball", game_lowball, 1},
    {"l", game_lowball, 0},
    {"lowball27", game_lowball27, 1},
    {"l27", game_lowball27, 0},
    {"27_triple_draw", game_27_triple_draw, 1},
    {"27td", game_27_triple_draw, 0},
    {"a5_triple_draw", game_a5_triple_draw, 1},
    {"a5td", game_a5_triple_draw, 0},
    {"badacey", game_badacey, 1},
    {"ba", game_badacey, 0},
    {"badeucy", game_badeucy, 1},
    {"be", game_badeucy, 0},
    {"badugi", game_badugi, 1},
    {"badugi_hilo", game_badugi_hilo, 1},
    {"archie", game_archie, 1},
    {"drawmaha", game_drawmaha, 1},
    {"dm", game_drawmaha, 0},
    {"drawmaha49", game_drawmaha49, 1},
    {"drawmaha_zero", game_drawmaha_zero, 1},
    {"drawmaha_dugi", game_drawmaha_dugi, 1},
    {"fusion", game_fusion, 1},

    /* Registry-only variants (no historical short flag). */
    {"royal", game_royal, 1},
    {"ofc", game_ofc, 1},
    {"openfacechinese", game_ofc, 0},
    {"chinese13", game_chinese13, 1},
    {"chinesepoker13", game_chinese13, 0},
    {"doubleboard_omaha85", game_doubleboard_omaha85, 1},
};

#define GAME_NAME_ROW_COUNT (sizeof(game_name_rows) / sizeof(game_name_rows[0]))

/*
 * Fold a spelling for comparison: lowercase ASCII, drop '-', '_' and spaces,
 * and map the two-byte UTF-8 forms of ö/Ö to 'o'.  Returns 0 on success, -1
 * when the result does not fit.
 */
static int normalise_game_name(const char *in, char *out, size_t out_size) {
    size_t o = 0;

    if (in == NULL || out_size == 0)
        return -1;
    while (*in) {
        unsigned char c = (unsigned char)*in;

        if (c == 0xC3 && (unsigned char)in[1] == 0xB6) { /* ö */
            c = 'o';
            in += 2;
        } else if (c == 0xC3 && (unsigned char)in[1] == 0x96) { /* Ö */
            c = 'o';
            in += 2;
        } else {
            ++in;
        }
        if (c == '-' || c == '_' || isspace(c))
            continue;
        if (o + 1 >= out_size)
            return -1;
        out[o++] = (char)tolower(c);
    }
    out[o] = '\0';
    return 0;
}

int pe_game_from_name(const char *name, enum_game_t *out) {
    char wanted[64];
    size_t i;

    if (name == NULL || out == NULL)
        return -1;
    /* A leading '-' is how every front end spells an option. */
    while (*name == '-')
        ++name;
    if (normalise_game_name(name, wanted, sizeof(wanted)) != 0)
        return -1;
    if (wanted[0] == '\0')
        return -1;

    for (i = 0; i < GAME_NAME_ROW_COUNT; ++i) {
        char candidate[64];
        if (normalise_game_name(game_name_rows[i].name, candidate,
                                sizeof(candidate)) != 0)
            continue;
        if (strcmp(wanted, candidate) == 0) {
            *out = game_name_rows[i].game;
            return 0;
        }
    }
    return -1;
}

const char *pe_game_canonical_name(enum_game_t game) {
    size_t i;

    for (i = 0; i < GAME_NAME_ROW_COUNT; ++i) {
        if (game_name_rows[i].canonical && game_name_rows[i].game == game)
            return game_name_rows[i].name;
    }
    return NULL;
}

size_t pe_game_name_count(void) {
    return GAME_NAME_ROW_COUNT;
}

const pe_game_name_entry_t *pe_game_name_at(size_t i) {
    static pe_game_name_entry_t entry;

    if (i >= GAME_NAME_ROW_COUNT)
        return NULL;
    entry.name = game_name_rows[i].name;
    entry.game = game_name_rows[i].game;
    entry.canonical = game_name_rows[i].canonical;
    return &entry;
}

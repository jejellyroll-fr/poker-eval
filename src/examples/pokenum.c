/*
 *  Copyright 2006 Michael Maurer <mjmaurer@yahoo.com>, 
 *                 Brian Goetz <brian@quiotix.com>, 
 *                 Loic Dachary <loic@dachary.org>, 
 *                 Tim Showalter <tjs@psaux.com>
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
/* $Id: pokenum.c 5146 2008-12-04 03:11:22Z bkuhn $
   Example showing how to calculate pot equity using the enumeration functions.
   See also hcmp2.c and hcmpn.c for slightly lower level examples.

Note: games that use the joker are not yet implemented, though listed below.

Typical usage:
$ pokenum [-mc niter] [-t] [-O]
[-h|-h8|-o|-o8|-7s|-7s8|-7snsq|-r|-5d|-5d8|-5dnsq|-l|-l27|-pa]
<pocket1> - <pocket2> - ... [ -- <board> ] [ / <dead> ]
      -t     terse output (one line, list of EV values)
      -O     compute and output detailed relative rank ordering histogram
      -mc    sample monte-carlo style instead of full enumeration
      -h     holdem hi (default)
      -h8    holdem hi/lo 8-or-better
      -o     omaha hi
      -o5    omaha hi 5cards
      -o6    omaha hi 6cards
      -o8    omaha hi/lo 8-or-better
      -o85   omaha 5cards hi/lo 8-or-better
      -7s    7-card stud hi
      -7s8   7-card stud hi/lo 8-or-better
      -7snsq 7-card stud hi/lo no stinking qualifier
      -r     7-card stud ace-to-5 low (razz)
      -5d    5-card draw hi (with joker)
      -5d8   5-card draw hi/lo 8-or-better (with joker)
      -5dnsq 5-card draw hi/lo no stinking qualifier (with joker)
      -l     5-card draw ace-to-5 lowball (with joker)
      -l27   5-card draw deuce-to-seven lowball
      -pa    pineapple hold'em (3 hole cards, discard 1 after flop)
      -fusion fusion poker (hybrid Hold'em/Omaha)
      -27td  2-7 triple draw
      -a5td  A-5 triple draw
      -badacey or -ba  Badacey (Badugi + A-5 Lowball split pot)
      -badugi  Badugi (4-card lowball, unique suits and ranks)
      -dm    drawmaha (split pot: Omaha hi + draw)
      -manila or -mn  Manila Hold'em (32-card deck, 8-A only, Flush > Full House)

Streaming usage:
$ pokenum -i
followed by lines on stdin, one enumeration request per line, for example:
  -t -h Ac 7c - 5s 4s - Ks Kd
  -t -h Ac 7c - 5s 4s - Ks Kd -- 7h 2c 3h
  -t -h Ac 7c - 5s 4s - Ks Kd -- 7h 2c 3h 4d
  -t -mc 10000 -l27 5h 4h 3h / 5s Qd - 9s 8h 6d / Ks Kh

Use '-' between each player (optional in flop games).
Use '--' before the board (if any).
Use '/' before dead cards (can appear within a hand for discards).
Use 'Xx' for the joker.

Examples:

$ pokenum -h Ac 7c - 5s 4s - Ks Kd
$ pokenum -h Ac 7c 5s 4s Ks Kd
$ pokenum -h Ac 7c 5s 4s Ks Kd -- 7h 2c 3h
$ pokenum -sd Ac 7c - 5s 4s - Ks Kd

$ pokenum -o As Kh Qs Jh - 8h 8d 7h 6d
$ pokenum -o As Kh Qs Jh 8h 8d 7h 6d
$ pokenum -o As Kh Qs Jh 8h 8d 7h 6d -- 8s Ts Jc
$ pokenum -mc 1000000 -o85 As Kh Qs Jh Ts - 8h 8d 7h 6d 9c
$ pokenum -mc 1000000 -o5 As Kh Qs Jh Ts - 8h 8d 7h 6d 9c
$ pokenum -mc 1000000 -o6 As Kh Qs Jh Ts 9d - 8h 8d 7h 6d 9c 6c

$ pokenum -7s As Ah Ts Th 8h 8d - Kc Qc Jc Td 3c 2d
$ pokenum -7s As Ah Ts Th 8h 8d - Kc Qc Jc Td 3c 2d / 5c 6c 2s Jh

$ pokenum -l 7h 5s 3d Xx - 9s 8h 6d 4c / 8c Kd
$ pokenum -l27 5h 4h 3h 2h - 9s 8h 6d 4c / Kd 5s
$ pokenum -mc 10000 -l27 5h 4h 3h - 9s 8h 6d / Ks Kh 5s Qd

   Michael Maurer, Apr 2002
   modified by jejellyroll
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <poker_eval/poker_eval.h>
#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/universal_deck.h>

/* Fusion-specific helper functions */
static int fusion_get_expected_pocket_cards(int nboard) {
    switch (nboard) {
        case 0: return 2;  /* preflop */
        case 3: return 3;  /* flop */
        case 4:
        case 5: return 4;  /* turn/river */
        default: return 2; /* default to preflop */
    }
}

static int fusion_validate_pocket_cards(int ncards, int nboard) {
    int expected = fusion_get_expected_pocket_cards(nboard);
    return ncards == expected ? 0 : 1;
}

/* Post-parsing validation for Fusion poker */
static int fusion_validate_parsed_hands(UniversalCardMask pockets[], int npockets, int nboard) {
    int i;
    for (i = 0; i < npockets; i++) {
        /* Count pocket cards for this player using Universal_numCards */
        int nhole = Universal_numCards(pockets[i], UNIVERSAL_DECK_STANDARD);

        /* Validate against expected count for this board size */
        if (fusion_validate_pocket_cards(nhole, nboard) != 0) {
            return 1; /* Invalid number of pocket cards */
        }
    }
    return 0; /* All hands valid */
}

static int
parseArgs(int argc, char **argv,
          enum_game_t *game, enum_sample_t *enumType, int *niter,
          UniversalCardMask pockets[], UniversalCardMask *board,
          UniversalCardMask *board2, UniversalCardMask *dead,
          deck_type_t *deckType,
          int *npockets, int *nboard, int *nboard2,
          int *orderflag, int *terse,
          int *show_backend, const char **backend_override) {
  enum_gameparams_t *gameParams = enumGameParams(game_holdem);
  enum { ST_OPTIONS, ST_POCKET, ST_BOARD, ST_BOARD2, ST_DEAD } state; 
  int ncards;
  int card;
  int i;

  state = ST_OPTIONS;
  *npockets = *nboard = *nboard2 = ncards = 0;
  *terse = 0;
  *orderflag = 0;
  *game = game_holdem;
  *deckType = Universal_DetermineRequiredDeckType(*game);
  *enumType = ENUM_EXHAUSTIVE;
  if (show_backend)
    *show_backend = 0;
  if (backend_override)
    *backend_override = NULL;
  UMASK_RESET(*dead, *deckType);
  UMASK_RESET(*board, *deckType);
  UMASK_RESET(*board2, *deckType);
  for (i=0; i<ENUM_MAXPLAYERS; i++)
    UMASK_RESET(pockets[i], *deckType);

  while (++argv, --argc) {
    if (state == ST_OPTIONS) {
      if (argv[0][0] != '-') {
        state = ST_POCKET;
        argv--; argc++;
      } else {
        if (strcmp(*argv, "-mc") == 0) {
          *enumType = ENUM_SAMPLE;
          if (argc < 1)
            return 1;
          *niter = (int)strtol(argv[1], NULL, 0);
          if (*niter <= 0 || errno != 0)
            return 1;
          argv++; argc--;
        } else if (strcmp(*argv, "-t") == 0) {
          *terse = 1;
        } else if (strcmp(*argv, "-O") == 0) {
          *orderflag = 1;
        } else if (strcmp(*argv, "-h") == 0) {
          *game = game_holdem;
        } else if (strcmp(*argv, "-h8") == 0) {
          *game = game_holdem8;
        } else if (strcmp(*argv, "-o") == 0) {
          *game = game_omaha;
        } else if (strcmp(*argv, "-o5") == 0) {
          *game = game_omaha5;
        } else if (strcmp(*argv, "-o6") == 0) {
          *game = game_omaha6;
        } else if (strcmp(*argv, "-o8") == 0) {
          *game = game_omaha8;
        } else if (strcmp(*argv, "-o85") == 0) {
          *game = game_omaha85;
        } else if (strcmp(*argv, "-7s") == 0) {
          *game = game_7stud;
        } else if (strcmp(*argv, "-7s8") == 0) {
          *game = game_7stud8;
        } else if (strcmp(*argv, "-7snsq") == 0) {
          *game = game_7studnsq;
        } else if (strcmp(*argv, "-r") == 0) {
          *game = game_razz;
        } else if (strcmp(*argv, "-5d") == 0) {
          *game = game_5draw;
        } else if (strcmp(*argv, "-5d8") == 0) {
          *game = game_5draw8;
        } else if (strcmp(*argv, "-5dnsq") == 0) {
          *game = game_5drawnsq;
        } else if (strcmp(*argv, "-l") == 0) {
          *game = game_lowball;
        } else if (strcmp(*argv, "-l27") == 0) {
          *game = game_lowball27;
        } else if (strcmp(*argv, "-sd") == 0) {
          *game = game_sdholdem;
        } else if (strcmp(*argv, "-dh") == 0) {
          *game = game_doubleflop_holdem;
        } else if (strcmp(*argv, "-pa") == 0) {
          *game = game_pineapple;
        } else if (strcmp(*argv, "-27td") == 0) {
          *game = game_27_triple_draw;
        } else if (strcmp(*argv, "-a5td") == 0) {
          *game = game_a5_triple_draw;
        } else if (strcmp(*argv, "-badacey") == 0 || strcmp(*argv, "-ba") == 0) {
          *game = game_badacey;
        } else if (strcmp(*argv, "-badeucy") == 0 || strcmp(*argv, "-be") == 0) {
          *game = game_badeucy;
        } else if (strcmp(*argv, "-badugi") == 0) {
          *game = game_badugi;
        } else if (strcmp(*argv, "-dm") == 0) {
          *game = game_drawmaha;
        } else if (strcmp(*argv, "-fusion") == 0) {
          *game = game_fusion;
        } else if (strcmp(*argv, "-courchevel") == 0 || strcmp(*argv, "-cv") == 0) {
          *game = game_courchevel;
        } else if (strcmp(*argv, "-courchevel8") == 0 || strcmp(*argv, "-cv8") == 0) {
          *game = game_courchevel8;
        } else if (strcmp(*argv, "-irish") == 0 || strcmp(*argv, "-ir") == 0) {
          *game = game_irish;
        } else if (strcmp(*argv, "-manila") == 0 || strcmp(*argv, "-mn") == 0) {
          *game = game_manila;
        } else if (strncmp(*argv, "--backend=", 10) == 0) {
          if (backend_override)
            *backend_override = *argv + 10;
        } else if (strcmp(*argv, "--backend") == 0) {
          if (argc < 1)
            return 1;
          if (backend_override)
            *backend_override = argv[1];
          argv++; argc--;
        } else if (strcmp(*argv, "--show-backend") == 0) {
          if (show_backend)
            *show_backend = 1;
        } else {
          return 1;
        }
        if ((gameParams = enumGameParams(*game)) == NULL)
          return 1;
        *deckType = Universal_DetermineRequiredDeckType(*game);
      }
    } else if (state == ST_POCKET) {
      if (strcmp(*argv, "-") == 0) {
        if (ncards > 0) {
          /* For Fusion, skip validation during parsing - validate later */
          if (*game != game_fusion) {
            if (ncards < gameParams->minpocket)
              return 1;
          }
          (*npockets)++;
          ncards = 0;
        }
        state = ST_POCKET;
      } else if (strcmp(*argv, "--") == 0) {
        state = ST_BOARD;
      } else if (strcmp(*argv, "/") == 0 || strstr(*argv, "/") != NULL) {
        char *lastSlash = strrchr(*argv, '/');
        if (lastSlash != NULL && *(lastSlash + 1) == '\0') {
          state = ST_DEAD;
        } else {
          char *cardStr = lastSlash ? lastSlash + 1 : *argv;
          deck_type_t parsedDeckType = *deckType;
          if (Universal_StringToCard(cardStr, &card, &parsedDeckType) == 0)
            return 1;
          if (parsedDeckType == UNIVERSAL_DECK_JOKER && *deckType == UNIVERSAL_DECK_STANDARD) *deckType = UNIVERSAL_DECK_JOKER;
          if (UMASK_IS_SET(*dead, card, *deckType))
            return 1;
          UMASK_SET(pockets[*npockets], card, *deckType);
          UMASK_SET(*dead, card, *deckType);
          ncards++;
          /* For Fusion, use a flexible approach - don't auto-advance */
          if (*game != game_fusion) {
            if (ncards == gameParams->maxpocket) {
              (*npockets)++;
              ncards = 0;
            }
          }
        }
      } else {
        if (*npockets >= ENUM_MAXPLAYERS)
          return 1;
        deck_type_t parsedDeckType = *deckType;
        if (Universal_StringToCard(*argv, &card, &parsedDeckType) == 0)
          return 1;
        if (parsedDeckType == UNIVERSAL_DECK_JOKER && *deckType == UNIVERSAL_DECK_STANDARD) *deckType = UNIVERSAL_DECK_JOKER;
        if (UMASK_IS_SET(*dead, card, *deckType))
          return 1;
        UMASK_SET(pockets[*npockets], card, *deckType);
        UMASK_SET(*dead, card, *deckType);
        ncards++;
        /* For Fusion, use a flexible approach - don't auto-advance */
        if (*game != game_fusion) {
          if (ncards == gameParams->maxpocket) {
            (*npockets)++;
            ncards = 0;
          }
        }
      }
    } else if (state == ST_BOARD) {
      if (strcmp(*argv, "---") == 0) {
        if (*game == game_doubleflop_holdem) {
          if (*nboard < gameParams->maxboard) {
            state = ST_BOARD2;
          } else {
            return 1;
          }
        } else {
          return 1;
        }
      } else if (strcmp(*argv, "/") == 0 || strstr(*argv, "/") != NULL) {
        char *lastSlash = strrchr(*argv, '/');
        if (lastSlash != NULL && *(lastSlash + 1) == '\0') {
          state = ST_DEAD;
        } else {
          char *cardStr = lastSlash ? lastSlash + 1 : *argv;
          deck_type_t parsedDeckType = *deckType;
          if (Universal_StringToCard(cardStr, &card, &parsedDeckType) == 0)
            return 1;
          if (parsedDeckType == UNIVERSAL_DECK_JOKER && *deckType == UNIVERSAL_DECK_STANDARD) *deckType = UNIVERSAL_DECK_JOKER;
          if (UMASK_IS_SET(*dead, card, *deckType))
            return 1;
          if (*nboard >= gameParams->maxboard)
            return 1;
          UMASK_SET(*board, card, *deckType);
          UMASK_SET(*dead, card, *deckType);
          (*nboard)++;
        }
      } else {
        deck_type_t parsedDeckType = *deckType;
        if (Universal_StringToCard(*argv, &card, &parsedDeckType) == 0)
          return 1;
        if (parsedDeckType == UNIVERSAL_DECK_JOKER && *deckType == UNIVERSAL_DECK_STANDARD) *deckType = UNIVERSAL_DECK_JOKER;
        if (UMASK_IS_SET(*dead, card, *deckType))
          return 1;
        if (*nboard >= gameParams->maxboard)
          return 1;
        UMASK_SET(*board, card, *deckType);
        UMASK_SET(*dead, card, *deckType);
        (*nboard)++;
      }
    } else if (state == ST_BOARD2) {
      if (strcmp(*argv, "/") == 0 || strstr(*argv, "/") != NULL) {
        char *lastSlash = strrchr(*argv, '/');
        if (lastSlash != NULL && *(lastSlash + 1) == '\0') {
          state = ST_DEAD;
        } else {
          char *cardStr = lastSlash ? lastSlash + 1 : *argv;
          deck_type_t parsedDeckType = *deckType;
          if (Universal_StringToCard(cardStr, &card, &parsedDeckType) == 0)
            return 1;
          if (parsedDeckType == UNIVERSAL_DECK_JOKER && *deckType == UNIVERSAL_DECK_STANDARD) *deckType = UNIVERSAL_DECK_JOKER;
          if (UMASK_IS_SET(*dead, card, *deckType))
            return 1;
          if (*nboard2 >= gameParams->maxboard)
            return 1;
          UMASK_SET(*board2, card, *deckType);
          UMASK_SET(*dead, card, *deckType);
          (*nboard2)++;
        }
      } else {
        deck_type_t parsedDeckType = *deckType;
        if (Universal_StringToCard(*argv, &card, &parsedDeckType) == 0)
          return 1;
        if (parsedDeckType == UNIVERSAL_DECK_JOKER && *deckType == UNIVERSAL_DECK_STANDARD) *deckType = UNIVERSAL_DECK_JOKER;
        if (UMASK_IS_SET(*dead, card, *deckType))
          return 1;
        if (*nboard2 >= gameParams->maxboard)
          return 1;
        UMASK_SET(*board2, card, *deckType);
        UMASK_SET(*dead, card, *deckType);
        (*nboard2)++;
      }
    } else if (state == ST_DEAD) {
      deck_type_t parsedDeckType = *deckType;
      if (Universal_StringToCard(*argv, &card, &parsedDeckType) == 0)
        return 1;
      if (parsedDeckType == UNIVERSAL_DECK_JOKER && *deckType == UNIVERSAL_DECK_STANDARD) *deckType = UNIVERSAL_DECK_JOKER;
      // Note: For dead cards, we don't check if it's already in dead, as it's a new card being added to dead.
      UMASK_SET(*dead, card, *deckType);
    }
  }

  if (ncards > 0) {
    /* For Fusion, skip validation during parsing - validate later */
    if (*game != game_fusion) {
      if (ncards < gameParams->minpocket)
        return 1;
    }
    (*npockets)++;
  }
  if (*npockets == 0)
    return 1;
  return 0;
}

#define MAX_ARGS 100
#define BUF_LEN 1000

int 
main(int argc, char **argv) {
  enum_game_t game;
  deck_type_t deckType;
  enum_sample_t enumType;
  int niter = 0, npockets, nboard, nboard2, err, terse, orderflag;
  UniversalCardMask pockets[ENUM_MAXPLAYERS];
  UniversalCardMask board;
  UniversalCardMask board2;
  UniversalCardMask dead;
  enum_result_t result;
  int fromStdin;

  fromStdin = (argc == 2 && !strcmp(argv[1], "-i"));
  if (fromStdin)
    argv = (char **) malloc(MAX_ARGS * sizeof(char *));
  do {
    err = 0;
    enumResultClear(&result);
    int show_backend = 0;
    const char *backend_override = NULL;
    if (fromStdin) {	/* read one line from stdin, split into argv/argc */
      char buf[BUF_LEN], *p;
      if (fgets(buf, sizeof(buf), stdin) == NULL)
        break;
      argc = 0;
      argv[argc++] = strdup("pokenum");
      p = strtok(buf, " \t\r\n");
      while (p != NULL) {
        argv[argc++] = p;
        p = strtok(NULL, " \t\r\n");
      }
    }
    if (parseArgs(argc, argv, &game, &enumType, &niter,
                  pockets, &board, &board2, &dead, &deckType, &npockets, &nboard,
                  &nboard2, &orderflag, &terse, &show_backend, &backend_override)) {
      if (fromStdin) {
        printf("ERROR\n");
      } else {
        printf("single usage: %s [-t] [-O] [-mc niter]\n", argv[0]);
        printf("\t[-h|-h8|-o|-o5|-o6|-o8|-o85|-7s|-7s8|-7snsq|-r|-5d|-5d8|-5dnsq|-l|-sd|-l27|-pa|-fusion|-27td|-a5td|-badacey|-badeucy|-badugi|-dm|-cv|-cv8|-irish]\n");
        printf("\t<pocket1> - <pocket2> - ... [ -- <board> ] [ / <dead> ] ]\n");
        printf("streaming usage: %s -i < argsfile\n", argv[0]);
      }
      err = 1;
    } else if (game == game_fusion && fusion_validate_parsed_hands(pockets, npockets, nboard)) {
      if (fromStdin) {
        printf("ERROR: Invalid Fusion hand configuration\n");
      } else {
      printf("ERROR: Fusion requires specific pocket card counts based on board:\n");
        printf("  Preflop (no board): 2 pocket cards per player\n");
        printf("  Flop (3 board cards): 3 pocket cards per player\n");
        printf("  Turn/River (4-5 board cards): 4 pocket cards per player\n");
      }
      err = 1;
    } else {
      if (backend_override && *backend_override) {
        setenv("PE_ENUM_BACKEND", backend_override, 1);
      }
      if (deckType == UNIVERSAL_DECK_JOKER) {
        // For JokerDeck games, we still use StdDeck_CardMask in the enumeration functions
        // The conversion to JokerDeck is handled internally in enumerate.c
        StdDeck_CardMask spockets[ENUM_MAXPLAYERS], sboard, sdead;
        for (int i = 0; i < npockets; ++i) spockets[i] = pockets[i].std;
        sboard = board.std;
        sdead = dead.std;
        if (enumType == ENUM_EXHAUSTIVE) {
          err = enumExhaustive_dispatch(game, spockets, sboard, sdead, npockets, nboard, orderflag, &result);
        } else if (enumType == ENUM_SAMPLE) {
          err = enumSample(game, spockets, sboard, sdead, npockets, nboard, niter, orderflag, &result);
        } else {
          err = 1;
        }
      } else {
        StdDeck_CardMask spockets[ENUM_MAXPLAYERS], sboard, sdead;
        for (int i = 0; i < npockets; ++i) spockets[i] = pockets[i].std;
        sboard = board.std;
        sdead = dead.std;
        if (enumType == ENUM_EXHAUSTIVE) {
          err = enumExhaustive_dispatch(game, spockets, sboard, sdead, npockets, nboard, orderflag, &result);
        } else if (enumType == ENUM_SAMPLE) {
          err = enumSample(game, spockets, sboard, sdead, npockets, nboard, niter, orderflag, &result);
        } else {
          err = 1;
        }
      }
      if (err) {
        if (fromStdin)
          printf("ERROR\n");
        else
          printf("enumeration function failed err=%d\n", err);
      } else {
        StdDeck_CardMask spockets_print[ENUM_MAXPLAYERS], sboard_print;
        for (int i = 0; i < npockets; ++i) {
          if (deckType == UNIVERSAL_DECK_JOKER) Universal_ConvertJokerToStd(pockets[i].joker, &spockets_print[i]);
          else spockets_print[i] = pockets[i].std;
        }
        if (deckType == UNIVERSAL_DECK_JOKER) Universal_ConvertJokerToStd(board.joker, &sboard_print);
        else sboard_print = board.std;

        if (terse)
          enumResultPrintTerse(&result, spockets_print, sboard_print);
        else
          enumResultPrint(&result, spockets_print, sboard_print);
        if (show_backend) {
          if (enumType == ENUM_EXHAUSTIVE) {
            enum_dispatch_backend_t backend = enum_dispatch_last_backend();
            printf("[enum-backend] %s\n", enum_dispatch_backend_name(backend));
          } else {
            printf("[enum-backend] sampling\n");
          }
        }
      }
    }
    enumResultFree(&result);
    fflush(stdout);
  } while (fromStdin);
  return err;
}

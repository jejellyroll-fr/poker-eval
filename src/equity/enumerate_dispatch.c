/*
 * enumerate_dispatch.c -- backend router for exhaustive enumeration
 *
 * Selects the most appropriate enumeration backend (classic macros,
 * generic EEDC, Omaha-optimized EEDC) depending on the game and optional
 * environment overrides.
 */

#include <ctype.h>
#include <stdlib.h>

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/equity/enumerate_eedc.h>

/* Simple case-insensitive string compare to avoid non-portable strcasecmp */
static int pe_streq_ci(const char *lhs, const char *rhs)
{
  if (lhs == NULL || rhs == NULL)
    return 0;
  while (*lhs && *rhs)
  {
    unsigned char a = (unsigned char)*lhs++;
    unsigned char b = (unsigned char)*rhs++;
    if (tolower(a) != tolower(b))
      return 0;
  }
  return *lhs == '\0' && *rhs == '\0';
}

static int is_omaha_game(enum_game_t game)
{
  return game == game_omaha ||
         game == game_omaha5 ||
         game == game_omaha6 ||
         game == game_omaha8 ||
         game == game_omaha85;
}

static int is_draw_game(enum_game_t game)
{
  return game == game_5draw ||
         game == game_5draw8 ||
         game == game_5drawnsq ||
         game == game_lowball ||
         game == game_lowball27 ||
         game == game_27_triple_draw ||
         game == game_a5_triple_draw;
}

static int is_stud_like_game(enum_game_t game)
{
  return game == game_7stud ||
         game == game_7stud8 ||
         game == game_7studnsq ||
         game == game_razz;
}

static int is_holdem_family(enum_game_t game)
{
  return game == game_holdem ||
         game == game_holdem8 ||
         game == game_sdholdem ||
         game == game_doubleflop_holdem;
}

/* Returns 1 if caller requested the legacy classic backend explicitly. */
static int force_classic_backend(void)
{
  const char *backend = getenv("PE_ENUM_BACKEND");
  return backend && pe_streq_ci(backend, "classic");
}

/* Returns 1 if caller requested to route everything through EEDC. */
static int force_eedc_backend(void)
{
  const char *backend = getenv("PE_ENUM_BACKEND");
  return backend && pe_streq_ci(backend, "eedc");
}

static enum_dispatch_backend_t g_last_backend = enum_dispatch_backend_classic;

enum_dispatch_backend_t enum_dispatch_last_backend(void)
{
  return g_last_backend;
}

const char *enum_dispatch_backend_name(enum_dispatch_backend_t backend)
{
  switch (backend)
  {
  case enum_dispatch_backend_classic:
    return "classic";
  case enum_dispatch_backend_eedc:
    return "eedc";
  case enum_dispatch_backend_eedc_omaha_opt:
    return "eedc_omaha_opt";
  default:
    return "unknown";
  }
}

int enumExhaustive_dispatch(enum_game_t game,
                            StdDeck_CardMask pockets[],
                            StdDeck_CardMask board,
                            StdDeck_CardMask dead,
                            int npockets,
                            int nboard,
                            int orderflag,
                            enum_result_t *result)
{
  if (force_classic_backend())
  {
    g_last_backend = enum_dispatch_backend_classic;
    return enumExhaustive(game, pockets, board, dead, npockets, nboard, orderflag, result);
  }

  if (is_omaha_game(game))
  {
    g_last_backend = enum_dispatch_backend_eedc_omaha_opt;
    return enumExhaustive_eedc_omaha_opt(game, pockets, board, dead, npockets, nboard, orderflag, result);
  }

  if (is_stud_like_game(game) || is_draw_game(game))
  {
    g_last_backend = enum_dispatch_backend_eedc;
    return enumExhaustive_eedc(game, pockets, board, dead, npockets, nboard, orderflag, result);
  }

  if (is_holdem_family(game) || force_eedc_backend())
  {
    g_last_backend = enum_dispatch_backend_eedc;
    return enumExhaustive_eedc(game, pockets, board, dead, npockets, nboard, orderflag, result);
  }

  /* Default: keep legacy path until a dedicated EEDC adapter exists. */
  g_last_backend = enum_dispatch_backend_classic;
  return enumExhaustive(game, pockets, board, dead, npockets, nboard, orderflag, result);
}

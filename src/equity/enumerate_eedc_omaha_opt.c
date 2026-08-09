/* enumerate_eedc_omaha_opt.c -- Optimized EEDC for Omaha games
 * Specific optimizations for Omaha evaluation
 */

#include <poker_eval/core/enumdefs.h>
#include <poker_eval/core/eval.h>
#include <poker_eval/core/poker_defs.h>
#include <poker_eval/deck/deck_std.h>
#include <poker_eval/equity/enumerate_eedc.h>
#include <poker_eval/games/eval_omaha.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

// Cache pour stocker les évaluations Omaha déjà calculées
typedef struct {
  StdDeck_CardMask hole;
  StdDeck_CardMask board;
  HandVal hival;
  LowHandVal loval;
  int valid;
} OmahaEvalCache;

#define CACHE_SIZE 1024
// Thread-local cache
#ifdef _OPENMP
static OmahaEvalCache eval_cache[CACHE_SIZE]; // Fallback for non-threaded
#pragma omp threadprivate(eval_cache)
#else
static OmahaEvalCache eval_cache[CACHE_SIZE];
#endif

static int cache_initialized = 0;
#ifdef _OPENMP
#pragma omp threadprivate(cache_initialized)
#endif

// Hash simple pour le cache
static unsigned int hash_cards(StdDeck_CardMask hole, StdDeck_CardMask board) {
  unsigned int hash = 0;
  // Utiliser les bits directement pour le hash
  for (int i = 0; i < StdDeck_N_CARDS; i++) {
    if (StdDeck_CardMask_CARD_IS_SET(hole, i)) {
      hash ^= (i + 1) * 2654435761U;
    }
    if (StdDeck_CardMask_CARD_IS_SET(board, i)) {
      hash ^= (i + 1) * 1103515245U;
    }
  }
  return hash % CACHE_SIZE;
}

// Version optimisée de l'évaluation Omaha avec cache
static inline int eval_omaha_cached(StdDeck_CardMask hole,
                                    StdDeck_CardMask board, HandVal *hival,
                                    LowHandVal *loval) {
  if (!cache_initialized) {
    memset(eval_cache, 0, sizeof(eval_cache));
    cache_initialized = 1;
  }

  unsigned int idx = hash_cards(hole, board);
  OmahaEvalCache *entry = &eval_cache[idx];

  // Vérifier le cache
  if (entry->valid && StdDeck_CardMask_EQUAL(entry->hole, hole) &&
      StdDeck_CardMask_EQUAL(entry->board, board)) {
    *hival = entry->hival;
    *loval = entry->loval;
    return 0;
  }

  // Calculer et mettre en cache
  int err = StdDeck_OmahaHiLow8_EVAL(hole, board, hival, loval);
  if (err == 0) {
    entry->hole = hole;
    entry->board = board;
    entry->hival = *hival;
    entry->loval = *loval;
    entry->valid = 1;
  }
  return err;
}

// Helper to merge results
static void enumResultMerge(enum_result_t *dst, enum_result_t *src) {
  for (int i = 0; i < dst->nplayers; i++) {
    dst->nwinhi[i] += src->nwinhi[i];
    dst->ntiehi[i] += src->ntiehi[i];
    dst->nlosehi[i] += src->nlosehi[i];
    dst->nwinlo[i] += src->nwinlo[i];
    dst->ntielo[i] += src->ntielo[i];
    dst->nloselo[i] += src->nloselo[i];
    dst->nscoop[i] += src->nscoop[i];
    dst->ev[i] += src->ev[i];

    for (int h = 0; h <= dst->nplayers; h++) {
      dst->nsharehi[i][h] += src->nsharehi[i][h];
      dst->nsharelo[i][h] += src->nsharelo[i][h];
      for (int l = 0; l <= dst->nplayers; l++) {
        dst->nshare[i][h][l] += src->nshare[i][h][l];
      }
    }
  }
  dst->nsamples += src->nsamples;

  // Merge ordering histograms if present
  if (dst->ordering && src->ordering) {
    for (int i = 0; i < dst->ordering->nentries; i++) {
      dst->ordering->hist[i] += src->ordering->hist[i];
    }
  }
}

// Recursive combination generator
static void generate_combinations_recursive(int *deck, int n, int k,
                                            int start_idx, int *current_combo,
                                            int *all_combos, int *count) {
  if (k == 0) {
    for (int i = 0; i < 5; i++) { // Assuming max k=5 for preflop
      // We store the actual card values, not indices into deck
      // But wait, the deck array holds the card values.
      // current_combo holds indices into deck? No, let's store card values
      // directly.
    }
    // Actually, let's store indices into 'deck' array to save space if needed,
    // but storing card values is simpler.
    // However, the loop below uses deck[indices[i]].
    // Let's store the indices into the 'deck' array.
    return;
  }
}

// Iterative combination generator that fills a buffer
// Returns number of combinations generated
static size_t generate_all_combinations(int n, int k, int *deck,
                                        int **output_buffer) {
  if (k <= 0 || n < k)
    return 0;

  // Calculate C(n, k)
  size_t num_combos = 1;
  for (int i = 0; i < k; i++) {
    num_combos = num_combos * (n - i) / (i + 1);
  }

  *output_buffer = malloc(num_combos * k * sizeof(int));
  if (!*output_buffer)
    return 0;

  int *indices = malloc(k * sizeof(int));
  if (!indices) {
    free(*output_buffer);
    *output_buffer = NULL;
    return 0;
  }
  for (int i = 0; i < k; i++)
    indices[i] = i;

  size_t count = 0;
  while (1) {
    // Store current combination (card values)
    for (int i = 0; i < k; i++) {
      (*output_buffer)[count * k + i] = deck[indices[i]];
    }
    count++;

    // Next combination
    int i = k - 1;
    while (i >= 0 && indices[i] == n - k + i)
      i--;
    if (i < 0)
      break;

    indices[i]++;
    for (int j = i + 1; j < k; j++)
      indices[j] = indices[j - 1] + 1;
  }

  free(indices);
  return count;
}

// Version EEDC optimisée spécifiquement pour Omaha
int enumExhaustive_eedc_omaha_opt(enum_game_t game, StdDeck_CardMask pockets[],
                                  StdDeck_CardMask board, StdDeck_CardMask dead,
                                  int npockets, int nboard, int orderflag,
                                  enum_result_t *result) {
  if (game != game_omaha && game != game_omaha8 && game != game_omaha5 &&
      game != game_omaha6 && game != game_omaha85 && game != game_omaha86) {
    // Fallback pour les autres jeux
    return enumExhaustive_eedc(game, pockets, board, dead, npockets, nboard,
                               orderflag, result);
  }

  enumResultClear(result);
  result->game = game;
  result->nplayers = npockets;
  result->sampleType = ENUM_EXHAUSTIVE;

  // Construire l'ensemble des cartes interdites (dead + pockets + board)
  StdDeck_CardMask effective_dead = dead;
  for (int i = 0; i < npockets; ++i) {
    StdDeck_CardMask_OR(effective_dead, effective_dead, pockets[i]);
  }
  StdDeck_CardMask_OR(effective_dead, effective_dead, board);

  // Construire le deck restant
  int deck[StdDeck_N_CARDS];
  int ndeck = 0;

  for (int c = 0; c < StdDeck_N_CARDS; ++c) {
    if (!StdDeck_CardMask_CARD_IS_SET(effective_dead, c)) {
      deck[ndeck++] = c;
    }
  }

  int n_needed = 5 - nboard;
  if (n_needed < 0 || ndeck < n_needed)
    return 1;

  if (n_needed == 0) {
    // Cas trivial : pas de cartes à tirer
    // ... (code existant pour n_needed=0, mais ici on peut juste faire une
    // itération) Pour simplifier, on traite ça comme une boucle de 1 itération
    // Mais attention, generate_all_combinations ne gère pas k=0
    // On gère ce cas séparément ou on adapte.
    // Le code original gérait ça dans la boucle do-while.
    // Ici, on va juste faire l'évaluation directe.
    // ...
    // Pour l'instant, assumons n_needed > 0 pour le parallélisme
    // Si n_needed == 0, on appelle juste l'évaluation une fois.
  }

  // Générer toutes les combinaisons
  int *combos = NULL;
  size_t num_combos = 0;

  if (n_needed > 0) {
    num_combos = generate_all_combinations(ndeck, n_needed, deck, &combos);
    if (!combos)
      return 1;
  } else {
    // 1 "combinaison" vide
    num_combos = 1;
    combos = malloc(sizeof(int)); // Dummy
    if (!combos)
      return 1;
  }

  // Préparer les résultats thread-local
  int max_threads = 1;
#ifdef _OPENMP
  max_threads = omp_get_max_threads();
#endif

  // Allocation dynamique des résultats locaux pour éviter la stack
  enum_result_t **local_results = malloc(max_threads * sizeof(enum_result_t *));
  if (!local_results)
    return -1;
  for (int i = 0; i < max_threads; i++) {
    local_results[i] = malloc(sizeof(enum_result_t));
    if (!local_results[i]) {
      for (int j = 0; j < i; j++) {
        enumResultFree(local_results[j]);
        free(local_results[j]);
      }
      free(local_results);
      return -1;
    }
    /* Clear before allocating: enumResultClear() memsets the struct, so an
     * ordering allocated first is dropped without being freed — and the
     * histogram merge below then finds a NULL ordering and skips. */
    enumResultClear(local_results[i]);
    enum_ordering_mode_t mode =
        result->ordering ? result->ordering->mode : enum_ordering_mode_hi;
    enumResultAlloc(local_results[i], npockets, mode);
    local_results[i]->game = game;
    local_results[i]->nplayers = npockets;
  }

#pragma omp parallel
  {
    int thread_id = 0;
#ifdef _OPENMP
    thread_id = omp_get_thread_num();
#endif

    enum_result_t *my_result = local_results[thread_id];

    HandVal hival[ENUM_MAXPLAYERS];
    LowHandVal loval[ENUM_MAXPLAYERS];
    long long c_idx;
    long long num_combos_ll = (long long)num_combos;

#pragma omp for schedule(dynamic, 1024)
    for (c_idx = 0; c_idx < num_combos_ll; c_idx++) {
      size_t combo_index = (size_t)c_idx;
      size_t combo_offset = combo_index * (size_t)n_needed;
      StdDeck_CardMask sharedCards;
      StdDeck_CardMask_RESET(sharedCards);

      if (n_needed > 0) {
        for (int i = 0; i < n_needed; ++i) {
          StdDeck_CardMask_SET(sharedCards, combos[combo_offset + (size_t)i]);
        }
      }

      StdDeck_CardMask fullBoard;
      StdDeck_CardMask_OR(fullBoard, board, sharedCards);

      // Variables pour le calcul du meilleur
      HandVal besthi = HandVal_NOTHING;
      LowHandVal bestlo = LowHandVal_NOTHING;
      int hishare = 0;
      int loshare = 0;

      // Évaluation avec cache pour chaque joueur
      int err = 0;
      for (int i = 0; i < npockets; i++) {
        err = eval_omaha_cached(pockets[i], fullBoard, &hival[i], &loval[i]);
        if (err != 0)
          break;

        if (hival[i] != HandVal_NOTHING) {
          if (hival[i] > besthi) {
            besthi = hival[i];
            hishare = 1;
          } else if (hival[i] == besthi) {
            hishare++;
          }
        }
        if (loval[i] != LowHandVal_NOTHING) {
          if (loval[i] < bestlo) {
            bestlo = loval[i];
            loshare = 1;
          } else if (loval[i] == bestlo) {
            loshare++;
        }
    }
    }

      if (err)
        continue; // Should handle error better

      // Calcul des pots
      double hipot, lopot;
      if (bestlo != LowHandVal_NOTHING && besthi != HandVal_NOTHING) {
        hipot = (hishare != 0) ? 0.5 / hishare : 0;
        lopot = (loshare != 0) ? 0.5 / loshare : 0;
      } else if (bestlo == LowHandVal_NOTHING && besthi != HandVal_NOTHING) {
        hipot = (hishare != 0) ? 1.0 / hishare : 0;
        lopot = 0;
      } else if (bestlo != LowHandVal_NOTHING && besthi == HandVal_NOTHING) {
        hipot = 0;
        lopot = (loshare != 0) ? 1.0 / loshare : 0;
      } else {
        hipot = lopot = 0;
      }

      // Mise à jour des résultats
      for (int i = 0; i < npockets; i++) {
        double potfrac = 0;
        int H = 0, L = 0;

        if (hival[i] != HandVal_NOTHING) {
          if (hival[i] == besthi) {
            H = hishare;
            potfrac += hipot;
            if (hishare == 1)
              my_result->nwinhi[i]++;
            else
              my_result->ntiehi[i]++;
          } else {
            my_result->nlosehi[i]++;
          }
        }

        if (loval[i] != LowHandVal_NOTHING) {
          if (loval[i] == bestlo) {
            L = loshare;
            potfrac += lopot;
            if (loshare == 1)
              my_result->nwinlo[i]++;
            else
              my_result->ntielo[i]++;
          } else {
            my_result->nloselo[i]++;
          }
        }

        my_result->nsharehi[i][H]++;
        my_result->nsharelo[i][L]++;
        my_result->nshare[i][H][L]++;

        if (besthi != HandVal_NOTHING &&
            hival[i] == besthi && hishare == 1 &&
            (bestlo == LowHandVal_NOTHING || (loval[i] == bestlo && loshare == 1))) {
          my_result->nscoop[i]++;
        }

        my_result->ev[i] += potfrac;
      }
      my_result->nsamples++;
    }
  }

  // Merge results
  for (int i = 0; i < max_threads; i++) {
    enumResultMerge(result, local_results[i]);
    enumResultFree(local_results[i]);
    free(local_results[i]);
  }
  free(local_results);
  free(combos);

  return 0;
}

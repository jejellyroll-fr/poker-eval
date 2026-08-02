/*
 * The OpenCL low evaluator is a hand-port of the CPU one, so the two drift
 * apart silently whenever only one side is fixed. This test compiles the kernel
 * for the host (see opencl_low_kernel_host.c) and compares the two on every
 * seven-card hand it draws -- no GPU and no OpenCL runtime involved.
 *
 * It exists because exactly that drift happened: the CPU learned to play its
 * best five of seven and to reject paired lows at the 8-or-better qualifier
 * while the kernel kept the old behaviour.
 */

#include <stdint.h>
#include <stdio.h>

#include <poker_eval/core/low_eval.h>
#include <poker_eval/core/low_qualifier.h>
#include <poker_eval/deck/deck_std.h>

uint32_t opencl_host_eval_low_a5(uint32_t spades, uint32_t clubs,
                                 uint32_t diamonds, uint32_t hearts,
                                 int n_cards);
int opencl_host_low_qualifies(uint32_t low, int qualifier_rank);

/* Self-contained xorshift32: see test_low_7card_best5.c for why not rand(). */
static uint32_t rng_state = 1;

static void rng_seed(uint32_t seed) {
    rng_state = seed ? seed : 1;
}

static uint32_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static uint32_t eval_on_kernel(StdDeck_CardMask hand, int n_cards) {
    return opencl_host_eval_low_a5(StdDeck_CardMask_SPADES(hand),
                                   StdDeck_CardMask_CLUBS(hand),
                                   StdDeck_CardMask_DIAMONDS(hand),
                                   StdDeck_CardMask_HEARTS(hand),
                                   n_cards);
}

static StdDeck_CardMask hand_from_string(const char *cards[], int count) {
    StdDeck_CardMask hand;
    StdDeck_CardMask_RESET(hand);
    for (int i = 0; i < count; ++i) {
        char text[3] = {cards[i][0], cards[i][1], '\0'};
        int index;
        StdDeck_stringToCard(text, &index);
        StdDeck_CardMask_SET(hand, index);
    }
    return hand;
}

/*
 * Five-card hands take a separate branch in both evaluators, and a paired hand
 * is still a low there -- just a worse one. One hand per category, so a branch
 * that is merely rare does not go unchecked.
 */
static int check_five_card_categories(void) {
    static const char *hands[][5] = {
        {"2c", "4d", "6h", "8s", "Ac"}, /* no pair  */
        {"2c", "2d", "6h", "8s", "Ac"}, /* one pair */
        {"2c", "2d", "6h", "6s", "Ac"}, /* two pair */
        {"2c", "2d", "2h", "6s", "Ac"}, /* trips    */
        {"2c", "2d", "2h", "6s", "6c"}, /* full house */
        {"2c", "2d", "2h", "2s", "Ac"}, /* quads -- the case the hierarchical
                                           selection above cannot express */
    };

    for (int i = 0; i < (int)(sizeof(hands) / sizeof(hands[0])); ++i) {
        StdDeck_CardMask hand = hand_from_string(hands[i], 5);
        LowHandVal cpu = pe_eval_low_a5(hand);
        uint32_t kernel = eval_on_kernel(hand, 5);
        if (cpu != kernel) {
            fprintf(stderr, "five-card category %d: cpu=%u kernel=%u\n", i, cpu, kernel);
            return 1;
        }
        if (cpu == LowHandVal_NOTHING) {
            fprintf(stderr, "five-card category %d evaluated to nothing\n", i);
            return 1;
        }
    }
    return 0;
}

int main(void) {
    /* The showdown that motivated the fix: two pairs among seven cards, and a
       qualifying 8-7-6-5-4 to be found underneath them. */
    {
        const char *cards[] = {"7h", "6s", "8h", "4c", "5c", "5s", "6d"};
        StdDeck_CardMask hand = hand_from_string(cards, 7);

        LowHandVal cpu = pe_eval_low_a5(hand);
        uint32_t kernel = eval_on_kernel(hand, 7);
        if (cpu != kernel) {
            fprintf(stderr, "8-7-6-5-4: cpu=%u kernel=%u\n", cpu, kernel);
            return 1;
        }
        if (!opencl_host_low_qualifies(kernel, 8)) {
            fprintf(stderr, "8-7-6-5-4 must qualify 8-or-better on the kernel\n");
            return 1;
        }
    }

    if (check_five_card_categories() != 0)
        return 1;

    long qualifying = 0;
    rng_seed(20260802);
    for (int iteration = 0; iteration < 50000; ++iteration) {
        /* Alternate hand sizes: the two evaluators take a different branch for
           five cards than for six or seven. */
        int n_cards = 5 + (iteration % 3);
        StdDeck_CardMask hand;
        StdDeck_CardMask_RESET(hand);
        for (int i = 0; i < n_cards;) {
            int card = (int)(rng_next() % StdDeck_N_CARDS);
            if (StdDeck_CardMask_CARD_IS_SET(hand, card))
                continue;
            StdDeck_CardMask_SET(hand, card);
            i++;
        }

        LowHandVal cpu = pe_eval_low_a5(hand);
        uint32_t kernel = eval_on_kernel(hand, n_cards);
        if (cpu != kernel) {
            fprintf(stderr, "low value on %d cards: cpu=%u kernel=%u\n",
                    n_cards, cpu, kernel);
            return 1;
        }

        int cpu_qualifies = pe_low_qualify5(cpu, LOW_QUALIFIER_8) ? 1 : 0;
        int kernel_qualifies = opencl_host_low_qualifies(kernel, 8);
        if (cpu_qualifies != kernel_qualifies) {
            fprintf(stderr, "qualifier: cpu=%d kernel=%d for low %u\n",
                    cpu_qualifies, kernel_qualifies, cpu);
            return 1;
        }
        qualifying += cpu_qualifies;
    }

    /* Guard against a run where nothing qualifies, which would make the
       qualifier comparison above vacuous. */
    if (qualifying == 0) {
        fprintf(stderr, "no qualifying low drawn: the comparison proved nothing\n");
        return 1;
    }

    printf("OpenCL low evaluator matches the CPU (%ld qualifying lows).\n", qualifying);
    return 0;
}

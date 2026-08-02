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

static uint32_t eval_on_kernel(StdDeck_CardMask hand) {
    return opencl_host_eval_low_a5(StdDeck_CardMask_SPADES(hand),
                                   StdDeck_CardMask_CLUBS(hand),
                                   StdDeck_CardMask_DIAMONDS(hand),
                                   StdDeck_CardMask_HEARTS(hand),
                                   7);
}

int main(void) {
    /* The showdown that motivated the fix: two pairs among seven cards, and a
       qualifying 8-7-6-5-4 to be found underneath them. */
    {
        const char *cards[] = {"7h", "6s", "8h", "4c", "5c", "5s", "6d"};
        StdDeck_CardMask hand;
        StdDeck_CardMask_RESET(hand);
        for (int i = 0; i < 7; ++i) {
            char text[3] = {cards[i][0], cards[i][1], '\0'};
            int index;
            StdDeck_stringToCard(text, &index);
            StdDeck_CardMask_SET(hand, index);
        }

        LowHandVal cpu = pe_eval_low_a5(hand);
        uint32_t kernel = eval_on_kernel(hand);
        if (cpu != kernel) {
            fprintf(stderr, "8-7-6-5-4: cpu=%u kernel=%u\n", cpu, kernel);
            return 1;
        }
        if (!opencl_host_low_qualifies(kernel, 8)) {
            fprintf(stderr, "8-7-6-5-4 must qualify 8-or-better on the kernel\n");
            return 1;
        }
    }

    long qualifying = 0;
    rng_seed(20260802);
    for (int iteration = 0; iteration < 50000; ++iteration) {
        StdDeck_CardMask hand;
        StdDeck_CardMask_RESET(hand);
        for (int i = 0; i < 7;) {
            int card = (int)(rng_next() % StdDeck_N_CARDS);
            if (StdDeck_CardMask_CARD_IS_SET(hand, card))
                continue;
            StdDeck_CardMask_SET(hand, card);
            i++;
        }

        LowHandVal cpu = pe_eval_low_a5(hand);
        uint32_t kernel = eval_on_kernel(hand);
        if (cpu != kernel) {
            fprintf(stderr, "low value: cpu=%u kernel=%u\n", cpu, kernel);
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

/*
 * ofc_monte_carlo_simple.cl - Simplified OpenCL kernel for Intel GPU
 *
 * Simplified version that avoids complex memory address space issues
 * that cause compilation failures on Intel Iris Graphics.
 */

#define OFC_TOP_CARDS         3
#define OFC_MIDDLE_CARDS      5
#define OFC_BOTTOM_CARDS      5
#define OFC_TOTAL_CARDS       13
#define OFC_NUM_HANDS         3
#define STD_DECK_N_CARDS      52
#define OPENCL_BLOCK_SIZE     256

typedef enum {
    OFC_TOP = 0,
    OFC_MIDDLE = 1,
    OFC_BOTTOM = 2
} ofc_position_t;

typedef struct {
    ulong hands[3];
    int card_count[3];
} ofc_hand_gpu_t;

// Simple RNG function
uint simple_rand(uint seed, uint iteration) {
    uint state = seed + iteration;
    state = (state * 1103515245U + 12345U) & 0x7FFFFFFF;
    return state;
}

// Basic hand evaluation (simplified)
int evaluate_simple(ulong hand_mask) {
    // Very basic evaluation - count bits (number of cards)
    int count = 0;
    ulong temp = hand_mask;
    while (temp) {
        count += temp & 1;
        temp >>= 1;
    }
    return count * 1000; // Simple strength based on card count
}

// Check if hand hierarchy is valid
bool is_valid_hierarchy(ofc_hand_gpu_t* hand) {
    if (hand->card_count[OFC_TOP] != OFC_TOP_CARDS ||
        hand->card_count[OFC_MIDDLE] != OFC_MIDDLE_CARDS ||
        hand->card_count[OFC_BOTTOM] != OFC_BOTTOM_CARDS) {
        return false;
    }

    int top_strength = evaluate_simple(hand->hands[OFC_TOP]);
    int middle_strength = evaluate_simple(hand->hands[OFC_MIDDLE]);
    int bottom_strength = evaluate_simple(hand->hands[OFC_BOTTOM]);

    return (bottom_strength >= middle_strength) && (middle_strength >= top_strength);
}

__kernel void ofc_monte_carlo_simple(
    __global ofc_hand_gpu_t* partial_hands,
    __global int* cards,
    __global int* positions,
    __global float* foul_risks,
    __global int* num_simulations
) {
    int gid = get_global_id(0);
    int hand_idx = gid;

    // Simple bounds check
    if (hand_idx >= get_global_size(0)) return;

    ofc_hand_gpu_t hand = partial_hands[hand_idx];
    int card = cards[hand_idx];
    int position = positions[hand_idx];
    int sims = num_simulations[hand_idx];

    // Place the card
    if (position >= 0 && position < OFC_NUM_HANDS) {
        hand.hands[position] |= (1UL << card);
        hand.card_count[position]++;
    }

    int foul_count = 0;
    int valid_count = 0;

    // Run simulations
    for (int sim = 0; sim < sims; sim++) {
        ofc_hand_gpu_t sim_hand = hand;
        uint rand_state = simple_rand(gid + 123, sim);

        // Fill remaining cards randomly (simplified)
        int cards_needed = OFC_TOTAL_CARDS -
                          (sim_hand.card_count[0] +
                           sim_hand.card_count[1] +
                           sim_hand.card_count[2]);

        // Simple random card placement
        for (int i = 0; i < cards_needed; i++) {
            rand_state = simple_rand(rand_state, i);
            int random_card = rand_state % STD_DECK_N_CARDS;
            int random_pos = (rand_state >> 8) % OFC_NUM_HANDS;

            // Check if card is already used (simplified check)
            bool card_used = false;
            for (int pos = 0; pos < OFC_NUM_HANDS; pos++) {
                if (sim_hand.hands[pos] & (1UL << random_card)) {
                    card_used = true;
                    break;
                }
            }

            if (!card_used) {
                // Try to place card if position has space
                int max_cards = (random_pos == OFC_TOP) ? OFC_TOP_CARDS :
                               (random_pos == OFC_MIDDLE) ? OFC_MIDDLE_CARDS : OFC_BOTTOM_CARDS;

                if (sim_hand.card_count[random_pos] < max_cards) {
                    sim_hand.hands[random_pos] |= (1UL << random_card);
                    sim_hand.card_count[random_pos]++;
                }
            }
        }

        valid_count++;

        // Check if completed hand is valid
        if (!is_valid_hierarchy(&sim_hand)) {
            foul_count++;
        }
    }

    // Calculate foul risk
    if (valid_count > 0) {
        foul_risks[hand_idx] = (float)foul_count / (float)valid_count;
    } else {
        foul_risks[hand_idx] = 1.0f; // All simulations were invalid
    }
}
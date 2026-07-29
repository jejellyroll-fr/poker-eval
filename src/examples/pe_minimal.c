/* pe_minimal.c - Minimal example using pe_* Engine + Betting APIs */

#include <stdio.h>
#include <stdint.h>
#include <poker_eval/engine/engine_api.h>
#include <poker_eval/engine/betting_api.h>

int main(void)
{
    pe_engine_t *eng = NULL;
    pe_game_state_t *st = NULL;
    pe_betting_engine_t *be = NULL;
    pe_status_t s;

    s = pe_engine_create_default(&eng);
    if (s != PE_STATUS_OK)
    {
        printf("pe_engine_create_default failed: %s\n", pe_error_string(s));
        return 1;
    }

    s = pe_game_state_create(NULL, NULL, 2, &st);
    if (s != PE_STATUS_OK)
    {
        printf("pe_game_state_create failed: %s\n", pe_error_string(s));
        return 1;
    }

    s = pe_engine_setup_hand(eng, st, 0);
    if (s != PE_STATUS_OK)
    {
        printf("pe_engine_setup_hand failed: %s\n", pe_error_string(s));
        return 1;
    }

    int64_t pot = 0;
    s = pe_engine_get_pot_size(st, &pot);
    if (s == PE_STATUS_OK)
        printf("Initial pot: %lld\n", (long long)pot);

    s = pe_betting_engine_create(BETTING_NO_LIMIT, NULL, &be);
    if (s != PE_STATUS_OK)
    {
        printf("pe_betting_engine_create failed: %s\n", pe_error_string(s));
        return 1;
    }

    const BettingAction *actions = NULL;
    int n = 0;
    s = pe_betting_engine_get_valid_actions(be, st, 0, &actions, &n);
    if (s == PE_STATUS_OK)
    {
        printf("Player 0 valid actions: %d\n", n);
        for (int i = 0; i < n; i++)
        {
            printf("  action=%d min=%lld max=%lld valid=%d\n", actions[i].action,
                   (long long)actions[i].min_amount, (long long)actions[i].max_amount,
                   actions[i].is_valid ? 1 : 0);
        }
    }

    pe_betting_engine_destroy(be);
    pe_game_state_destroy(st);
    pe_engine_destroy(eng);
    return 0;
}

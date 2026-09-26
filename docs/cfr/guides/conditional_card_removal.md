# Conditional Card Removal in Sampled Deals

Issue #259. Test: `tests/test_pe_conditional_card_removal.c`. Oracle:
`tests/support/pe_conditional_oracle.h`.

## The invariant

> Hidden cards belonging to players who have already acted remain removed from
> the deck according to the posterior range implied by their observed actions.

In poker an observed action carries information about the acting player's
private cards, and that information changes which cards are still available to
everyone behind him. The example the issue opens with:

```
Board: 3 3 5
Player A folds
```

If A's strategy almost never folds a hand containing a three, then conditioning
on the observed fold makes it *less* likely that A holds a three. The remaining
threes therefore become *more* likely to reach another player or a future
runout. A correct imperfect-information solver obtains this from conditional
ranges and blockers. It must not need a special-case card-removal rule per
game, per street, or per action.

## Where it comes from

The statement is one line of probability, and nothing in the codebase
implements it as a special case:

```
P(hand_p | observed actions)  proportional to  prior_p(hand_p) * product_a P(a | hand_p)
```

normalised over the **legal joint deals** — every deal in which no two players
share a card and no private card sits on the board.

Three consequences matter in practice.

1. **Blockers are applied before the final normalisation.** The normalisation
   runs over legal joints, not over each player's range independently. A combo
   that blocks a large part of the other players' ranges therefore carries
   proportionally *more* posterior mass than the raw reweighting
   `prior x action_prob` suggests. Applying blockers after normalising gives a
   plausible-looking distribution that is wrong.

2. **A folded player is still a player.** Folding does not return a hand to the
   deck. The folded player's cards stay out of the deck according to his
   *fold-conditioned* posterior range, which is what the deal has to carry.

3. **Action conditioning belongs to the caller, not the sampler.**
   `pe_preflop_deal_sampler_*` samples unconditionally from the ranges it is
   handed, by design: it is a deal primitive, not a game model. A caller that
   has observed an action expresses the posterior by handing it a
   reach-conditioned range (weights multiplied by the action likelihood, then
   renormalised). The sampler then removes cards, and the importance ratio it
   returns keeps the estimator unbiased.

   `pe_holdem_river_terminal_values` is the production path that consumes a
   reach vector directly; `pe_blockers_compatible_sum` is the terminal-node
   card removal; `pe_holdem_deals_*` / `pe_omaha_deals_*` are the exact joint
   enumerators. The invariant is the composition of those pieces, which is why
   the suite tests the composition and not just each piece.

## What the suite checks

`test_pe_conditional_card_removal` builds small fixtures whose posterior can be
computed by exact enumeration, then drives the production sampler on the same
fixture and compares. The oracle never calls the sampler, the deal iterator or
the blocker code: an oracle that reuses the code under test proves only that
the code agrees with itself.

| Case | Fixture | Exact claim |
|------|---------|-------------|
| A | A folds only with aces | `P(A holds A-spades \| fold) = 1`, `P(A holds K-spades) = 0` |
| B | three combos, fold frequencies 1.0 / 0.25 / 0.0 | posterior `2/3, 1/3, 0`; with a blocking opponent `4/7, 3/7` |
| C | board `3s 3h 5d`, A never folds a three | the remaining threes become certain to be dealt (`1.0` against a prior of `0.5`) |
| D | PLO4, A continues only with four spades | a spade becomes more available after the fold |
| E | PLO5 and PLO6 | a five- or six-card hand blocks the opponent's hand exactly, `P = 0` |
| F | three players, A folds then B calls | the third player's distribution reflects *both* earlier posteriors |

Structural invariants are asserted on every sampled deal: no card in two hands,
no private card on the board, every hand legal for the variant, every sampled
marginal sums to one, and a combo the oracle gives zero posterior mass is never
emitted at all.

Monte Carlo estimates are compared against the oracle at five sigma, with the
tolerance derived from the effective sample size of the importance-weighted
estimator rather than from a fixed percentage. Seeds are fixed, so a failure
reproduces.

## What the suite rejects

Two variants of each fixture are measured alongside the correct one, so the
suite fails if either mistake is reintroduced. Both were confirmed by mutation:

- **Sampling from the unconditional range after an action was observed** — the
  suite reports 120 failures, e.g. the folded group coming back at `0.169`
  against an exact `0.5`.
- **Returning an acting player's private cards to the deck** — the suite
  reports 141 failures, e.g. a card's availability at `0.338` against an exact
  `0.0`.

## Scope and remaining work

The suite validates the deal and blocker primitives and the range-conditioning
composition. Not yet covered:

- conditioning inside a sampled traversal whose chance node sits *after* the
  action (postflop roots and bunching-style scenarios), as opposed to a caller
  that pre-conditions the ranges it hands to the sampler;
- a benchmark for the affected sampler path. The issue asks for one only if a
  correctness fix turns out to be needed, and none was: the sampled path
  matches the exact oracle on all six cases, so **no production change was
  made** for #259.

## Running it

```
cmake --build build --target test_pe_conditional_card_removal
./build/tests/test_pe_conditional_card_removal
# or, with the registered labels:
ctest --test-dir build -R test_pe_conditional_card_removal --output-on-failure
```

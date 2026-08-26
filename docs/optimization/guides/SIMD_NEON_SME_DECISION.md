# NEON / SME decision report

## Scope

This is a first native ARM64 measurement of the hot compute kernels after the
SIMD-01/SIMD-02 work. The runner identifies itself as Darwin arm64, kernel
build `T8142`; the sandbox does not expose the exact Apple product name.

The Release benchmark uses five repetitions, 128 inner loops, and reports the
median. The NEON run uses the normal runtime dispatch. The scalar run uses
`PE_DISABLE_SIMD=1` with the same executable and workload.

## Results

Results for a batch of 1,000,000 infosets:

| Kernel | NEON elements/s | Scalar elements/s | NEON speedup |
|---|---:|---:|---:|
| `strategy_batch`, 2 actions | 516,395,559 | 456,932,342 | 1.13x |
| `strategy_batch`, 3 actions | 457,897,297 | 391,268,995 | 1.17x |
| `strategy_batch`, 5 actions | 709,399,095 | 553,349,363 | 1.28x |
| `strategy_batch`, 9 actions | 1,063,368,256 | 738,368,769 | 1.44x |
| `apply_update_batch`, 5×169 | 725,906,040 | 197,733,090 | 3.67x |
| `apply_update_batch`, 5×1326 | 674,058,777 | 204,000,000 | 3.30x |
| `apply_update_batch`, 5×65535 | 650,098,424 | 201,674,272 | 3.22x |

The update kernel benefits strongly because it processes long contiguous
double spans. Regret matching has short ragged action spans; the scalar tail
and dispatch overhead reduce its gain, especially for five actions.

## Decision

Do not add SME/SME2 kernels yet. The measured regret-matching gain remains
below the plan's 1.5× engagement threshold for every tested action count, and
the update kernel is already well served by NEON. SME should be reconsidered
only after a tiled workload or a larger dense kernel appears, and should be
remeasured on an M4/M5 runner with the same protocol.

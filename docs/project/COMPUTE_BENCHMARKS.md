# Compute micro-benchmarks

`bench_pe_compute` measures the public compute kernels independently and emits
stable CSV with this schema:

```text
backend,kernel,batch,actions,combos,ns_per_element,elements_per_s
```

Build and run the CPU reference workload with:

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_EXAMPLES=ON -DBUILD_TESTS=OFF
cmake --build build/release --target bench_pe_compute
./build/release/bin/bench_pe_compute --backend cpu_ref --csv \
  > benchmarks/baseline/pe_compute.csv
```

The CI guard compares `elements_per_s` by backend/kernel/size and fails when a
row drops below 90% of the committed baseline:

```bash
./build/release/bin/bench_pe_compute --backend cpu_ref --csv > /tmp/pe_compute.csv
bash scripts/check_pe_compute_benchmark.sh \
  benchmarks/baseline/pe_compute.csv /tmp/pe_compute.csv
```

Each sample contains 128 inner kernel loops and publishes the median of five
repetitions so that short scheduler or frequency fluctuations do not dominate
the measurement. The current compute
ABI does not expose a constructor for `pe_showdown_job_t`; consequently
`vector_showdown` is not emitted by this public-contract harness until that
ABI is made constructible.

`cpu_par` requires an OpenMP-enabled build. CUDA and OpenCL use the same
executable when their adapters are compiled and available. The current public
infoset contract stores combo counts in `uint16_t`; consequently the benchmark
uses the largest representable combo count (`65535`) for the update kernel and
keeps larger poker-range sizes as a future storage-contract item.

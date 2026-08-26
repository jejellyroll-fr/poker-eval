# Runtime SIMD dispatch

The SIMD capability API exposes three deliberately different values:

- `simd_detect_capability()` reports what the host CPU supports at runtime.
- `simd_compiled_capability()` reports the highest SIMD implementation linked
  into the binary.
- `simd_runtime_capability()` returns the safe intersection used by dispatch.

On GCC and Clang x86-64 builds, the AVX2 and AVX-512 batch evaluators are
compiled with function-level `target("avx2")` and `target("avx512f")` attributes.
They do not add ISA flags to the global build. A generic binary can therefore
select AVX2 or AVX-512 at runtime when the host supports it, and falls back to
the scalar evaluator otherwise.

`pe_runtime_probe()` exposes all three values as `simd_machine`,
`simd_compiled`, and `simd`. This lets frontends distinguish a capable host
from a binary that was intentionally built without a particular kernel.

The portable CI configuration uses `-DENABLE_NATIVE_ARCH=OFF`, exports
`compile_commands.json`, rejects global `-mavx2`, `-mavx512f`, and
`-march=native` flags, and runs `test_simd_operations` plus `test_pe_runtime`.

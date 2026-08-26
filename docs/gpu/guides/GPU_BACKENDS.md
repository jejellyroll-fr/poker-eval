# Backends GPU — CUDA, OpenCL, HIP, Metal

## Ce que chaque backend sert

| Backend | Regret / stratégie | Terminal eval | Construit quand |
|---|---|---|---|
| `cuda` | oui | oui | toolkit CUDA trouvé |
| `opencl` | oui | oui | OpenCL trouvé |
| `hip` | oui | **non** | ROCm/HIP trouvé |
| `metal` | oui | **non** | macOS + framework Metal |

HIP et Metal ne portent que les noyaux de regret. Il n'existe pas encore
d'évaluateur terminal batché pour eux, et ils ne publient donc pas
`PE_CAP_GPU_TERMINAL_EVAL` : le capability bit et le comportement réel de
`terminal_eval_batch()` disent la même chose, plutôt que de laisser le
resolver router un batch terminal vers un backend qui le refusera.

## Noyaux partagés CUDA/HIP (GPU-02)

`src/gpu/common/pe_regret_kernels.inc` contient les noyaux **et** le code hôte,
écrits contre des macros. Les deux unités de compilation ne font que déclarer
comment le runtime s'épelle :

```
src/gpu/kernels_regret_cuda.cu        →  common/pe_gpu_runtime_cuda.h
src/gpu/hip/kernels_regret_hip.hip.cpp →  common/pe_gpu_runtime_hip.h
```

Elles font 23 lignes chacune et sont identiques à la casse du nom du runtime
près. L'arithmétique n'est écrite qu'une fois, donc les deux backends ne
peuvent pas diverger silencieusement.

Le fichier HIP est compilé en C++ parce que hipcc compile du C++, mais il
n'utilise ni classes, ni templates, ni STL, ni exceptions.

## Metal (GPU-03)

Tout l'Objective-C tient dans `src/gpu/metal/pe_regret_metal.m`, derrière une
ABI C : le solveur ne sait pas ce qu'est un `MTLDevice`. Le source MSL est
compilé à la création du contexte (`newLibraryWithSource:`), ce qui évite une
dépendance de build sur la chaîne d'outils Metal.

**Mémoire unifiée.** C'est la raison d'être du backend sur Apple Silicon. Les
tableaux du solveur sont enveloppés par `newBufferWithBytesNoCopy:` quand le
pointeur est aligné sur une page : le GPU lit et écrit l'allocation hôte
directement, sans transfert. Quand l'alignement n'est pas au rendez-vous — le
`malloc` de C ne permet pas de l'exiger — un buffer `storageModeShared` est
utilisé et la copie reste interne à un seul système mémoire.

## Validation de parité, par étage

`pe_runtime_probe()` compare chaque backend GPU à `cpu_ref` avant de publier
ses capabilities, et n'ouvre **que les verrous qu'il a prouvés**. Un backend
qui sert les updates de regret sans évaluateur terminal obtient le seul verrou
qu'il a mérité ; un étage qui refuse et un étage qui répond faux sont traités
pareil — ni l'un ni l'autre n'ouvre son verrou.

## Vérifier

```bash
ctest --test-dir build -R 'test_compute_regret_gpu|test_backend_parity' --output-on-failure
```

`test_compute_regret_gpu` compare `hip` et `metal` à `cpu_ref` et **skippe**
(code 77) quand aucun des deux n'a de device. Sur une machine sans GPU, un
`SKIP` est le résultat attendu — jamais un succès silencieux.

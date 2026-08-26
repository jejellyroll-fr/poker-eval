# Plan d'amélioration — couche compute hétérogène

**Cible** : un solveur unique qui exploite CPU x86 (AVX2/AVX-512), CPU ARM
(NEON/SME), GPU NVIDIA (CUDA), GPU AMD (HIP), GPU Apple (Metal) et OpenCL, sur
une ou plusieurs machines, sans que l'algorithme CFR sache sur quoi il tourne.

**Statut du dépôt** : les contrats sont déjà là et ils sont bons. `pe_compute_ops_t`
([pe_compute.h](include/poker_eval/solver/pe_compute.h)) est exactement l'ABI de
backend décrite dans la réflexion, `pe_runtime_probe()` fait déjà la découverte de
capacités, les capabilities sont déjà des bits sérialisables, et il existe une
suite de parité CPU/GPU. **Ce plan ne refait pas cette architecture.** Il attaque
ce que l'audit du code montre comme réellement bloquant.

**Conclusion de l'audit, en une phrase** : le contrat est vectoriel mais les
données ne le sont pas. Le traversal vectoriel reconstruit une structure scalaire
`array-of-structs` à la frontière du batch, et tout ce qui suit — merge, réduction,
application — paie ce choix. Aucune quantité de SIMD, de SME ou de Metal ne
compensera cela. **Le Lot 1 vaut plus que les Lots 2, 3 et 4 réunis.**

---

## Table des lots

| Lot | Objet | Dépend de | Gain attendu | Risque |
|---|---|---|---|---|
| **L0** | Mesure et garde-fous | — | aucun (prérequis) | faible |
| **L1** | Chemin chaud : layout SoA, allocation, réduction | L0 | **majeur, indépendant du matériel** | moyen |
| **L2** | Portabilité SIMD et dispatch runtime | L1 | moyen à fort | moyen |
| **L3** | Backends GPU : gate, Metal, HIP | L1 | fort sur gros batches | moyen |
| **L4** | Exécution distribuée | L1, L2 | spéculatif tant que L1 n'est pas fait | élevé |
| **L5** | CI et conformité multi-arch | L2, L3 | anti-régression | faible |

Ordre imposé : **L0 → L1** avant tout le reste. L2 et L3 sont parallélisables
entre eux une fois L1 mergé. L4 ne doit pas démarrer avant que L1 ait livré ses
chiffres.

---

# L0 — Mesure et garde-fous

Rien dans ce plan ne doit être mergé sans chiffre avant/après. Il existe des
benchmarks (`src/benchmarks/`, `bench_reproducible_suite.c`) mais aucun baseline
versionné ni détection de régression.

## PERF-01 — Harnais de micro-benchmark des kernels compute

**Problème** — Les benchmarks existants mesurent des solves entiers. On ne peut
pas attribuer un gain à `strategy_batch` plutôt qu'à `apply_update_batch`, ni
comparer deux backends sur le même kernel.

**Travail** — Un exécutable `bench_pe_compute` qui, pour un backend donné
(`--backend cpu_ref|cpu_par|cuda|opencl`), mesure isolément :
`strategy_batch`, `apply_update_batch`, `terminal_eval_batch`, `vector_showdown`,
sur des tailles de batch représentatives (10³, 10⁵, 10⁶ infosets ; 2, 3, 5, 9
actions ; 169, 1326, 270 725 combos). Sortie CSV stable :
`backend,kernel,batch,actions,combos,ns_per_element,elements_per_s`.

**DoD** — L'exécutable tourne sur un build CPU seul. Le CSV est déterministe à
±3 % sur trois exécutions consécutives. Un fichier
`benchmarks/baseline/pe_compute.csv` est commité avec les mesures de la machine
de référence, et le README documente comment le régénérer.

**Vérification**
```bash
cmake --build build/release --target bench_pe_compute && ./build/release/bench_pe_compute --backend cpu_par --csv
```

## PERF-02 — Compteurs d'allocation et de taille de batch dans la télémétrie

**Problème** — Le port de télémétrie existe (`pe_telemetry.h`) mais ne rapporte
pas ce qui dimensionne le coût : nombre d'updates émis, taille crête du batch,
nombre d'allocations par itération.

**Travail** — Ajouter au flux de télémétrie, par itération : `updates_emitted`,
`updates_after_reduce`, `batch_peak_bytes`, `vec_allocs`, `merge_comparisons`.
Ce sont les cinq nombres qui rendront le Lot 1 démontrable.

**DoD** — `pe_vector_sim` et `pe_preflop_solve` savent afficher ces compteurs.
Sur un solve river hold'em 1326 combos, `merge_comparisons` est mesuré et
consigné dans le ticket PERF-03 comme point de départ.

## PERF-03 — Garde anti-régression en CI

**Travail** — Un job CI qui exécute `bench_pe_compute`, compare au baseline
commité et échoue au-delà de −10 % sur n'importe quel kernel. Le seuil est
volontairement lâche : le but est d'attraper un facteur 2, pas le bruit.

**DoD** — Le job tourne sur `ubuntu-latest`, dure moins de 5 minutes, et sa
sortie nomme le kernel régressé.

---

# L1 — Chemin chaud du solveur

C'est le cœur du plan. Les cinq tickets ci-dessous sont indépendants du matériel
et bénéficient à **tous** les backends présents et futurs.

## HOT-01 — Batch d'updates en Structure-of-Arrays, par infoset

**Problème (mesuré dans le code)** — Le traversal vectoriel parcourt l'arbre en
forme vectorielle, puis, à
[traversal_full_vector.c:318-350](src/solver/domain/traversal_full_vector.c:318),
matérialise **un `pe_update_t` de 24 octets par couple (action, combo)** et le
pousse un par un dans un tableau croissant. Pour un nœud hold'em à 1326 combos et
5 actions : 6630 structures, ~159 Ko, par nœud et par visite. Le champ `infoset`
est répété 6630 fois avec la même valeur.

La définition en cause,
[pe_batch.h:20-27](include/poker_eval/solver/pe_batch.h:20) :

```c
typedef struct {
    pe_infoset_id_t infoset;   /* identique pour 6630 entrées consécutives */
    uint16_t action;
    uint16_t combo;
    double delta;
    double average_delta;
} pe_update_t;
```

C'est exactement l'`AoS` que la forme vectorielle existe pour éviter. Le
traversal est vectoriel, sa sortie ne l'est pas.

**Travail** — Introduire une forme SoA groupée par infoset, à côté de la forme
actuelle (pas de suppression dans ce ticket) :

```c
typedef struct {
    pe_infoset_id_t infoset;
    uint16_t actions;
    uint16_t combos;
    uint32_t offset;           /* dans les tableaux plats ci-dessous */
} pe_update_group_t;

typedef struct {
    pe_update_group_t *groups;
    size_t group_count;
    float *deltas;             /* [offset .. offset + actions*combos)  */
    float *average_deltas;
    size_t value_count, value_capacity;
    uint64_t iteration;
} pe_update_soa_t;
```

Le traversal écrit directement dans `deltas` en balayage contigu action-majeur ;
il n'y a plus de `push` par élément.

Corollaire à traiter dans le même ticket : la boucle d'émission recalcule
`opponent_reach` en O(joueurs) **à l'intérieur** de la boucle sur les combos. Le
produit des reaches adverses ne dépend pas de l'action ; il se calcule une fois
par nœud dans un vecteur temporaire.

**DoD** — `pe_update_soa_t` est publié dans `pe_batch.h`. Le traversal vectoriel
l'émet. La forme `pe_update_t` reste disponible et testée pour les traversals
outcome/external. `updates_emitted` (PERF-02) est inchangé, `batch_peak_bytes`
baisse d'au moins 40 % sur le solve river de référence.

**Vérification**
```bash
ctest --test-dir build/debug -R 'test_pe_batch|test_solver_vector|test_backend_parity' --output-on-failure
```

**Attention** — La parité numérique bit-à-bit avec la forme actuelle n'est pas
garantie si les `double` deviennent des `float` ici. Ce ticket garde `double`
pour les deltas ; le passage en `float` est le ticket HOT-05, séparé exprès.

## HOT-02 — `apply_update_batch` : un appel de storage par infoset, pas par combo

**Problème** — Dans
[compute_cpu_par.c:279-283](src/solver/adapters/compute_cpu_par.c:279), pour
**chaque** update scalaire, l'adaptateur appelle `storage->shape()` puis deux
fois `storage->values()` — trois appels de fonction indirects, non inlinables,
pour écrire deux `double`. Ensuite la région OpenMP (`#pragma omp parallel for`,
ligne 300) ne fait plus que des stores dispersés : tout le calcul a été fait en
série dans la boucle de validation qui précède. Le parallélisme annoncé par
`PE_CAP_CPU_PARALLEL` ne parallélise ici que des écritures mémoire.

**Travail** — Réécrire `apply_update_batch` sur la forme SoA de HOT-01 : résoudre
`shape`/`values` **une fois par groupe d'infoset**, puis appliquer
`actions × combos` valeurs dans une boucle plate et vectorisable. La région
parallèle porte alors sur les groupes, et chaque groupe contient du vrai calcul
(discount DCFR, clamp CFR+, pondération d'averaging).

**DoD** — Le nombre d'appels à `storage->values()` par itération passe de
`O(updates)` à `O(infosets touchés)`, mesuré par un compteur de test. Le speedup
de `apply_update_batch` sur `bench_pe_compute` est reporté dans la PR. Parité
numérique exacte avec `cpu_ref` maintenue (`test_backend_parity`).

## HOT-03 — Réduction des batches en O(n log n)

**Problème** — `pe_update_batch_merge`
([batch.c:99-125](src/solver/domain/batch.c:99)) fait un balayage linéaire de la
destination pour **chaque** élément de la source : O(n·m). Il est appelé dans la
boucle principale du solveur à
[solver.c:873](src/solver/domain/solver.c:873), une fois par trajectoire
échantillonnée. Le commentaire juste au-dessus explique que `sample_batch_size` a
été ajouté « pour rendre la taille de batch configurée utile aux ranges préflop
larges » — c'est précisément le réglage qui fait exploser ce merge en quadratique.
Augmenter `sample_batch_size` dans Studio dégrade donc les performances au lieu de
les améliorer, au-delà d'un seuil.

`pe_update_batch_reduce` fait déjà, lui, un tri déterministe correct.

**Travail** — Remplacer l'accumulation par merges successifs par une collecte des
`samples` batches suivie d'un unique `pe_update_batch_reduce`, qui est déjà
O(n log n) et déjà déterministe. Sur la forme SoA (HOT-01), la réduction devient
une fusion par groupe d'infoset, sans tri global.

**DoD** — `merge_comparisons` (PERF-02) croît linéairement, et non plus
quadratiquement, avec `sample_batch_size`. Un test paramétré vérifie que le
résultat est identique pour `sample_batch_size` ∈ {1, 8, 64}. L'ordre de réduction
déterministe documenté dans `pe_batch.h` est préservé.

## HOT-04 — Arène pour les vecteurs du traversal

**Problème** — `pe_vec_alloc` fait un `calloc`
([reach.c:33](src/solver/domain/reach.c:33)) et le traversal vectoriel alloue puis
libère `actions × player_count` vecteurs **par nœud visité**
(`traversal_full_vector.c`, boucles de `pe_vec_free` en fin de fonction). Pour un
arbre postflop, c'est des millions de `calloc`/`free` par itération, sur des
tailles identiques, avec mise à zéro systématique.

**Travail** — Une arène par thread, dimensionnée à la profondeur maximale de
l'arbre × largeur maximale, avec discipline pile (`mark`/`release` à l'entrée et
la sortie d'un nœud). Les vecteurs deviennent des `pe_vec_wrap` sur l'arène.
La mise à zéro n'est faite que là où la sémantique l'exige.

**DoD** — `vec_allocs` (PERF-02) devient O(profondeur) par itération au lieu de
O(nœuds). Aucune fuite sous ASan/LSan (`infra-sanitizers.yml` passe). Le
traversal reste réentrant : l'arène est strictement thread-locale.

## HOT-05 — Précision : FP32 pour les tableaux chauds, accumulateurs compensés

**Problème et nuance** — `pe_storage_port.h:143` expose les valeurs en `double*`,
et `pe_vec_t` est un tableau de `double`. L'en-tête `pe_vector.h` **justifie
explicitement** ce choix : sommer 270 725 doubles naïvement perd ~√n ulps, ce qui
sortirait des tolérances de convergence. Cette justification est correcte et ce
ticket ne la balaie pas.

Mais elle ne s'applique pas uniformément. Les regrets et la stratégie moyenne sont
des tableaux **stockés**, dominés par la bande passante ; les sommes sont des
**réductions**, dominées par la précision. Le contrat compute le reconnaît déjà à
demi-mot : `pe_infoset_batch_t.regrets` est un `const float *`, alors que le
storage derrière est en `double`. Il y a donc déjà une conversion à chaque
traversée du port.

Second point, mesurable : `pe_vec_sum` et `pe_vec_dot`
([reach.c:130](src/solver/domain/reach.c:130) et
[reach.c:154](src/solver/domain/reach.c:154)) contiennent un
`if (fabs(sum) >= fabs(x))` **dans la boucle**. C'est une dépendance de branche
imprévisible par élément : elle interdit toute vectorisation automatique et coûte
une mauvaise prédiction sur données mixtes. La forme Neumaier admet une écriture
sans branche, et une variante à N accumulateurs indépendants qui est à la fois
vectorisable et plus précise que la version scalaire actuelle.

**Travail, en deux temps**
1. Réécrire `pe_vec_sum`/`pe_vec_dot` en Neumaier **sans branche**, sur 4 ou 8
   accumulateurs partiels réduits en fin de boucle. Aucun changement de type.
2. Introduire un stockage FP32 **optionnel** pour regrets et moyenne, derrière une
   option de configuration, avec les accumulateurs de réduction et
   d'exploitabilité maintenus en `double`. Le port storage gagne une variante
   `values_f32`.

**DoD** — Étape 1 : parité à ±1 ulp avec l'implémentation actuelle sur un jeu de
vecteurs adverses (mélange de grands et petits termes), et vectorisation confirmée
par le rapport d'optimisation du compilateur. Étape 2 : sur trois solves de
référence, la trajectoire d'exploitabilité en FP32 est comparée à la FP64 et
l'écart est **documenté dans le CHANGELOG**, pas dissimulé. FP32 n'est pas activé
par défaut tant que cet écart n'est pas mesuré sur PLO.

**Attention** — Ce ticket change des résultats numériques. Il relève de
l'annexe A du backlog v3 (« tickets dont le comportement change délibérément ») et
doit être seul dans sa PR.

---

# L2 — Portabilité SIMD et dispatch runtime

## SIMD-01 — Sortir le dispatch SIMD du compile-time

**Problème (le plus gros obstacle au déploiement multi-machine)** — La détection
est runtime en apparence seulement. Dans
[simd_operations.c:92-114](src/equity/simd_operations.c:92), le test CPUID est
imbriqué dans un `#if defined(__AVX2__)`, et le commentaire le dit franchement :
*« Generic builds intentionally omit -mavx2/-mavx512f and must therefore stay on
the scalar path. »*

Conséquence directe : un binaire portable, compilé sans `-mavx2`, **restera
scalaire à vie sur une machine AVX-512**. La seule façon d'avoir de l'AVX-512 est
`-march=native`, qui produit un binaire qui plante ailleurs. Pour un solveur qu'on
déploie sur un parc hétérogène — l'objectif affiché — c'est rédhibitoire.

À noter aussi : `-march=native` est explicitement désactivé sur Apple
([CMakeLists.txt:193](CMakeLists.txt:193), `AND NOT APPLE`) et aucun
`-mcpu=apple-m*` ne le remplace. Les builds macOS ARM tournent donc sur la ligne
de base AArch64.

**Travail** — Passer aux kernels multi-versionnés :
- un fichier par ISA (`kernel_avx2.c`, `kernel_avx512.c`, `kernel_neon.c`,
  `kernel_scalar.c`), chacun compilé en cible CMake séparée avec **ses seuls**
  flags ISA, jamais globaux ;
- un registre de kernels résolu une fois au démarrage à partir du vrai CPUID
  (`__builtin_cpu_supports` sur GCC/Clang, `getauxval`/`sysctlbyname` sur ARM),
  indépendamment des macros de compilation ;
- `simd_detect_capability()` cesse de mentir : elle rapporte ce que la machine
  sait faire, et une seconde fonction rapporte ce que le binaire embarque.

Sur Apple, ajouter la sonde `-mcpu=apple-m1`/`apple-m2`/… avec repli, plutôt
qu'un `NOT APPLE` sec.

**DoD** — Un binaire construit sans aucun flag ISA sélectionne AVX2 sur une
machine AVX2, vérifié par `PE_FORCE_*` et par le test de dispatch. Aucune ISA
n'apparaît dans les flags globaux du projet. `pe_runtime_probe()` distingue
« supporté par la machine » de « compilé dans ce binaire ».

**Vérification**
```bash
cmake -B build/portable -DENABLE_NATIVE_ARCH=OFF && cmake --build build/portable && ctest --test-dir build/portable -R 'simd_dispatch' --output-on-failure
```

## SIMD-02 — Kernels NEON pour les chemins aujourd'hui x86 seulement

**Problème** — `CMakeLists.txt` documente lui-même le trou : *« The OFC kernels
in src/ofc/ofc_simd.c are x86 intrinsics only (SSE2/AVX2/AVX-512); there is no
NEON implementation »*, et fixe `OFC_SIMD_BATCH_SIZE 1` sur ARM. Côté solveur,
la seule intrinsèque est un `_mm256_max_pd` dans `cfr_storage.c:27-36` — avec un
`#include <arm_neon.h>` présent mais aucun chemin NEON correspondant.

**Travail** — Après HOT-01/HOT-02, les kernels chauds sont des boucles plates sur
des tableaux plats. Écrire les variantes NEON de : `strategy_batch` (regret
matching), l'application d'update, la réduction compensée de HOT-05. Priorité à
ces trois-là ; OFC vient après, sur mesure.

**DoD** — `SIMD_NEON` est sélectionné et **utilisé** sur Apple Silicon et
Graviton. `bench_pe_compute` montre un gain NEON mesuré sur les trois kernels.
Parité exacte NEON ↔ scalaire dans `test_backend_parity`.

## SIMD-03 — SME/SME2 : évaluer, ne pas s'engager

**Position** — SME est intéressant mais c'est un moteur matriciel : il ne paiera
que si les kernels sont denses et tuilés. Après L1 on saura s'ils le sont. Ce
ticket est une **étude chiffrée**, pas une implémentation.

**Travail** — Sur une machine M4/M5 : mesurer le regret matching batché en
NEON explicite, via Accelerate (BLAS), et en SME streaming mode, sur les tailles
de `bench_pe_compute`. Produire un rapport de décision dans
`docs/optimization/guides/`.

**DoD** — Le rapport recommande ou écarte SME avec des chiffres. Aucun code SME
n'est mergé dans ce ticket. Si le gain est inférieur à 1,5× sur NEON, on écarte
et on l'écrit.

---

# L3 — Backends GPU

## GPU-01 — Ouvrir la porte de parité en production *(bug, à traiter en premier)*

**Problème** — `pe_gpu_terminal_eval_gate_open()` et
`pe_gpu_regret_update_gate_open()`
([registry.c:26-47](src/solver/domain/registry.c:26)) sont un verrou volontaire :
un backend n'annonce ses capacités GPU qu'après passage du test de parité. Le
mécanisme est sain. Mais les **seuls** appelants dans tout le dépôt sont
[test_solver_gpu_parity.c:245-246](tests/test_solver_gpu_parity.c:245).

Autrement dit : en production, les portes sont fermées en permanence.
`compute_cuda_capabilities()` retourne 0, `pe_solver_validation_caps()` masque
`PE_CAP_GPU_*`, et **aucun solve utilisateur ne peut atteindre le GPU**, même sur
une machine où CUDA fonctionne parfaitement. Tout le travail GPU déjà écrit
(`kernels_regret_cuda.cu`, `gpu_cfr_opencl.c`, `compute_gpu_updates.c`) est
inatteignable hors tests.

**Travail** — Donner à la porte un ouvreur de production : au premier `create()`
d'un backend GPU, exécuter le vecteur de parité de référence (petit, quelques
millisecondes) contre `cpu_ref` ; ouvrir la porte si et seulement s'il passe.
Consigner le résultat dans `pe_runtime_backend_info_t.validated` et
`.reason`, deux champs qui existent déjà et attendent exactement cette
information. Prévoir une variable d'environnement d'échappement documentée pour
le diagnostic.

**DoD** — Sur une machine CUDA, `pe_runtime_probe()` rapporte
`validated = 1` et un solve configuré sur `cuda` atteint réellement le GPU. Sur
une machine sans CUDA, `reason` explique pourquoi. `test_solver_gpu_parity`
continue de passer.

**Priorité** — Ce ticket est le meilleur rapport gain/effort de tout le plan.
Il ne demande aucun kernel nouveau : il rend accessible ce qui est déjà écrit.

## GPU-02 — Noyaux partagés CUDA/HIP par macros

**Problème** — `kernels_regret_cuda.cu` et `kernels_regret_opencl.c` sont deux
implémentations séparées du même calcul. Ajouter HIP en ferait une troisième, et
Metal une quatrième — quatre endroits où corriger le même bug.

**Travail** — Extraire les corps de kernels dans des en-têtes
`src/gpu/common/*.h` neutres, avec les macros `PE_GPU_GLOBAL` / `PE_GPU_DEVICE`
déjà esquissées dans la réflexion. `backend.cu` (nvcc) et `backend.hip.cpp`
(hipcc) les incluent. Le HIP reste écrit en C-like : pas de templates, pas de STL,
pas d'exceptions — il n'est C++ que par le compilateur.

**DoD** — Un backend HIP (`pe_compute_hip_ops()`) existe et passe
`test_backend_parity` sur une machine ROCm. Le corps du kernel de regret n'est
écrit qu'une fois. Un build sans ROCm est inchangé.

## GPU-03 — Backend Metal pour Apple Silicon

**Problème** — Aucun code Metal dans le dépôt (`grep -rli metal src/` ne trouve
qu'une mention dans un CMakeLists). Sur Mac, il n'y a donc aucun chemin GPU :
OpenCL est déprécié par Apple depuis 10.14 et CUDA n'existe pas.

**Travail** — `pe_compute_metal_ops()` implémentant le même
`pe_compute_ops_t`. Pont Objective-C **minimal** (`backend_metal.m`, quelques
centaines de lignes de plomberie : device, command queue, buffers, pipeline) plus
les kernels en `.metal`. Tout le reste du solveur reste en C et ne voit que l'ABI.

Point spécifique Apple à exploiter : la mémoire unifiée permet des
`MTLBuffer` en `storageModeShared` persistants entre itérations, donc pas de
copie hôte↔device des regrets. C'est le vrai avantage architectural de la
plateforme et il doit être dans le design dès le départ, pas ajouté après.

**DoD** — `test_backend_parity` passe avec `metal` sur Apple Silicon. Le build
Linux/Windows est strictement inchangé (le backend est conditionné par
`APPLE`). `pe_runtime_probe()` liste Metal.

**Séquencement** — Après GPU-01 (sinon le backend serait, lui aussi,
inatteignable) et après GPU-02 (pour hériter des kernels partagés).

## GPU-04 — Politique de taille de batch guidée par la mesure

**Problème** — Le piège classique du GPU est la synchronisation trop fréquente :
des dispatches minuscules coûtent plus que le calcul. `sample_batch_size`,
`terminal_batch_size` et `update_batch_size` sont aujourd'hui des réglages
utilisateur sans garde-fou.

**Travail** — Au `create()` d'un backend, un micro-benchmark détermine le seuil
de batch en dessous duquel le CPU est plus rapide. En dessous du seuil, le
routeur choisit le CPU. Le seuil est exposé en télémétrie.

**DoD** — Un solve à petites itérations n'est plus ralenti par la sélection
`AUTO` d'un GPU. Le seuil mesuré apparaît dans la sortie de `pe_runtime`.

Le resolver `pe_runtime_recommended_backend()` respecte désormais la règle de
sécurité : un GPU n'est candidat que si sa parité est validée, si
`GPU_TERMINAL_EVAL` est annoncé et si son débit mesuré dépasse le meilleur CPU.
En cas de débit absent ou à égalité, le CPU reste sélectionné. La mesure d'un
seuil de lancement propre à chaque machine reste une étape ultérieure de
profiling.

---

# L4 — Exécution distribuée

**Avertissement** — Ce lot est le plus spéculatif du plan. Il ne doit pas
démarrer avant que L1 ait livré ses chiffres : distribuer un chemin chaud
inefficace multiplie l'inefficacité par le nombre de machines, et le réseau est
plusieurs ordres de grandeur plus lent que le cache. Un solveur mono-machine
correctement optimisé peut rendre L4 inutile pour la plupart des usages.

## DIST-01 — Descripteur de capacités sérialisable

**Travail** — `pe_runtime_capabilities_t` existe déjà et contient presque tout
ce qu'il faut. Lui ajouter une sérialisation stable (le format des capabilities
est déjà réversible via `pe_caps_to_string`/`pe_caps_parse`, réutiliser cette
approche) plus les débits mesurés par PERF-01. Un worker s'annonce avec ce qu'il
sait faire **et à quelle vitesse**, jamais avec un modèle de matériel codé en dur.

**DoD** — Un descripteur fait un aller-retour texte sans perte, y compris pour
des bits de capability inconnus d'une version plus ancienne.

## DIST-02 — WorkUnit à la granularité du public state

**Position** — La bonne unité de distribution est le sous-arbre public (flop
canonique, groupe de boards, subgame), pas l'opération de regret. Une réduction
globale par nœud rendrait le cluster network-bound.

**Travail** — Définir `pe_work_unit_t` : état public, ensemble de boards, joueur
mis à jour, intervalle d'itérations, ranges, snapshot de regrets. Le
worker choisit **lui-même** son backend local pour l'exécuter. Le
coordinateur n'en sait rien.

**DoD** — Un WorkUnit exécuté sur `cpu_ref` et sur `cuda` produit le même
résultat. La sérialisation est testée par aller-retour.

## DIST-03 — Coordinateur et répartition proportionnelle au débit

**Travail** — Un coordinateur qui distribue les WorkUnits proportionnellement aux
débits annoncés (DIST-01), pas à parts égales. Protocole binaire simple sur TCP
d'abord ; MPI/NCCL/RCCL restent des optimisations **intra-cluster homogène**, pas
la fondation — un parc mêlant macOS ARM64, Linux x86-64 et Windows ne peut pas
reposer sur `mpirun`.

**DoD** — Deux workers hétérogènes (un CPU seul, un GPU) résolvent ensemble un
solve river et le résultat est identique au solve mono-machine. La machine lente
reçoit proportionnellement moins de travail, vérifié par la télémétrie.

## DIST-04 — CFR-D pour le découpage sans état global

**Position** — La décomposition CFR-D permet de résoudre des sous-parties sans
maintenir la stratégie entière en mémoire, avec des garanties théoriques dans son
cadre. `PE_CAP_SUBGAME_RESOLVE` existe déjà et `cfr_resolve.c` est en place.

**Travail** — Étudier ce que `cfr_resolve.c` fournit déjà et ce qui manque pour
en faire l'unité de distribution de DIST-02. Ticket d'étude, pas
d'implémentation.

---

# L5 — CI et conformité multi-arch

## CI-01 — Exécuter les tests sur ARM, pas seulement les compiler

**Problème** — Tous les jobs de `ci.yml` tournent sur `ubuntu-latest` (x86-64).
L'ARM n'apparaît que dans `release.yml`, en **cross-compilation** (donc jamais
exécuté) et dans `examples-build.yml` sur `macos-14`. Aucun test NEON n'est donc
jamais exécuté sur une machine ARM en CI. Les kernels NEON de SIMD-02 seraient
livrés sans filet.

**Travail** — Ajouter `ubuntu-24.04-arm` et `macos-14` à la matrice de test
principale, pas seulement de build.

**DoD** — `ctest` complet passe sur ARM natif en CI. Un échec spécifique à ARM
casse la CI.

## CI-02 — Matrice de parité des backends

**Travail** — Un job qui, pour chaque backend disponible sur le runner, exécute
`test_backend_parity` contre `cpu_ref` comme oracle, et publie une matrice
lisible : backend × kernel × verdict. C'est ce tableau qui rendra crédible
l'ajout de HIP, Metal et SME.

**DoD** — La matrice est publiée en artefact de CI. Un backend qui n'a pas de
runner est marqué « non couvert », jamais « OK ».

## CI-03 — Build portable sans flag ISA en CI

**Travail** — Un job qui construit avec `-DENABLE_NATIVE_ARCH=OFF` et vérifie
qu'aucun flag ISA n'a fuité dans les flags globaux, puis que le dispatch runtime
de SIMD-01 sélectionne bien le meilleur kernel disponible.

**DoD** — Le job échoue si `-mavx2`, `-mavx512f` ou `-march=native` apparaît dans
les options de compilation globales.

---

# Ce que ce plan écarte volontairement

- **Une couche d'abstraction GPU universelle** (Kokkos, SYCL comme fondation,
  Vulkan comme chemin principal). `pe_compute_ops_t` est déjà cette couche, et
  elle est meilleure : elle est en C, elle est à la bonne granularité (des
  kernels de solveur, pas des primitives génériques), et elle laisse chaque
  backend exploiter ce que son matériel a de particulier. Une couche universelle
  masquerait précisément la mémoire unifiée d'Apple et les warp intrinsics de
  NVIDIA.
- **OpenMP target offload comme backend GPU.** Il ne cible ni Metal, donc il ne
  résout pas le cas Apple, et il ne remplacerait CUDA/HIP nulle part.
- **Deep CFR / évaluateur neuronal aux feuilles.** Autre architecture. Le
  backlog v3 l'exclut déjà explicitement (annexe B) et rien dans l'audit ne
  justifie de rouvrir ce périmètre maintenant.
- **La suppression des chemins `gpu_cfr_*` dépréciés.** `CLN-01` du backlog v3
  couvre déjà la dépréciation ; la suppression est un ticket ultérieur.

# Articulation avec le backlog v3

Ce plan ne remplace pas [SOLVER_BACKLOG_V3.md](docs/project/SOLVER_BACKLOG_V3.md).
Il se greffe dessus :

| Ce plan | Relation au backlog v3 |
|---|---|
| HOT-01…05 | Approfondit M7 (CPU parallèle) et M2 (storage SoA) ; ne les contredit pas |
| SIMD-01…03 | Nouveau — le backlog v3 ne traite pas le dispatch runtime |
| GPU-01 | **Corrige** un défaut de M9/M11 : la porte de parité n'a pas d'ouvreur de production |
| GPU-02…04 | Étend M9/M11 à HIP et Metal |
| DIST-01…04 | Hors périmètre v3, à ouvrir seulement après L1 |
| CI-01…03 | Nouveau — comble l'absence de couverture ARM exécutée |

HOT-05 relève de l'annexe A du backlog v3 : il change délibérément un résultat
numérique et doit être consigné au CHANGELOG.

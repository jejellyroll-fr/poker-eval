# poker-eval — Backlog Solver v3

**Référence :** [`SOLVER_ARCHITECTURE_V3.md`](SOLVER_ARCHITECTURE_V3.md)
**Base :** `d3f5a909`
**80 tickets · 38 PR · 13 jalons**

Remplace le « Backlog Solver Architecture v2 » (638 micro-tickets, dont la première PR
visait un fichier inexistant).

---

## Contrat agent

- Implémenter **uniquement** le ticket demandé, dans les fichiers listés.
- Ne pas refactoriser du code voisin, même manifestement améliorable. Si un défaut est
  trouvé hors périmètre, l'écrire dans le rapport final ; ne pas le corriger.
- Ne jamais désactiver, `skip`, ou assouplir un test ou un warning pour faire passer un
  ticket. Un test rouge est un blocage à rapporter, pas un obstacle à contourner.
- Si une hypothèse du ticket se révèle fausse, ou si une dépendance manque : **arrêter** et
  rapporter le blocage. Ne pas élargir le périmètre pour compenser.
- Un ticket = un commit logique.
- Rapport final obligatoire : fichiers modifiés, résumé du diff, commandes de test
  exécutées, et leur sortie réelle.

**Invariant permanent :** le chemin `FULL_SCALAR` sur backend `cpu_ref` en F64 est l'oracle
de référence. Aucun autre chemin ne peut être déclaré correct sans parité mesurée contre
lui.

### Commandes de référence

```bash
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

Ciblage :

```bash
ctest --test-dir build/debug -L game_theory --output-on-failure   # qualification math
ctest --test-dir build/debug -L cfr          --output-on-failure   # solveur
ctest --test-dir build/debug -R test_cfr_    --output-on-failure   # unitaires CFR
```

Un nouveau test `tests/game_theory/*.c` s'enregistre avec `add_game_theory_test()` dans
[`tests/CMakeLists.txt`](../../tests/CMakeLists.txt) ; un test hors qualification suit la
convention `tests/test_*.c` déjà en place.

---

## Règles de merge

1. Une PR dont le test de sortie est rouge ne se fusionne pas.
2. Une PR GPU peut se fusionner derrière une capability **désactivée**, mais son backend ne
   devient sélectionnable par `AUTO` qu'après sa gate de parité.
3. Un jalon ne s'ouvre pas tant que la gate du précédent n'est pas verte.
4. **L'ordre M3 → M4 → M5 est non négociable.** Ranges, puis vectoriel, puis mesure.
   Toute optimisation avant M5 est une optimisation à l'aveugle.

## Gates

| Gate | Jalons | Condition |
|---|---|---|
| **A** | M0 | Le registry résout et refuse. Aucun changement de comportement du solveur. |
| **B** | M1–M2 | `ctest -L game_theory` vert. Résultats **inchangés au bit près** hors DCFR. |
| **C** | M3 | Ranges, chance flop et isomorphisme fonctionnels, privacy testée. |
| **D** | M4 | **V5** : lane A == lane scalaire sur tous les jeux de référence. |
| **E** | M5 | **V4** : exploitabilité → 0 sur un équilibre LP exact. |
| **F** | M6–M7 | CFR+/DCFR canoniques ; `cpu_par` bit-identique à `cpu_ref`. |
| **G** | M8 | **V12** : un `.tree` Monker se charge, un `.mkr` se compare. |
| **H** | M9, M11 | Parité GPU mesurée à chaque niveau avant activation `AUTO`. |
| **I** | M10, M12 | Lane B validée ; API/CLI/checkpoint stables. |

## Découpage en PR

| PR | Jalon | Tickets |
|---|---|---|
| PR-01 | M0 | `CTR-01` → `CTR-03` |
| PR-02 | M0 | `CTR-04` → `CTR-06` |
| PR-03 | M1 | `EXT-01` → `EXT-03` |
| PR-04 | M1 | `EXT-04` → `EXT-06` |
| PR-05 | M1 | `EXT-07` → `EXT-08` |
| PR-06 | M2 | `STO-01` → `STO-03` |
| PR-07 | M2 | `STO-04` → `STO-05` |
| PR-08 | M3 | `RNG-01` → `RNG-03` |
| PR-09 | M3 | `RNG-04` → `RNG-05` |
| PR-10 | M3 | `CHN-01` → `CHN-03` |
| PR-11 | M3 | `ISO-01`, `INF-01` |
| PR-12 | M4 | `VEC-01` → `VEC-03` |
| PR-13 | M4 | `VEC-04` → `VEC-05` |
| PR-14 | M4 | `VEC-06` → `VEC-07` |
| PR-15 | M4 | `VEC-08` → `VEC-09` |
| PR-16 | M4 | `VEC-10` |
| PR-17 | M5 | `BR-01` → `BR-03` |
| PR-18 | M5 | `BR-04` → `BR-05` |
| PR-19 | M6 | `ALG-01` → `ALG-03` |
| PR-20 | M6 | `ALG-04` → `ALG-05` |
| PR-21 | M7 | `PAR-01` → `PAR-02` |
| PR-22 | M7 | `PAR-03` → `PAR-04` |
| PR-23 | M8 | `MKR-01` → `MKR-03` |
| PR-24 | M8 | `MKR-04` → `MKR-05` |
| PR-25 | M8 | `MKR-06` → `MKR-07` |
| PR-26 | M9 | `GPU-01` → `GPU-02` |
| PR-27 | M9 | `GPU-03` → `GPU-04` |
| PR-28 | M9 | `GPU-05` |
| PR-29 | M10 | `ABS-01` → `ABS-02` |
| PR-30 | M10 | `ABS-03` → `ABS-04` |
| PR-31 | M10 | `LNB-01` → `LNB-02` |
| PR-32 | M11 | `GPU-06` → `GPU-07` |
| PR-33 | M11 | `RBP-01` |
| PR-34 | M11 | `OUT-01` |
| PR-35 | M12 | `API-01` → `API-02` |
| PR-36 | M12 | `API-03` |
| PR-37 | M12 | `API-04` |
| PR-38 | M12 | `CLN-01` |

---

# M0 — Contrats et squelette hexagonal

Aucun changement de comportement du solveur existant. Uniquement des en-têtes, une cible
de build, et un registry qui ne pilote encore rien.

## CTR-01 — Arborescence et cible de build `pe_solver`

**Priorité** `P0` · **Taille** `S` · **Dépendances** —

**Fichiers** `include/poker_eval/solver/pe_solver.h` (nouveau),
`src/solver/domain/solver.c` (nouveau), `src/CMakeLists.txt`

Créer l'arborescence `include/poker_eval/solver/` et `src/solver/domain/`, plus une cible
compilée dans la bibliothèque existante. `pe_solver.h` déclare le type opaque
`pe_solver_t` et les prototypes du cycle de vie ; `solver.c` fournit des stubs retournant
`PE_ERR_NOT_IMPLEMENTED`. Aucun lien avec `cfr_core` à ce stade.

**DoD** — La build passe sur les presets `debug` et `release`. `nm` sur la bibliothèque
montre les symboles `pe_solver_create` / `pe_solver_destroy`.

**Vérification**
```bash
cmake --preset debug && cmake --build build/debug
cmake --preset release && cmake --build build/release
```

## CTR-02 — Bits de capability

**Priorité** `P0` · **Taille** `S` · **Dépendances** `CTR-01`

**Fichiers** `include/poker_eval/solver/pe_capabilities.h` (nouveau),
`src/solver/domain/registry.c` (nouveau), `tests/test_pe_capabilities.c` (nouveau),
`tests/CMakeLists.txt`

Définir les 24 bits de capability listés au §5 de l'architecture, plus
`pe_caps_to_string()` et `pe_caps_parse()`.

**DoD** — Chaque bit a un nom unique, aucun chevauchement de valeur, et le round-trip
`caps → string → caps` est l'identité sur 1 000 combinaisons aléatoires.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_capabilities --output-on-failure
```

## CTR-03 — Axes de configuration orthogonaux

**Priorité** `P0` · **Taille** `M` · **Dépendances** `CTR-02`

**Fichiers** `include/poker_eval/solver/pe_solver_config.h` (nouveau),
`src/solver/domain/config.c` (nouveau), `tests/test_pe_solver_config.c` (nouveau),
`tests/CMakeLists.txt`

Déclarer `pe_traversal_mode_t`, `pe_regret_mode_t`, `pe_policy_mode_t`,
`pe_averaging_mode_t`, `pe_pruning_mode_t`, `pe_precision_mode_t`, `pe_compute_kind_t`,
`pe_stage_backends_t`, et les structures `pe_algorithm_config_t` /
`pe_execution_config_t` / `pe_solver_config_t` du §5. Ajouter
`pe_solver_config_default()`.

**DoD** — La config par défaut vaut `FULL_SCALAR` + `VANILLA` + `REGRET_MATCHING` +
`UNIFORM` + `PRUNE_NONE` + `F64` + `CPU_REF`, vérifié par assertion champ par champ.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_solver_config --output-on-failure
```

## CTR-04 — Ports et injection de dépendances

**Priorité** `P0` · **Taille** `M` · **Dépendances** `CTR-03`

**Fichiers** `include/poker_eval/solver/pe_ports.h` (nouveau),
`include/poker_eval/solver/pe_telemetry.h` (nouveau),
`src/solver/adapters/telemetry_null.c` (nouveau),
`src/solver/adapters/telemetry_callback.c` (nouveau), `src/CMakeLists.txt`

Déclarer `pe_solver_deps_t` portant les ports secondaires (`compute`, `evaluator`,
`storage`, `persist`, `telemetry`). Implémenter deux adaptateurs de télémétrie : un
`null` qui ignore tout, un `callback` qui route vers une fonction utilisateur.

**DoD** — `pe_solver_create()` avec `deps.telemetry = NULL` installe automatiquement
l'adaptateur `null` et ne déréférence jamais un pointeur nul. Un test émet 100 événements
sur l'adaptateur `callback` et les compte.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_solver_config --output-on-failure
```

## CTR-05 — RNG sans état global

**Priorité** `P0` · **Taille** `M` · **Dépendances** `CTR-01`

**Fichiers** `include/poker_eval/solver/pe_rng.h` (nouveau),
`src/solver/domain/rng.c` (nouveau), `tests/test_pe_rng.c` (nouveau),
`tests/CMakeLists.txt`

PCG32 à état explicite. `pe_rng_seed(rng, seed)`, `pe_rng_stream(parent, thread_id,
iteration, player, sample)` qui dérive un flux reproductible sans partage d'état, et
`pe_rng_uniform01()` / `pe_rng_below(n)`.

**DoD** — Aucune variable `static` mutable dans `rng.c` (vérifié par relecture et par
`nm --defined-only` : pas de symbole en section `.bss`/`.data` du module). Deux flux
dérivés avec des `thread_id` différents produisent des suites distinctes ; le même triplet
produit la même suite sur deux exécutions.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_rng --output-on-failure
```

## CTR-06 — Registry, résolution de preset et plan d'exécution

**Priorité** `P0` · **Taille** `L` · **Dépendances** `CTR-03`, `CTR-04`

**Fichiers** `include/poker_eval/solver/pe_solver_plan.h` (nouveau),
`src/solver/domain/registry.c`, `src/solver/domain/plan.c` (nouveau),
`tests/test_pe_registry.c` (nouveau), `tests/CMakeLists.txt`

Implémenter la table des presets du §5 (`cfr`, `cfr-vector`, `cfr+`, `dcfr`,
`cfr+-chance`, `external-mccfr`, `external-dcfr`, `outcome-mccfr`, `ecfr`), leur
résolution vers des composants explicites, la validation par capabilities, et la
production d'un `pe_execution_plan_t` immuable portant les ops résolues, les backends
effectifs par étage, la précision, les capabilities et les avertissements.

Les résultats de validation sont `PE_VALID_OK`, `PE_VALID_WARNING`, `PE_VALID_FALLBACK`,
`PE_VALID_ERROR`.

**DoD** — Les 9 presets se résolvent sans erreur. Une combinaison non déclarée dans la
matrice (par exemple `OUTCOME` + `PRUNE_RBP`) rend `PE_VALID_ERROR` avec un message
nommant les deux composants en conflit, et **ne produit aucun plan**. Un preset demandant
une capability absente du jeu rend `PE_VALID_ERROR`, jamais un fallback silencieux.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_registry --output-on-failure
```

---

# M1 — Extraction du CFR existant

Découpage de `cfr_traverse_recursive()` sans changer les résultats. Une seule exception,
isolée dans `EXT-07` : la correction du discount DCFR.

## EXT-01 — Supprimer le mode de stratégie global

**Priorité** `P0` · **Taille** `M` · **Dépendances** `CTR-01`

**Fichiers** `src/engine/solvers/cfr/cfr_storage.c`,
`include/poker_eval/engine/solvers/cfr/cfr_core.h`,
`src/engine/solvers/cfr/cfr_core.c`

`cfr_storage_set_strategy_mode(int use_ecfr, double ecfr_lambda)` est un état statique
global : il rend le solveur non réentrant et interdit deux solves simultanés dans un
processus. Le déplacer dans `cfr_storage_t` (ou un contexte explicite passé aux
accesseurs de stratégie). Conserver l'ancienne fonction comme wrap déprécié agissant sur
une instance par défaut, pour ne pas casser les appelants existants.

**DoD** — Aucun `static` mutable de mode de stratégie ne subsiste. Deux `cfr_storage_t`
configurés avec des `ecfr_lambda` différents dans le même processus rendent des stratégies
différentes pour les mêmes regrets.

**Vérification**
```bash
ctest --test-dir build/debug -L cfr --output-on-failure
```

## EXT-02 — Sortir l'état thread-local du traversal

**Priorité** `P0` · **Taille** `M` · **Dépendances** `EXT-01`

**Fichiers** `src/engine/solvers/cfr/cfr_core.c`

Les 7 variables `CFR_THREAD_LOCAL` (`g_cfr_current_iter`, `g_cfr_recursion_depth`,
`g_cfr_max_depth`, `g_cfr_depth_exceeded`, `g_cfr_node_count`, `g_cfr_use_flow_focus`,
`g_cfr_flow_pow`) deviennent les champs d'un `cfr_walk_ctx_t` alloué par `cfr_solve()` et
passé en paramètre à toute la récursion.

**DoD** — Aucun `CFR_THREAD_LOCAL` ne subsiste dans `cfr_core.c`. Toute la suite
`ctest -L game_theory` produit des valeurs **identiques au bit près** à celles d'avant le
ticket (comparer les sorties texte des tests avant/après).

**Vérification**
```bash
ctest --test-dir build/debug -L game_theory --output-on-failure
grep -c CFR_THREAD_LOCAL src/engine/solvers/cfr/cfr_core.c   # attendu : 0
```

## EXT-03 — Router les sorties du domaine vers la télémétrie

**Priorité** `P0` · **Taille** `M` · **Dépendances** `EXT-02`, `CTR-04`

**Fichiers** `src/engine/solvers/cfr/cfr_core.c`,
`include/poker_eval/solver/pe_telemetry.h`

Remplacer chaque `fprintf(stderr, ...)` du chemin de solve (traces d'itération, dépassement
de profondeur, progression) par un appel `pe_telemetry_emit()`. L'adaptateur par défaut
écrit sur `stderr` avec exactement le même format, pour que rien ne change à l'observation.

**DoD** — Zéro `fprintf` et zéro `printf` dans `cfr_core.c`. Avec l'adaptateur `stderr`,
la sortie d'un solve avec `trace_iterations = 1` est identique à celle d'avant le ticket.

**Vérification**
```bash
grep -cE 'fprintf|printf' src/engine/solvers/cfr/cfr_core.c   # attendu : 0
ctest --test-dir build/debug -L cfr --output-on-failure
```

## EXT-04 — Extraire la traversée scalaire

**Priorité** `P0` · **Taille** `L` · **Dépendances** `EXT-03`

**Fichiers** `src/solver/domain/traversal_full_scalar.c` (nouveau),
`include/poker_eval/solver/pe_traversal.h` (nouveau),
`src/engine/solvers/cfr/cfr_core.c`

Déplacer le corps de `cfr_traverse_recursive()` dans un module implémentant
`pe_traversal_ops_t`. Le traversal ne fait plus que : descendre l'arbre, calculer les
valeurs, et **produire des deltas de regret bruts dans un `pe_update_batch_t`**. Il
n'applique aucun discount, aucune pondération d'averaging, aucune logique de lock.

**DoD** — `ctest -L game_theory` inchangé au bit près. `traversal_full_scalar.c` ne
contient aucune occurrence de `enable_dcfr`, `enable_linear_avg`, `enable_ecfr`,
`dcfr_alpha`, `dcfr_beta`, `dcfr_gamma`.

**Vérification**
```bash
ctest --test-dir build/debug -L game_theory --output-on-failure
grep -cE 'enable_dcfr|enable_linear_avg|dcfr_(alpha|beta|gamma)' \
     src/solver/domain/traversal_full_scalar.c   # attendu : 0
```

## EXT-05 — Extraire les ops de regret vanilla

**Priorité** `P0` · **Taille** `M` · **Dépendances** `EXT-04`

**Fichiers** `src/solver/domain/regret_vanilla.c` (nouveau),
`include/poker_eval/solver/pe_regret.h` (nouveau)

Implémenter `pe_regret_ops_t` pour le CFR vanilla : `compute_strategy` (regret matching
avec repli uniforme), `apply_delta_batch` (accumulation simple), `begin_iteration` /
`end_iteration` sans effet.

**DoD** — `ctest -L game_theory` inchangé au bit près.

**Vérification**
```bash
ctest --test-dir build/debug -L game_theory --output-on-failure
```

## EXT-06 — Extraire les ops d'averaging

**Priorité** `P0` · **Taille** `M` · **Dépendances** `EXT-05`

**Fichiers** `src/solver/domain/average.c` (nouveau),
`include/poker_eval/solver/pe_average.h` (nouveau)

Implémenter `pe_average_ops_t` pour `UNIFORM` et `LINEAR`. Le poids d'averaging est
calculé par l'ops, pas dans le traversal.

**DoD** — Un solve avec `enable_linear_avg = 1` rend exactement les mêmes moyennes
qu'avant le ticket, sur `test_leduc_openspiel` et `test_akq_game`.

**Vérification**
```bash
ctest --test-dir build/debug -L game_theory --output-on-failure
```

## EXT-07 — Corriger le discount DCFR : une fois par itération

**Priorité** `P0` · **Taille** `M` · **Dépendances** `EXT-06`

**Fichiers** `src/solver/domain/regret_dcfr.c` (nouveau),
`src/solver/domain/traversal_full_scalar.c`,
`tests/test_cfr_dcfr_discount.c` (nouveau), `tests/CMakeLists.txt`, `CHANGELOG.md`

**Ce ticket change délibérément un comportement.** Aujourd'hui le discount est appliqué à
chaque *visite* d'infoset (`cfr_storage_update_regret_at_street(..., discount)` est appelé
une fois par état, et un infoset poker correspond à de nombreux états) : le regret cumulé
est donc multiplié par `d^N` au lieu de `d`. Déplacer l'application du discount dans
`end_iteration`, où il s'applique une seule fois par itération sur l'ensemble du storage.

**DoD** — Un test construit un jeu jouet où un infoset est visité exactement 4 fois par
itération, avec `dcfr_alpha` connu, et vérifie que le regret cumulé après une itération
vaut `R * d + Σ deltas` et **non** `R * d^4 + ...`. Le changement est consigné dans
`CHANGELOG.md` comme correction de comportement.

**Vérification**
```bash
ctest --test-dir build/debug -R test_cfr_dcfr_discount --output-on-failure
ctest --test-dir build/debug -L game_theory --output-on-failure
```

## EXT-08 — Sortir les locks et le relock du traversal

**Priorité** `P1` · **Taille** `L` · **Dépendances** `EXT-07`

**Fichiers** `src/solver/domain/locks.c` (nouveau),
`src/solver/domain/traversal_full_scalar.c`,
`include/poker_eval/solver/pe_locks.h` (nouveau)

Le bloc de relock périodique (FEAT-11) occupe une part importante de la traversée et
mélange descente, mesure de perte d'EV et best-response. L'extraire derrière une interface
appelée par le traversal en trois points : `lock_strategy_for(infoset)`,
`should_relock(iter)`, `record_relock_loss(...)`.

**DoD** — `test_cfr_node_lock`, `test_cfr_periodic_relock` et
`test_cfr_periodic_relock_multiway` passent avec des valeurs identiques à celles d'avant
le ticket.

**Vérification**
```bash
ctest --test-dir build/debug -R 'test_cfr_(node_lock|periodic_relock)' --output-on-failure
```

---

# M2 — Storage dense-ID

## STO-01 — Métadonnées d'infoset et table key → id

**Priorité** `P0` · **Taille** `M` · **Dépendances** `EXT-08`

**Fichiers** `include/poker_eval/solver/pe_storage.h` (nouveau),
`src/solver/domain/storage_v2.c` (nouveau), `tests/test_pe_storage_ids.c` (nouveau),
`tests/CMakeLists.txt`

Implémenter `pe_infoset_meta_t` (§6) et une table de hachage `uint64_t key -> uint32_t id`
avec attribution d'IDs denses et croissants, et croissance amortie.

**DoD** — Sur 1 000 000 de clés aléatoires : chaque clé rend le même id à chaque
consultation, les ids couvrent exactement `[0, n)` sans trou, et aucune collision ne
produit un id partagé entre deux clés distinctes.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_storage_ids --output-on-failure
```

## STO-02 — Tableaux ragged SoA

**Priorité** `P0` · **Taille** `M` · **Dépendances** `STO-01`

**Fichiers** `src/solver/domain/storage_v2.c`,
`include/poker_eval/solver/pe_storage.h`, `tests/test_pe_storage_ids.c`

Ajouter les tableaux `regret[]`, `avg_strategy[]`, `current_strategy[]`,
`locked_strategy[]` indexés par `action_offset` / `combo_offset`. La disposition doit
couvrir la lane B (`combo_count == 1`) **et** la lane A (`combo_count == N`) sans
changement de structure — c'est le point qui évite deux storages.

**DoD** — L'indexation `[infoset][action][combo]` est correcte pour `combo_count = 1` et
pour `combo_count = 1326`, vérifié par écriture puis relecture exhaustive sur un jeu de
1 000 infosets à comptes d'actions et de combos variables.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_storage_ids --output-on-failure
```

## STO-03 — Port de storage et adaptateur RAM

**Priorité** `P0` · **Taille** `M` · **Dépendances** `STO-02`

**Fichiers** `include/poker_eval/solver/pe_storage.h`,
`src/solver/adapters/storage_ram.c` (nouveau), `src/CMakeLists.txt`

Extraire `pe_storage_ops_t` (allouer, résoudre une clé, lire/écrire un span, itérer,
compter, mesurer l'occupation) et fournir l'implémentation RAM.

**DoD** — Le domaine n'appelle plus aucune fonction `cfr_storage_*` directement : toutes
les écritures et lectures passent par le port.

**Vérification**
```bash
grep -c 'cfr_storage_' src/solver/domain/*.c   # attendu : 0
ctest --test-dir build/debug -L cfr --output-on-failure
```

## STO-04 — Adaptateur de compatibilité sur `cfr_storage_t`

**Priorité** `P0` · **Taille** `M` · **Dépendances** `STO-03`

**Fichiers** `src/solver/adapters/storage_legacy.c` (nouveau)

Implémenter `pe_storage_ops_t` par-dessus le `cfr_storage_t` historique, pour que les
appelants existants (`cfr_resolve.c`, `mpf_export.c`, benchmarks, outils) continuent de
fonctionner pendant toute la migration.

**DoD** — `ctest -L cfr` et `ctest -L game_theory` verts avec l'adaptateur legacy
sélectionné, et verts avec l'adaptateur RAM sélectionné. Les deux produisent les mêmes
stratégies moyennes à `1e-12` près.

**Vérification**
```bash
ctest --test-dir build/debug -L cfr --output-on-failure
ctest --test-dir build/debug -L game_theory --output-on-failure
```

## STO-05 — Estimation mémoire et refus de dépassement

**Priorité** `P0` · **Taille** `M` · **Dépendances** `STO-04`, `CTR-06`

**Fichiers** `src/solver/domain/solver.c`, `src/solver/domain/plan.c`,
`include/poker_eval/solver/pe_solver.h`, `tests/test_pe_estimate.c` (nouveau),
`tests/CMakeLists.txt`

Implémenter `pe_solver_estimate()` : à partir du plan résolu, du nombre d'infosets
attendus, du nombre de combos et de la précision, produire une estimation RAM et VRAM.
Ajouter `max_ram_bytes` à `pe_execution_config_t` et faire échouer `pe_solver_validate()`
avec `PE_VALID_ERROR` quand l'estimation dépasse le budget.

**DoD** — L'estimation est à moins de 15 % de l'occupation réelle mesurée sur un solve
Leduc et sur un solve Hold'em river. Un budget délibérément trop petit fait échouer
`validate()` **avant** toute allocation.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_estimate --output-on-failure
```

---

# M3 — Ranges, chance et isomorphisme

Le jalon qui débloque tout le reste. Sans lui, le solveur résout un matchup de mains
connues, pas un jeu à information imparfaite.

## RNG-01 — Type de range et raccord au parseur existant

**Priorité** `P0` · **Taille** `M` · **Dépendances** `STO-05`

**Fichiers** `include/poker_eval/solver/pe_range.h` (nouveau),
`src/solver/domain/range.c` (nouveau), `tests/test_pe_range.c` (nouveau),
`tests/CMakeLists.txt`

Définir `pe_hand_combo_t` / `pe_player_range_t` (§7.1) et une fonction de construction à
partir du parseur existant (`src/range/AdvancedRangeParser.c`). Normalisation des poids,
déduplication des combos, tri stable.

**DoD** — `"AA"` en Hold'em rend 6 combos de poids égal ; `"AKs"` en rend 4 ; une range
vide est refusée avec un code d'erreur explicite. Les poids somment à 1 après
normalisation, à `1e-12` près.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_range --output-on-failure
```

## RNG-02 — Le modèle de jeu accepte une range par joueur

**Priorité** `P0` · **Taille** `L` · **Dépendances** `RNG-01`

**Fichiers** `include/poker_eval/engine/solvers/cfr/multiway_postflop_adapter.h`,
`src/engine/solvers/cfr/multiway_postflop_adapter.c`,
`tests/test_mpf_ranges.c` (nouveau), `tests/CMakeLists.txt`

Ajouter `pe_player_range_t range[MPF_MAX_PLAYERS]` à `mpf_config_t` à côté du
`hole[]`/`hole_specified[]` existant. Une main fixe reste valide : elle est traduite en
range d'un seul combo de poids 1. La configuration est rejetée si un joueur n'a ni main
fixe ni range.

**DoD** — Un solve configuré avec `hole[p]` fixe produit exactement les mêmes valeurs
qu'avant le ticket. Un solve configuré avec une range d'un seul combo produit les mêmes
valeurs que le même solve en main fixe.

**Vérification**
```bash
ctest --test-dir build/debug -R 'test_mpf_ranges|test_cfr_multiway' --output-on-failure
```

## RNG-03 — Root private chance

**Priorité** `P0` · **Taille** `L` · **Dépendances** `RNG-02`

**Fichiers** `src/engine/solvers/cfr/multiway_postflop_adapter.c`,
`tests/test_mpf_ranges.c`

Ajouter un nœud de chance racine de type `PRIVATE_HANDS` distribuant les mains privées
selon les ranges, avec les poids conjoints corrects (retrait des cartes entre joueurs : une
combinaison où deux joueurs partagent une carte a un poids nul).

**DoD** — Sur un spot HU où les deux joueurs ont la range `"AA"`, le nombre de
combinaisons de départ de poids non nul vaut exactement 6×6 − (combinaisons partageant un
as) = 6, vérifié par énumération dans le test.

**Vérification**
```bash
ctest --test-dir build/debug -R test_mpf_ranges --output-on-failure
```

## RNG-04 — Vecteurs de reach par combo

**Priorité** `P0` · **Taille** `M` · **Dépendances** `RNG-03`

**Fichiers** `include/poker_eval/solver/pe_vector.h` (nouveau),
`src/solver/domain/reach.c` (nouveau), `tests/test_pe_vector.c` (nouveau),
`tests/CMakeLists.txt`

Définir `pe_reach_vec_t` et `pe_value_vec_t` : tableaux denses indexés par combo, avec
opérations `scale`, `multiply_by_strategy`, `sum`, `dot`.

**DoD** — Les opérations sont exactes à `1e-14` sur des vecteurs de 1 326 et de 270 725
éléments. Le produit d'un vecteur de reach uniforme par une stratégie uniforme à 3 actions
rend `1/3` sur chaque composante.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_vector --output-on-failure
```

## RNG-05 — Retrait des cartes en conflit à l'évaluation terminale

**Priorité** `P0` · **Taille** `L` · **Dépendances** `RNG-04`

**Fichiers** `src/solver/domain/blockers.c` (nouveau),
`include/poker_eval/solver/pe_vector.h`, `tests/test_pe_blockers.c` (nouveau),
`tests/CMakeLists.txt`

Implémenter le retrait des combinaisons impossibles au moment de l'évaluation terminale :
une main du héros et une main du vilain partageant une carte, ou entrant en conflit avec le
board, ont une contribution nulle. La méthode retenue est la somme totale des reach adverses
moins les contributions par carte (52 accumulateurs), qui donne un `O(n)` par nœud au lieu
d'un `O(n²)`.

**DoD** — Sur un spot HU avec deux ranges connues et un board donné, la valeur obtenue par
la méthode par accumulateurs est identique, à `1e-12` près, à celle obtenue par double
boucle exhaustive sur toutes les paires de combos. Le test compare les deux sur au moins
3 boards.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_blockers --output-on-failure
```

## CHN-01 — Types de chance génériques

**Priorité** `P0` · **Taille** `M` · **Dépendances** `RNG-03`

**Fichiers** `include/poker_eval/solver/pe_game_rules.h` (nouveau),
`src/engine/solvers/cfr/multiway_postflop_adapter.c`,
`tests/test_cfr_chance_nodes.c`

Remplacer le drapeau binaire `chance_pending` par `chance_kind()` rendant `PE_CHANCE_NONE`,
`PE_CHANCE_PRIVATE_HANDS`, `PE_CHANCE_FLOP_THREE`, `PE_CHANCE_BOARD_ONE` ou
`PE_CHANCE_DRAW_N`.

**DoD** — `test_cfr_chance_nodes` passe sans changement de valeur. Un état turn rend
`BOARD_ONE`, un état préflop→flop rend `FLOP_THREE`, la racine avec ranges rend
`PRIVATE_HANDS`.

**Vérification**
```bash
ctest --test-dir build/debug -R test_cfr_chance_nodes --output-on-failure
```

## CHN-02 — Chance flop combinatoire

**Priorité** `P0` · **Taille** `L` · **Dépendances** `CHN-01`

**Fichiers** `src/engine/solvers/cfr/multiway_postflop_adapter.c`,
`tests/test_mpf_flop_chance.c` (nouveau), `tests/CMakeLists.txt`

`mpf_advance_street()` révèle aujourd'hui un board figé au flop
(`mpf_update_board(st, 3)`). Le remplacer par un vrai nœud de chance dont un outcome est
une **combinaison** de 3 cartes parmi les cartes restantes, jamais une permutation
séquentielle. Le cache `chance_children[52]` reste réservé à `BOARD_ONE`.

**DoD** — Depuis un état préflop sans cartes connues autres que les mains privées, le
nombre d'outcomes du nœud flop vaut `C(48,3) = 17 296` en Hold'em HU, et chaque outcome
apparaît exactement une fois (vérifié par ensemble de masques). Aucune permutation
dupliquée.

**Vérification**
```bash
ctest --test-dir build/debug -R test_mpf_flop_chance --output-on-failure
```

## CHN-03 — Échantillonnage direct de chance

**Priorité** `P1` · **Taille** `M` · **Dépendances** `CHN-02`, `CTR-05`

**Fichiers** `include/poker_eval/solver/pe_game_rules.h`,
`src/engine/solvers/cfr/multiway_postflop_adapter.c`,
`tests/test_mpf_flop_chance.c`

Ajouter `chance_sample(state, rng, out)` rendant un outcome et son `importance_ratio`,
pour les espaces de chance trop grands à énumérer. Le callback est optionnel : quand il
est absent, le solveur énumère.

**DoD** — Sur 1 000 000 de tirages d'un flop, la distribution empirique des classes
d'isomorphisme est à moins de 1 % de la distribution théorique (test du χ² ou écart
relatif max).

**Vérification**
```bash
ctest --test-dir build/debug -R test_mpf_flop_chance --output-on-failure
```

## ISO-01 — Isomorphisme de couleurs sur le flop

**Priorité** `P1` · **Taille** `L` · **Dépendances** `CHN-02`

**Fichiers** `src/solver/domain/isomorphism.c` (nouveau),
`src/engine/solvers/cfr/board_canonical.c`,
`tests/test_pe_isomorphism.c` (nouveau), `tests/CMakeLists.txt`

Utiliser `pe_board_canonical_key()` pour regrouper les flops en classes d'isomorphisme et
pondérer chaque classe par son cardinal. Exposer la capability `SUIT_ISOMORPHISM`.

**DoD** — Le nombre de classes d'isomorphisme de flops vaut exactement **1 755**, et la
somme des cardinaux vaut **22 100** = `C(52,3)`. Un solve avec isomorphisme actif rend la
même valeur racine, à `1e-9` près, qu'un solve énumérant les 22 100 flops.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_isomorphism --output-on-failure
```

## INF-01 — Test de privacy des infosets

**Priorité** `P0` · **Taille** `S` · **Dépendances** `RNG-03`

**Fichiers** `tests/test_mpf_infoset_privacy.c` (nouveau), `tests/CMakeLists.txt`

`mpf_pattern_hash()` ne hache aujourd'hui que `board_mask | hole[joueur_actif]` : la
privacy est correcte. Ce ticket la **verrouille par un test de mutation** plutôt que par
relecture.

**DoD** — Pour un état donné, modifier la main d'un joueur **non actif** (toutes les
substitutions possibles ne créant pas de conflit de carte) laisse `mpf_state_infoset_key()`
strictement inchangée. Modifier la main du joueur actif la change. Le test couvre Hold'em
et PLO4, en 2 et 3 joueurs.

**Vérification**
```bash
ctest --test-dir build/debug -R test_mpf_infoset_privacy --output-on-failure
```

---

# M4 — Lane A : CFR vectoriel

Le jalon décisif. C'est lui qui transforme le dépôt en solveur, et lui qui rend le GPU
rentable ensuite.

## VEC-01 — Storage indexé par combo

**Priorité** `P0` · **Taille** `M` · **Dépendances** `INF-01`, `STO-02`

**Fichiers** `src/solver/domain/storage_v2.c`,
`include/poker_eval/solver/pe_storage.h`, `tests/test_pe_storage_ids.c`

Renseigner `combo_offset` / `combo_count` pour les infosets de la lane A et allouer les
spans en conséquence. Un infoset porte alors `action_count × combo_count` valeurs de regret.

**DoD** — Un infoset à 3 actions et 1 326 combos occupe exactement 3 978 emplacements de
regret ; l'accès `(a, c)` rend la valeur écrite en `(a, c)` pour toutes les paires.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_storage_ids --output-on-failure
```

## VEC-02 — Squelette de la traversée vectorielle

**Priorité** `P0` · **Taille** `L` · **Dépendances** `VEC-01`, `RNG-05`

**Fichiers** `src/solver/domain/traversal_full_vector.c` (nouveau),
`include/poker_eval/solver/pe_traversal.h`

Implémenter `pe_traversal_ops_t` en forme vectorielle : la descente porte un
`pe_reach_vec_t` par joueur, remonte un `pe_value_vec_t` par joueur, et produit des deltas
de regret par (action, combo). Les nœuds terminaux sont délégués aux tickets `VEC-05` /
`VEC-06`.

**DoD** — La traversée compile et parcourt un arbre jouet à un seul nœud de décision sans
lire de mémoire non initialisée (build ASan propre).

**Vérification**
```bash
cmake --preset debug -DENABLE_ASAN=ON -B build/asan && cmake --build build/asan
ctest --test-dir build/asan -R test_pe_vector --output-on-failure
```

## VEC-03 — Regret matching vectoriel

**Priorité** `P0` · **Taille** `M` · **Dépendances** `VEC-02`

**Fichiers** `src/solver/domain/regret_vanilla.c`,
`tests/test_pe_vector_cfr.c` (nouveau), `tests/CMakeLists.txt`

Étendre `compute_strategy` pour opérer sur un span `[action][combo]` : chaque combo a sa
propre distribution sur les actions, normalisée indépendamment, avec repli uniforme quand
la somme des regrets positifs est nulle.

**DoD** — Pour chaque combo, la stratégie somme à 1 à `1e-12` près. Un combo dont tous les
regrets sont négatifs rend la distribution uniforme.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_vector_cfr --output-on-failure
```

## VEC-04 — Averaging vectoriel

**Priorité** `P0` · **Taille** `M` · **Dépendances** `VEC-03`

**Fichiers** `src/solver/domain/average.c`, `tests/test_pe_vector_cfr.c`

Étendre `pe_average_ops_t` au span `[action][combo]`, pondéré par le reach du joueur agissant
**pour ce combo**.

**DoD** — Sur un jeu jouet à solution connue, la stratégie moyenne par combo converge vers
la solution analytique à `2e-3` près en 10 000 itérations.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_vector_cfr --output-on-failure
```

## VEC-05 — Nœud de fold vectoriel

**Priorité** `P0` · **Taille** `M` · **Dépendances** `VEC-04`, `RNG-05`

**Fichiers** `src/solver/domain/traversal_full_vector.c`,
`src/solver/domain/blockers.c`, `tests/test_pe_blockers.c`

Un fold rend au joueur restant le pot, pondéré par la masse de reach adverse compatible
avec chaque combo du héros. Utiliser les accumulateurs par carte de `RNG-05` : coût `O(n)`
par nœud, pas `O(n²)`.

**DoD** — La valeur par combo est identique, à `1e-12`, à celle d'une double boucle
exhaustive sur les paires de combos, sur au moins 3 configurations de ranges.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_blockers --output-on-failure
```

## VEC-06 — Showdown vectoriel trié

**Priorité** `P0` · **Taille** `L` · **Dépendances** `VEC-05`

**Fichiers** `src/solver/domain/showdown_vector.c` (nouveau),
`tests/test_pe_showdown.c` (nouveau), `tests/CMakeLists.txt`

L'opération centrale de la lane A. Trier les combos des deux joueurs par force de main
évaluée sur le board, puis parcourir en maintenant des sommes courantes de reach adverse
(gagnées / perdues / partagées), avec correction par carte pour les blockers. Coût
`O(n log n)` par nœud au lieu de `O(n²)`.

Les égalités de force doivent être traitées comme un vrai partage, pas comme un
départage arbitraire.

**DoD** — Sur au moins 5 boards (dont un pairé, un monotone et un board à quinte
possible) et 3 paires de ranges, la valeur par combo est identique à `1e-12` près à celle
d'une double boucle exhaustive. Le test mesure aussi que le temps croît en `n log n` et
non en `n²` entre 200 et 1 326 combos.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_showdown --output-on-failure
```

## VEC-07 — Multiway vectoriel

**Priorité** `P1` · **Taille** `L` · **Dépendances** `VEC-06`

**Fichiers** `src/solver/domain/showdown_vector.c`,
`src/solver/domain/traversal_full_vector.c`, `tests/test_pe_showdown.c`

Étendre le showdown et le fold à 3 joueurs : side pots, partages à plus de deux, et retrait
des cartes sur trois mains simultanément.

**DoD** — Sur un spot à 3 joueurs avec ranges connues, la valeur par combo est identique à
`1e-10` près à une triple boucle exhaustive. La somme des valeurs sur les trois joueurs
vaut le pot, à `1e-10` près (conservation).

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_showdown --output-on-failure
```

## VEC-08 — Chance vectorielle

**Priorité** `P1` · **Taille** `M` · **Dépendances** `VEC-07`, `ISO-01`

**Fichiers** `src/solver/domain/traversal_chance_vector.c` (nouveau),
`include/poker_eval/solver/pe_traversal.h`

Traversée vectorielle avec chance échantillonnée : un outcome de board tiré par itération,
décisions exhaustives. Les vecteurs de reach sont mis à jour par le retrait des cartes du
board révélé.

**DoD** — Sur un solve turn (une carte de river), la moyenne des valeurs racines sur
10 000 itérations échantillonnées est à moins de 0,5 % de la valeur du solve exhaustif sur
les 44 rivers.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_vector_cfr --output-on-failure
```

## VEC-09 — Gate V5 : lane A == lane scalaire

**Priorité** `P0` · **Taille** `M` · **Dépendances** `VEC-08`

**Fichiers** `tests/game_theory/test_vector_vs_scalar.c` (nouveau),
`tests/CMakeLists.txt`

**La gate D.** Résoudre le même jeu par `FULL_SCALAR` et par `FULL_VECTOR`, avec le même
seed et le même nombre d'itérations, et comparer.

**DoD** — Sur AKQ, Kuhn 2p, Kuhn 3p et Leduc : la valeur de la politique diffère de moins
de `1e-9`, et la stratégie moyenne de chaque infoset diffère de moins de `1e-7` par action.
Le test échoue si l'un des jeux dépasse la tolérance.

**Vérification**
```bash
ctest --test-dir build/debug -R test_vector_vs_scalar --output-on-failure
```

## VEC-10 — Preset `cfr-vector` et validation Hold'em river

**Priorité** `P0` · **Taille** `M` · **Dépendances** `VEC-09`

**Fichiers** `src/solver/domain/registry.c`,
`tests/test_pe_holdem_river_ranges.c` (nouveau), `tests/CMakeLists.txt`

Déclarer le preset `cfr-vector` (FULL_VECTOR + VANILLA + RM + UNIFORM) et valider un solve
Hold'em river range vs range contre une énumération indépendante.

**DoD** — Sur un spot river HU avec deux ranges de plus de 100 combos, la valeur racine du
solveur est identique à `1e-6` près à celle d'un calcul d'équité/EV indépendant sur la
stratégie moyenne exportée.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_holdem_river_ranges --output-on-failure
```

---

# M5 — Best-response à information imparfaite

## BR-01 — Renommer le best-response existant

**Priorité** `P0` · **Taille** `S` · **Dépendances** `VEC-10`

**Fichiers** `include/poker_eval/engine/solvers/cfr/cfr_core.h`,
`src/engine/solvers/cfr/cfr_core.c`, `tests/game_theory/README.md`

`cfr_best_response_value()` maximise par état, pas par infoset : c'est un best-response à
**information parfaite**, donc une borne supérieure qui reste positive à l'équilibre. Le
renommer `cfr_best_response_perfect_info()` / `cfr_exploitability_perfect_info()` et
conserver les anciens noms comme alias dépréciés documentés.

**DoD** — `test_best_response_exploitability` passe sans changement de valeur. La
documentation du header dit explicitement « borne supérieure » et pourquoi.

**Vérification**
```bash
ctest --test-dir build/debug -R test_best_response_exploitability --output-on-failure
```

## BR-02 — Best-response vectoriel par infoset

**Priorité** `P0` · **Taille** `L` · **Dépendances** `BR-01`

**Fichiers** `src/solver/domain/best_response_ii.c` (nouveau),
`include/poker_eval/solver/pe_solver.h`,
`tests/game_theory/test_best_response_ii.c` (nouveau), `tests/CMakeLists.txt`

Implémenter le best-response correct : pour chaque infoset du joueur BR, la valeur de
chaque action est la somme sur les combos pondérée par le reach contrefactuel adverse, et
le maximum est pris **par infoset**, une seule fois pour tous les états qui le composent.

**DoD** — Sur l'équilibre exact fourni par le LP séquentiel de
`tests/game_theory/analytical_oracles.c`, l'exploitabilité rendue est inférieure à `1e-7`.
Sur une politique délibérément exploitable (Always Fold en Kuhn), elle vaut la valeur
analytique connue.

**Vérification**
```bash
ctest --test-dir build/debug -R test_best_response_ii --output-on-failure
```

## BR-03 — Exploitabilité en mbb/g

**Priorité** `P0` · **Taille** `M` · **Dépendances** `BR-02`

**Fichiers** `src/solver/domain/best_response_ii.c`,
`include/poker_eval/solver/pe_solver.h`, `tests/game_theory/test_best_response_ii.c`

Convertir l'exploitabilité en milli-big-blinds par main, à partir de la big blind déclarée
dans la configuration du jeu. Exposer la valeur brute **et** la valeur en mbb/g dans
`pe_metrics_t`.

**DoD** — Sur un jeu à big blind connue, la conversion est exacte : une exploitabilité de
0,001 BB rend 1,0 mbb/g. La valeur brute reste accessible sans conversion.

**Vérification**
```bash
ctest --test-dir build/debug -R test_best_response_ii --output-on-failure
```

## BR-04 — Critère d'arrêt sur exploitabilité

**Priorité** `P0` · **Taille** `M` · **Dépendances** `BR-03`

**Fichiers** `src/solver/domain/solver.c`,
`include/poker_eval/solver/pe_solver_config.h`,
`tests/game_theory/test_best_response_ii.c`

Ajouter `target_exploitability_mbb` à `pe_solver_config_t`. Le solveur évalue
l'exploitabilité tous les `exploitability_interval` et s'arrête quand la cible est
atteinte, en rapportant l'itération et la valeur atteinte.

**DoD** — Un solve Leduc avec `target_exploitability_mbb = 5.0` s'arrête avant le nombre
maximal d'itérations, et l'exploitabilité mesurée à l'arrêt est bien inférieure à 5,0. Un
solve avec une cible inatteignable va jusqu'au maximum sans erreur.

**Vérification**
```bash
ctest --test-dir build/debug -R test_best_response_ii --output-on-failure
```

## BR-05 — Multiway : ne pas étiqueter « Nash »

**Priorité** `P1` · **Taille** `M` · **Dépendances** `BR-04`

**Fichiers** `src/solver/domain/best_response_ii.c`,
`include/poker_eval/solver/pe_solver.h`,
`tests/game_theory/test_kuhn_multiway_openspiel.c`

Pour un jeu à plus de 2 joueurs ou non zéro-somme, rendre `PE_GUARANTEE_NO_REGRET_ONLY` ou
`PE_GUARANTEE_EMPIRICAL` avec les BR gaps par joueur, le CCE gap et le déséquilibre
d'utilité. La garantie `PE_GUARANTEE_NASH` n'est jamais rendue dans ce cas.

**DoD** — Sur Kuhn 3 joueurs, `pe_solver_metrics()` rend `NO_REGRET_ONLY` et un NashConv
égal à celui de l'oracle indépendant existant, à `1e-9` près. Aucun chemin de code ne peut
rendre `NASH` pour `num_players > 2`.

**Vérification**
```bash
ctest --test-dir build/debug -R test_kuhn_multiway_openspiel --output-on-failure
```

---

# M6 — CFR+ et DCFR canoniques

## ALG-01 — Regret matching+ et clamp

**Priorité** `P0` · **Taille** `M` · **Dépendances** `BR-05`

**Fichiers** `src/solver/domain/regret_plus.c` (nouveau),
`tests/game_theory/test_cfr_plus.c` (nouveau), `tests/CMakeLists.txt`

Implémenter `pe_regret_ops_t` pour CFR+ : regret matching+ et clamp du regret cumulé à
zéro dans `end_iteration`. Aucune branche `if (enable_plus)` dans le traversal.

**DoD** — Après toute itération, aucun regret cumulé n'est négatif. Le clamp intervient
une fois par itération, pas une fois par visite (vérifié par compteur instrumenté).

**Vérification**
```bash
ctest --test-dir build/debug -R test_cfr_plus --output-on-failure
```

## ALG-02 — Averaging CFR+ pondéré et retardé

**Priorité** `P0` · **Taille** `M` · **Dépendances** `ALG-01`

**Fichiers** `src/solver/domain/average.c`, `tests/game_theory/test_cfr_plus.c`

Ajouter `PE_AVG_DELAYED_LINEAR` : les `averaging_delay` premières itérations ne
contribuent pas à la moyenne, les suivantes sont pondérées linéairement.

**DoD** — Avec `averaging_delay = 100`, la moyenne après 100 itérations est vide (aucune
contribution) ; à 101 itérations elle contient exactement une contribution de poids 1.

**Vérification**
```bash
ctest --test-dir build/debug -R test_cfr_plus --output-on-failure
```

## ALG-03 — DCFR canonique

**Priorité** `P0` · **Taille** `M` · **Dépendances** `ALG-02`, `EXT-07`

**Fichiers** `src/solver/domain/regret_dcfr.c`,
`tests/game_theory/test_cfr_dcfr.c` (nouveau), `tests/CMakeLists.txt`

Implémenter le DCFR de Brown & Sandholm : à l'itération `t`, les regrets **positifs** sont
multipliés par `t^α / (t^α + 1)`, les regrets **négatifs** par `t^β / (t^β + 1)`, et la
contribution à la stratégie moyenne est pondérée par `(t / (t+1))^γ`. Défauts `α = 1.5`,
`β = 0`, `γ = 2`.

Le comportement historique — où `dcfr_beta` servait d'exposant d'averaging — est conservé
sous `PE_REGRET_LEGACY_DCFR` tant que la migration n'est pas close.

**DoD** — Un test vérifie séparément les trois pondérations sur des regrets connus, positifs
et négatifs. `β = 0` rend bien un facteur `1/2` sur les regrets négatifs, pas `0` ni `1`.
DCFR converge sur Leduc à une exploitabilité inférieure à celle du CFR vanilla au même
nombre d'itérations.

**Vérification**
```bash
ctest --test-dir build/debug -R test_cfr_dcfr --output-on-failure
```

## ALG-04 — Presets `cfr+` et `dcfr`

**Priorité** `P0` · **Taille** `S` · **Dépendances** `ALG-03`

**Fichiers** `src/solver/domain/registry.c`, `tests/test_pe_registry.c`

Déclarer les presets et leurs combinaisons autorisées dans la matrice, y compris
`external-dcfr` marqué comme non disponible tant que `LNB-01` n'est pas validé.

**DoD** — `cfr+` et `dcfr` se résolvent. `external-dcfr` rend `PE_VALID_ERROR` avec un
message indiquant la dépendance non satisfaite, et non un fallback.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_registry --output-on-failure
```

## ALG-05 — Préserver ECFR existant

**Priorité** `P2` · **Taille** `M` · **Dépendances** `ALG-04`

**Fichiers** `src/solver/domain/regret_legacy_exp.c` (nouveau),
`src/solver/domain/registry.c`

Porter le comportement `enable_ecfr` / `ecfr_lambda` actuel en `PE_REGRET_LEGACY_EXP` +
`PE_POLICY_EXPONENTIAL`, sans chercher à le rendre canonique. Le preset `ecfr` est marqué
`EXPERIMENTAL` dans le registry.

**DoD** — Un solve `ecfr` rend exactement les mêmes valeurs qu'avant la migration, sur
`test_akq_game` et `test_leduc_openspiel`.

**Vérification**
```bash
ctest --test-dir build/debug -L game_theory --output-on-failure
```

---

# M7 — CPU parallèle

## PAR-01 — Accumulateurs de deltas thread-local

**Priorité** `P0` · **Taille** `M` · **Dépendances** `ALG-05`

**Fichiers** `src/solver/domain/batch.c` (nouveau),
`include/poker_eval/solver/pe_batch.h` (nouveau),
`tests/test_pe_batch.c` (nouveau), `tests/CMakeLists.txt`

Un `pe_update_batch_t` par thread : ids d'infoset, offsets, comptes, deltas de regret,
deltas de moyenne. Pas d'écriture directe dans le storage pendant la traversée.

**DoD** — Un lot de 100 000 updates se remplit et se relit sans perte. La fusion de deux
lots portant le même infoset additionne correctement les deltas.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_batch --output-on-failure
```

## PAR-02 — Réduction déterministe

**Priorité** `P0` · **Taille** `M` · **Dépendances** `PAR-01`

**Fichiers** `src/solver/domain/batch.c`, `tests/test_pe_batch.c`

Fusionner les lots dans un **ordre fixe** (par id d'infoset croissant, puis par index de
thread croissant), indépendant de l'ordre d'arrivée des threads. Trier et réduire les
updates du même infoset avant application, ce qui limitera aussi les atomiques côté GPU.

**DoD** — La même charge de travail, fusionnée depuis 1, 2, 4 et 8 lots dans des ordres
d'arrivée différents, produit un storage **bit-identique**.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_batch --output-on-failure
```

## PAR-03 — Backend `cpu_par`

**Priorité** `P0` · **Taille** `L` · **Dépendances** `PAR-02`

**Fichiers** `src/solver/adapters/compute_cpu_par.c` (nouveau),
`include/poker_eval/solver/pe_compute.h` (nouveau), `src/CMakeLists.txt`

Implémenter `pe_compute_ops_t` en OpenMP. La découpe du travail se fait sur les outcomes
de chance racine ou sur les sous-arbres du premier niveau de décision. **Aucune atomique
directe sur les regrets** : tout passe par les lots de `PAR-01`/`PAR-02`.

**DoD** — Le backend s'enregistre, `capabilities()` annonce `CPU_PARALLEL` et
`DETERMINISTIC`, et un solve à 8 threads se termine sans erreur ASan/TSan.

**Vérification**
```bash
cmake --preset debug -DENABLE_TSAN=ON -B build/tsan && cmake --build build/tsan
ctest --test-dir build/tsan -L cfr --output-on-failure
```

## PAR-04 — Gate de parité `cpu_ref` / `cpu_par`

**Priorité** `P0` · **Taille** `M` · **Dépendances** `PAR-03`

**Fichiers** `tests/game_theory/test_backend_parity.c` (nouveau),
`tests/CMakeLists.txt`

**La gate F.** Le même solve, même seed, exécuté sur `cpu_ref` (1 thread) et sur `cpu_par`
(2, 4 et 8 threads).

**DoD** — Les storages finaux sont **bit-identiques** entre `cpu_ref` et chaque
configuration de `cpu_par`. Le test compare les tableaux de regret et de moyenne octet par
octet, pas à une tolérance.

**Vérification**
```bash
ctest --test-dir build/debug -R test_backend_parity --output-on-failure
```

---

# M8 — Interopérabilité MonkerSolver

Objectif unique : obtenir un oracle externe à l'échelle du poker réel. Lecture seulement.

## MKR-01 — Lecteur d'en-tête `.tree`

**Priorité** `P1` · **Taille** `M` · **Dépendances** `PAR-04`

**Fichiers** `src/solver/adapters/monker_tree_reader.c` (nouveau),
`include/poker_eval/solver/pe_monker.h` (nouveau),
`tests/test_monker_tree.c` (nouveau), `tests/CMakeLists.txt`

Lire l'en-tête du format 33487 : signature `int64` (accepter 33486 à 33490), format
interne, joueurs, premier à agir, street, committed par joueur **uniquement si
street == 0**, dead money, stacks.

**DoD** — Un fichier de signature inconnue est refusé avec un code d'erreur explicite, pas
par un crash. Un fichier tronqué est refusé. Une fixture d'en-tête connue est lue avec
les bonnes valeurs de joueurs, street et stacks.

**Vérification**
```bash
ctest --test-dir build/debug -R test_monker_tree --output-on-failure
```

## MKR-02 — Lecteur de nœuds `.tree`

**Priorité** `P1` · **Taille** `M` · **Dépendances** `MKR-01`

**Fichiers** `src/solver/adapters/monker_tree_reader.c`, `tests/test_monker_tree.c`

Lire le flux récursif de nœuds (`char` action, `char` nombre d'enfants) et le convertir en
`mpf_tree_def_t`. Les codes de taille suivent la table du format (`fold` 0, `call`/`check`
1, `pot` 2, `all`/`allin` 3, `1/2` 4, `min` 5, `bet` 6, `1/4` 7, `3/4` 9, `auto` 10,
`N%` = `40000 + N`).

**DoD** — Le nombre de nœuds lus correspond au nombre annoncé par la structure, la
profondeur maximale est finie, et un arbre relu puis reparcouru rend la même topologie
(comparaison de la séquence d'actions en parcours préfixe).

**Vérification**
```bash
ctest --test-dir build/debug -R test_monker_tree --output-on-failure
```

## MKR-03 — Ranges du `.tree`

**Priorité** `P1` · **Taille** `M` · **Dépendances** `MKR-02`, `RNG-01`

**Fichiers** `src/solver/adapters/monker_tree_reader.c`, `tests/test_monker_tree.c`

Quand le booléen de présence est vrai, lire `int32 × joueurs × 270 725` (Omaha) ou
`× 1 326` (Hold'em) en virgule fixe, diviseur `2 147 483 647`, et construire un
`pe_player_range_t` par joueur.

**DoD** — Les poids relus sont dans `[0, 1]`, la somme par joueur est strictement positive,
et un round-trip virgule fixe → double → virgule fixe est stable à 1 ULP près.

**Vérification**
```bash
ctest --test-dir build/debug -R test_monker_tree --output-on-failure
```

## MKR-04 — Conteneur `.mkr`

**Priorité** `P1` · **Taille** `M` · **Dépendances** `MKR-03`

**Fichiers** `src/solver/adapters/monker_mkr_reader.c` (nouveau),
`tests/test_monker_mkr.c` (nouveau), `tests/CMakeLists.txt`

Lire l'archive ZIP, avec la particularité du writer Java : les **noms d'entrées sont en
UTF-16BE avec BOM** (`FE FF`), pas en UTF-16LE ni en UTF-8. Gérer le drapeau « data
descriptor ». Exposer la liste des entrées.

**DoD** — Une fixture `.mkr` rend les noms d'entrées attendus (`tree`,
`storedstrategy0..3`, `iterations`, `flopBuckets`, …). Un BOM UTF-16LE est rejeté avec un
message distinct du cas « archive corrompue ».

**Vérification**
```bash
ctest --test-dir build/debug -R test_monker_mkr --output-on-failure
```

## MKR-05 — Scalaires et stratégies du `.mkr`

**Priorité** `P1` · **Taille** `L` · **Dépendances** `MKR-04`

**Fichiers** `src/solver/adapters/monker_mkr_reader.c`, `tests/test_monker_mkr.c`

Décompresser les entrées `storedstrategyN` (zlib imbriqué), puis désérialiser le flux Java :
scalaires (`Integer`, `Long`, `Double`), tableaux (`evs`, `eviters`, `conv`), et les
fréquences en virgule fixe sur 2 octets par classe de main. Un `TC_NULL` signifie « pas de
stratégie sur cette street ».

**DoD** — Sur une fixture connue, `game`, `iterations`, `flopBuckets`, `rakepercent`,
`rakecap` et `rakeflags` sont lus avec les valeurs attendues, et `storedstrategy0` rend un
nombre de classes de mains cohérent avec le nombre de buckets déclaré.

**Vérification**
```bash
ctest --test-dir build/debug -R test_monker_mkr --output-on-failure
```

## MKR-06 — Grammaire de filtres de nœuds

**Priorité** `P1` · **Taille** `L` · **Dépendances** `MKR-02`

**Fichiers** `src/solver/adapters/monker_filters.c` (nouveau),
`include/poker_eval/solver/pe_monker.h`,
`tests/test_monker_filters.c` (nouveau), `tests/CMakeLists.txt`

Implémenter la grammaire complète : `ou := et ("," et)*`, `et := atome (":" atome)*`,
`atome := "!"? "^"? mot_clé (opérateur valeur)?`, plus les parenthèses. Les 36 mots-clés,
dont `prevstreet` qui est caché et vaut `^street`.

Trois comportements à reproduire exactement :
- `pot` s'exprime en **petites blindes** (le prédicat divise la valeur interne par 1000) ;
- **ni `pot` ni `spr` ne comptent le dead money** ;
- `^` préfixe un mot-clé, pas une valeur (`^street=1` compile, `street=^1` non), et l'ordre
  des préfixes est fixe (`!^pre` compile, `^!pre` non).

**DoD** — Le parseur accepte les 36 mots-clés, refuse un mot-clé inconnu, et reproduit les
asymétries de séparateurs (`pre,` et `pre:` compilent, `,pre` et `:pre` non, la chaîne vide
est refusée).

**Vérification**
```bash
ctest --test-dir build/debug -R test_monker_filters --output-on-failure
```

## MKR-07 — Notation PPT pour les ranges

**Priorité** `P1` · **Taille** `L` · **Dépendances** `MKR-06`, `RNG-01`

**Fichiers** `src/solver/adapters/range_ppt.c` (nouveau),
`tests/test_range_ppt.c` (nouveau), `tests/CMakeLists.txt`

Implémenter la grammaire : une définition est une suite de jetons portant un rang optionnel
(`2`–`A`, ou `r` variable) et une couleur optionnelle (`s`/`h`/`d`/`c`, ou `x`/`y`/`z`
variables). Une main correspond s'il existe une affectation **injective** des jetons à ses
cartes, variables liées de façon cohérente, deux variables distinctes prenant des valeurs
distinctes. Opérateurs `,` (OU), `:` (ET), `!` (différence binaire), `+` (cette paire ou
mieux), `( )`.

**DoD** — Les comptes de combos Omaha suivants sont exacts : `*` = 270 725, `A` = 76 145,
`AA` = 6 961, `AAA` = 193, `AAAA` = 1, `AK` = 17 316, `AAKK` = 36, `AKQJ` = 256,
`AA,KK` = 13 886, `AA:KK` = 36, `AA!KK` = 6 925, `s` = 188 474, `ss` = 69 667,
`ds` = 121 173, `xxxx` = 2 860, `rr` = 87 685, `AKs` = 4 610. `!AA` rend 0 (différence
binaire, pas négation unaire). Une définition à cinq rangs (`AKQJT`) est **refusée**, alors
que MonkerSolver lève dessus.

**Vérification**
```bash
ctest --test-dir build/debug -R test_range_ppt --output-on-failure
```

---

# M9 — GPU-1 et GPU-2

## GPU-01 — Port de calcul et backend `cpu_ref`

**Priorité** `P0` · **Taille** `M` · **Dépendances** `MKR-07`

**Fichiers** `include/poker_eval/solver/pe_compute.h`,
`src/solver/adapters/compute_cpu_ref.c` (nouveau), `src/CMakeLists.txt`

Formaliser `pe_compute_ops_t` (§4.5) et fournir `cpu_ref` : 1 thread, F64, batch de 1,
ordre stable. C'est l'oracle de parité de tous les backends suivants ; il ne doit jamais
être supprimé.

**DoD** — `cpu_ref` annonce `DETERMINISTIC` et aucune capability GPU. Deux exécutions du
même solve produisent des storages bit-identiques.

**Vérification**
```bash
ctest --test-dir build/debug -R test_backend_parity --output-on-failure
```

## GPU-02 — Port d'évaluation sur le moteur existant

**Priorité** `P0` · **Taille** `M` · **Dépendances** `GPU-01`

**Fichiers** `include/poker_eval/solver/pe_evaluator.h` (nouveau),
`src/solver/adapters/evaluator_equity.c` (nouveau),
`src/solver/adapters/evaluator_gpu.c` (nouveau)

Envelopper le moteur d'équité CPU et l'évaluation GPU par lots
(`src/gpu/eval_batched_*.c`) derrière un port unique. Le domaine ne référence plus
`EvalContext`, CUDA ni OpenCL.

**DoD** — Aucun `#include` de `poker_eval/gpu/` ni de `eval_context.h` sous
`src/solver/domain/`.

**Vérification**
```bash
grep -rc 'poker_eval/gpu/\|eval_context.h' src/solver/domain/   # attendu : 0 partout
cmake --build build/debug
```

## GPU-03 — Évaluation terminale par lot, CUDA

**Priorité** `P1` · **Taille** `L` · **Dépendances** `GPU-02`

**Fichiers** `src/solver/adapters/compute_cuda.c` (nouveau), `src/gpu/CMakeLists.txt`

Implémenter `terminal_eval_batch()` sur CUDA en réutilisant les noyaux
`eval_batched_cuda.c` existants. La capability `GPU_TERMINAL_EVAL` reste **désactivée par
défaut** jusqu'à `GPU-05`.

**DoD** — La build sans CUDA reste verte (compilation conditionnelle). Avec CUDA, un lot de
100 000 terminaux s'évalue sans erreur et la capability est annoncée mais non sélectionnée
par `AUTO`.

**Vérification**
```bash
cmake --preset debug && cmake --build build/debug            # sans CUDA
cmake -B build/cuda -DENABLE_CUDA=ON && cmake --build build/cuda
```

## GPU-04 — Évaluation terminale par lot, OpenCL

**Priorité** `P1` · **Taille** `L` · **Dépendances** `GPU-03`

**Fichiers** `src/solver/adapters/compute_opencl.c` (nouveau), `src/gpu/CMakeLists.txt`

Même contrat que `GPU-03`, sur `eval_batched_opencl.c`.

**DoD** — Identique à `GPU-03`, backend OpenCL. Les deux backends exposent la même
signature et se sélectionnent par le même code de resolver.

**Vérification**
```bash
cmake -B build/ocl -DENABLE_OPENCL=ON && cmake --build build/ocl
```

## GPU-05 — Gate de parité GPU-1

**Priorité** `P0` · **Taille** `M` · **Dépendances** `GPU-04`

**Fichiers** `tests/test_solver_gpu_parity.c` (nouveau), `tests/CMakeLists.txt`,
`src/solver/domain/registry.c`

**La gate H, premier niveau.** Comparer `terminal_eval_batch` entre `cpu_ref`, CUDA et
OpenCL sur le même lot, puis n'autoriser `AUTO` à sélectionner un backend GPU **qu'après**
succès de cette comparaison.

**DoD** — Sur 1 000 000 de terminaux tirés au hasard, l'écart maximal entre CPU et chaque
backend GPU est inférieur à `1e-6` en F32 et à `1e-12` en F64. Le test est ignoré
proprement (`SKIP`, pas échec) quand aucun GPU n'est disponible. Tant que la gate n'est pas
enregistrée comme passée, `AUTO` sélectionne CPU.

**Vérification**
```bash
ctest --test-dir build/cuda -R test_solver_gpu_parity --output-on-failure
ctest --test-dir build/debug -R test_solver_gpu_parity --output-on-failure   # SKIP attendu
```

---

# M10 — Abstraction et lane B

## ABS-01 — Port d'abstraction

**Priorité** `P1` · **Taille** `M` · **Dépendances** `GPU-05`

**Fichiers** `include/poker_eval/solver/pe_abstraction.h` (nouveau),
`src/solver/domain/abstraction.c` (nouveau)

Formaliser `pe_abstraction_ops_t` : entraîner, sérialiser (`.pe_sbk`), charger, et rendre
`bucket_of(hand, board, street)` et `texture_of(board, street)`. Les implémentations sont
`strength_bucketing.c` et `board_texture.c`, qui existent déjà.

**DoD** — Une abstraction entraînée, sérialisée puis rechargée rend exactement les mêmes
ids de bucket sur 10 000 mains.

**Vérification**
```bash
ctest --test-dir build/debug -R 'test_strength_bucketing|test_board_texture' --output-on-failure
```

## ABS-02 — Câbler les buckets dans la clé d'infoset MPF

**Priorité** `P1` · **Taille** `L` · **Dépendances** `ABS-01`

**Fichiers** `src/engine/solvers/cfr/multiway_postflop_adapter.c`,
`tests/test_mpf_abstraction.c` (nouveau), `tests/CMakeLists.txt`

`mpf_infoset_key()` hache aujourd'hui le board canonique exact via `mpf_pattern_hash()` :
l'abstraction n'est câblée que dans les adapters river/turn spécialisés. Quand
`strength_buckets_per_street > 0`, remplacer la composante « main + board » par
`(bucket_id, texture_id)`.

**DoD** — Avec `strength_buckets_per_street = 30`, le nombre d'infosets distincts d'un
solve flop est réduit d'au moins un ordre de grandeur par rapport au même solve sans
abstraction, et la valeur racine reste dans une tolérance de 2 % de la valeur exacte.
Avec `strength_buckets_per_street = 0`, la clé est **inchangée au bit près**.

**Vérification**
```bash
ctest --test-dir build/debug -R test_mpf_abstraction --output-on-failure
```

## ABS-03 — Câbler le filtre de texture

**Priorité** `P1` · **Taille** `M` · **Dépendances** `ABS-02`

**Fichiers** `src/engine/solvers/cfr/multiway_postflop_adapter.c`,
`tests/test_mpf_abstraction.c`

Brancher `texture_filter_level` par street sur le chemin général. `PE_TEXTURE_FILTER_NONE`
laisse la clé inchangée.

**DoD** — Les 5 niveaux produisent des nombres d'infosets monotones décroissants de
`PERFECT` à `NONE`, mesurés sur le même spot.

**Vérification**
```bash
ctest --test-dir build/debug -R test_mpf_abstraction --output-on-failure
```

## ABS-04 — Précision `fixed16`

**Priorité** `P2` · **Taille** `L` · **Dépendances** `ABS-03`

**Fichiers** `src/solver/domain/storage_v2.c`,
`include/poker_eval/solver/pe_solver_config.h`,
`tests/test_pe_storage_precision.c` (nouveau), `tests/CMakeLists.txt`

Mode de stockage des regrets en entier 16 bits avec facteur d'échelle par infoset, pour la
lane B à grande échelle. C'est le mode qu'emploie MonkerSolver.

**DoD** — Sur un solve Leduc, `fixed16` divise l'occupation mémoire par au moins 3,5 par
rapport à F64, et la valeur racine reste à moins de 1 % de celle du solve F64. Un
dépassement d'échelle est détecté et re-normalisé, jamais silencieusement tronqué.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_storage_precision --output-on-failure
```

## LNB-01 — External Sampling MCCFR

**Priorité** `P1` · **Taille** `L` · **Dépendances** `ABS-04`, `CHN-03`

**Fichiers** `src/solver/domain/traversal_external.c` (nouveau),
`tests/game_theory/test_external_mccfr.c` (nouveau), `tests/CMakeLists.txt`

Traversée External Sampling : un outcome de chance échantillonné, une action échantillonnée
pour chaque adversaire, **toutes** les actions explorées pour le joueur mis à jour. Le
joueur mis à jour est un champ explicite du contexte, jamais déduit.

**DoD** — Sur Kuhn 2p et Leduc, la valeur de la politique converge vers celle du solve
`FULL_SCALAR` à `5e-3` près en 1 000 000 d'itérations, avec un seed fixé. Le test échoue si
l'écart dépasse la tolérance.

**Vérification**
```bash
ctest --test-dir build/debug -R test_external_mccfr --output-on-failure
```

## LNB-02 — Preset `external-mccfr` et averaging adapté au sampling

**Priorité** `P1` · **Taille** `M` · **Dépendances** `LNB-01`

**Fichiers** `src/solver/domain/average.c`, `src/solver/domain/registry.c`,
`tests/test_pe_registry.c`, `tests/game_theory/test_external_mccfr.c`

L'averaging uniforme est incorrect sous sampling : la contribution doit être pondérée par
l'inverse de la probabilité d'échantillonnage. Ajouter le mode adapté et l'exiger dans la
matrice pour tout preset `EXTERNAL` ou `OUTCOME`.

**DoD** — Un preset `EXTERNAL` combiné à un averaging non compatible rend `PE_VALID_ERROR`.
`external-dcfr`, bloqué depuis `ALG-04`, se résout maintenant.

**Vérification**
```bash
ctest --test-dir build/debug -R 'test_pe_registry|test_external_mccfr' --output-on-failure
```

---

# M11 — GPU-3/4, pruning, Outcome Sampling

## GPU-06 — Noyaux stratégie et regret par lot

**Priorité** `P2` · **Taille** `L` · **Dépendances** `LNB-02`

**Fichiers** `src/gpu/kernels_regret_cuda.cu` (nouveau),
`src/gpu/kernels_regret_opencl.c` (nouveau),
`src/solver/adapters/compute_cuda.c`, `src/solver/adapters/compute_opencl.c`

Implémenter `strategy_batch()` et `apply_update_batch()` sur GPU, sur les tableaux ragged
de `STO-02`. Les updates étant déjà triés et réduits par `PAR-02`, les atomiques sont
minimisées.

**DoD** — Un lot de 1 000 000 d'updates s'applique sans erreur. Capability
`GPU_REGRET_UPDATE` annoncée mais non sélectionnée par `AUTO` avant `GPU-07`.

**Vérification**
```bash
cmake --build build/cuda && ctest --test-dir build/cuda -R test_solver_gpu_parity --output-on-failure
```

## GPU-07 — Gate de parité GPU-2/GPU-3

**Priorité** `P2` · **Taille** `M` · **Dépendances** `GPU-06`

**Fichiers** `tests/test_solver_gpu_parity.c`, `src/solver/domain/registry.c`

Étendre la gate de `GPU-05` à `vector_showdown` et `apply_update_batch`. Ce n'est qu'après
succès que `AUTO` peut placer l'étage `update` sur GPU.

**DoD** — Un solve complet exécuté avec l'étage `update` sur GPU rend la même stratégie
moyenne que le même solve sur `cpu_ref`, à `1e-6` près en F32 et `1e-11` en F64.

**Vérification**
```bash
ctest --test-dir build/cuda -R test_solver_gpu_parity --output-on-failure
```

## RBP-01 — Regret-Based Pruning

**Priorité** `P2` · **Taille** `L` · **Dépendances** `GPU-07`

**Fichiers** `src/solver/domain/pruning_rbp.c` (nouveau),
`include/poker_eval/solver/pe_pruning.h` (nouveau),
`tests/game_theory/test_rbp.c` (nouveau), `tests/CMakeLists.txt`

Implémenter `pe_pruning_ops_t` : une action dont le regret cumulé est suffisamment négatif
n'est plus explorée pendant un nombre d'itérations borné, puis réévaluée. Le registry
interdit RBP avec les traversées `OUTCOME`.

**DoD** — Sur Leduc, RBP réduit le nombre de nœuds visités d'au moins 20 % pour une
exploitabilité finale équivalente à `1e-4` près. `RBP` + `OUTCOME` rend `PE_VALID_ERROR`.

**Vérification**
```bash
ctest --test-dir build/debug -R 'test_rbp|test_pe_registry' --output-on-failure
```

## OUT-01 — Outcome Sampling MCCFR

**Priorité** `P2` · **Taille** `L` · **Dépendances** `LNB-02`

**Fichiers** `src/solver/domain/traversal_outcome.c` (nouveau),
`tests/game_theory/test_outcome_mccfr.c` (nouveau), `tests/CMakeLists.txt`

Une seule trajectoire par itération, exploration `epsilon`, probabilité d'échantillonnage
explicite, poids d'importance. Implémentation séparée de `traversal_external.c` : les deux
ne partagent que le sampler et le RNG.

**DoD** — Sur Kuhn 2p, la valeur de la politique converge vers `-1/18` à `1e-2` près en
10 000 000 d'itérations avec `epsilon = 0.6`, seed fixé. La variance décroît avec le nombre
d'itérations (mesurée sur 5 seeds).

**Vérification**
```bash
ctest --test-dir build/debug -R test_outcome_mccfr --output-on-failure
```

---

# M12 — API, CLI, checkpoint, dépréciation

## API-01 — Surface publique `pe_solver_*`

**Priorité** `P0` · **Taille** `L` · **Dépendances** `OUT-01`

**Fichiers** `include/poker_eval/solver/pe_solver.h`, `src/solver/domain/solver.c`,
`tests/test_pe_solver_api.c` (nouveau), `tests/CMakeLists.txt`

Implémenter le cycle de vie complet du §4.1 : `create`, `validate`, `capabilities`,
`estimate`, `plan`, `run`, `pause`, `resume`, `stop`, `progress`, `strategy`, `metrics`,
`save`, `load`, `destroy`.

**DoD** — Chaque fonction rend un code d'erreur défini quand elle est appelée dans un état
invalide (par exemple `run` avant `validate`, `strategy` avant `run`), sans crash ni
comportement indéfini. Le test couvre les 15 fonctions dans un état valide et un état
invalide.

**Vérification**
```bash
ctest --test-dir build/debug -R test_pe_solver_api --output-on-failure
```

## API-02 — Wrapper de compatibilité

**Priorité** `P0` · **Taille** `M` · **Dépendances** `API-01`

**Fichiers** `src/engine/solvers/cfr/cfr_core.c`,
`include/poker_eval/engine/solvers/cfr/cfr_core.h`

`cfr_solve()` et les `pe_cfr_*` existants deviennent des adaptateurs sur `pe_solver_*`.
Les anciens booléens (`enable_dcfr`, `enable_linear_avg`, `enable_ecfr`, `enable_mccfvfp`)
sont traduits en plan legacy par une couche de compatibilité.

**DoD** — Toute la suite existante — `ctest -L cfr` et `ctest -L game_theory` — passe sans
modification des tests. Aucun appelant existant (benchmarks, outils, exemples) n'est
modifié.

**Vérification**
```bash
ctest --test-dir build/debug -L cfr --output-on-failure
ctest --test-dir build/debug -L game_theory --output-on-failure
```

## API-03 — CLI : presets, introspection, plan

**Priorité** `P1` · **Taille** `L` · **Dépendances** `API-02`

**Fichiers** `tools/mpf_run_with_metrics.c`, `tools/CMakeLists.txt`

Ajouter `--algorithm <preset>`, `--backend <kind>`, les surcharges expertes
(`--traversal`, `--regret`, `--averaging`, `--alpha/--beta/--gamma`, `--precision`, les
backends par étage, les tailles de lot), et l'introspection : `--list-algorithms`,
`--list-backends`, `--show-capabilities`, `--validate-only`, `--estimate-only`,
`--print-execution-plan`.

**DoD** — `--validate-only` sur une combinaison invalide rend un code de sortie non nul et
un message nommant le conflit, **sans lancer d'itération**. `--print-execution-plan` affiche
le backend effectif par étage et la précision résolue.

**Vérification**
```bash
./build/debug/bin/mpf_run_with_metrics --list-algorithms
./build/debug/bin/mpf_run_with_metrics --algorithm dcfr --backend cpu --validate-only ; echo "exit=$?"
```

## API-04 — Checkpoint v2 indépendant du backend

**Priorité** `P1` · **Taille** `L` · **Dépendances** `API-03`

**Fichiers** `src/solver/adapters/persist_checkpoint.c` (nouveau),
`include/poker_eval/solver/pe_persist.h` (nouveau),
`tests/test_pe_checkpoint_v2.c` (nouveau), `tests/CMakeLists.txt`

Format portant : version, config résolue, hash du jeu et de l'arbre, infosets, regrets,
moyennes, locks, itération, version et état du RNG, précision numérique portable. Les
checkpoints v1 restent lisibles.

**DoD** — Un solve interrompu à l'itération 500 puis repris depuis le checkpoint rend, à
l'itération 1 000, exactement le même storage qu'un solve ininterrompu de 1 000 itérations
(comparaison bit-à-bit sur `cpu_ref`). Un checkpoint écrit sur `cpu_ref` se relit sur
`cpu_par`. Un checkpoint v1 se charge encore.

**Vérification**
```bash
ctest --test-dir build/debug -R 'test_pe_checkpoint_v2|test_cfr_checkpoint' --output-on-failure
```

## CLN-01 — Déprécier le solveur GPU autonome

**Priorité** `P1` · **Taille** `M` · **Dépendances** `API-04`

**Fichiers** `src/gpu/gpu_cfr.c`, `src/gpu/gpu_cfr_adapter.c`,
`include/poker_eval/gpu/gpu_cfr.h`, `CHANGELOG.md`,
`docs/gpu/guides/GPU_ACCELERATION_GUIDE.md`

`gpu_cfr_solve()` est une boucle vide avec `TODO`, et `gpu_cfr_adapter.c` est un CFR
vanilla exhaustif nommé « Monte Carlo », limité à 2 joueurs, qui confond `state_key` (un
pointeur heap sous MPF) avec la clé d'infoset. Les marquer dépréciés, les retirer du chemin
principal et du resolver `AUTO`, documenter le remplacement. **Ne rien supprimer** : les
noyaux réutilisables restent, et la suppression fera l'objet d'un ticket ultérieur.

**DoD** — Aucun appel à `gpu_cfr_solve()` ni à `gpu_cfr_adapter_*` depuis
`src/solver/`. Les en-têtes portent un attribut de dépréciation. Les tests
`test_gpu_cfr_converter` et `test_gpu_cfr_opencl` passent toujours. Le guide GPU indique le
chemin de remplacement.

**Vérification**
```bash
grep -rc 'gpu_cfr_solve\|gpu_cfr_adapter' src/solver/   # attendu : 0 partout
ctest --test-dir build/debug -R 'test_gpu_cfr' --output-on-failure
```

---

# Annexe A — Tickets dont le comportement change délibérément

Trois tickets modifient un résultat existant. Chacun doit être consigné dans
`CHANGELOG.md` et ne doit pas être noyé dans une PR plus large.

| Ticket | Changement | Justification |
|---|---|---|
| `EXT-07` | Le discount DCFR passe de `d^N` (une fois par visite) à `d` (une fois par itération) | Le comportement actuel n'est pas du DCFR |
| `ALG-03` | `dcfr_beta` cesse d'être un exposant d'averaging et devient le discount des regrets négatifs | Sémantique canonique de Brown & Sandholm |
| `BR-01` | `cfr_best_response_value()` est renommé `..._perfect_info()` | Le nom actuel laisse croire à un BR correct |

# Annexe B — Ce qui n'est pas dans ce backlog

- **Suppression** de `gpu_cfr_*` : `CLN-01` déprécie seulement. La suppression viendra
  après une période de dépréciation, dans un ticket dédié.
- **Écriture** des formats MonkerSolver : lecture seule, aucun besoin identifié.
- **Traversée GPU complète** (GPU-5/GPU-6 de l'architecture) : à ouvrir seulement si les
  mesures de `GPU-07` la justifient.
- **Lane C / Deep CFR** : autre architecture, hors périmètre v3.
- **Extension du solveur aux 49 variantes d'équité** : `pe_game_rules_t` rend l'ajout
  possible ; chaque variante sera son propre ticket, avec sa propre validation.

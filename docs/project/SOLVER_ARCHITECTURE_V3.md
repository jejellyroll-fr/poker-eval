# poker-eval — Architecture Solver v3

**Statut :** architecture cible. Remplace les documents « Architecture Solver v2 » et
« Backlog Solver Architecture v2 ».
**Base analysée :** `d3f5a909` (le seul commit depuis `b9954566` ne touche que le moteur
d'équité : aucun élément du diagnostic solver n'a bougé).

**Objectif.** Un moteur unique, hexagonal, portant deux familles de résolution sous les
mêmes ports : un CFR **vectoriel exact** pour le postflop range-vs-range, et un CFR
**abstrait/échantillonné** pour le multiway profond. Le moteur d'équité existant devient
un composant du domaine, pas une bibliothèque annexe. CPU parallèle et GPU sont des
adaptateurs, jamais des solveurs concurrents.

---

## 1. Ce que le dépôt contient déjà

Inventaire vérifié dans le code, pas repris d'un audit antérieur. C'est le point de départ
réel : la plus grande partie du travail v3 est du **branchement et de l'extraction**, pas
du développement à partir de zéro.

### 1.1 Acquis solides

| Composant | Emplacement | État |
|---|---|---|
| Moteur d'équité multi-variantes | `src/equity/`, `src/range/` | **49 variantes** dans `enum_game_t` ; énumération, SIMD, multithread, tables préflop |
| Évaluation GPU par lots | `src/gpu/eval_batched_{cuda,opencl}.c` | CUDA + OpenCL + AUTO, hi/lo, parité CPU/GPU testée (`tests/test_cpu_gpu_backend_parity.c`) |
| Suite de qualification mathématique | `tests/game_theory/` | Kuhn 2p/3p/4p, Leduc, AKQ, LP séquentiel exact, fixtures OpenSpiel 2.0.2 |
| Abstraction « Strength buckets + Texture filter » | `strength_bucketing.c`, `board_texture.c`, `hand_clustering.c` | k-means EHS/EHS², 5 niveaux de texture, sérialisable `.pe_sbk` |
| Re-solve de sous-jeu | `cfr_resolve.c` | Gadget CFR-D (Burch/Johanson/Bowling 2014) + mode trunk-locked |
| Nodelock exact + relock périodique | `cfr_core.c`, FEAT-11/FEAT-12 | Lock strict, mesure de la perte d'EV contrefactuelle par infoset |
| Stockage compact | `mpf_compact_storage.c` | Quantification 16 bits, `.pe_sol` / `.pe_tree`, mmap |
| Rétention sélective par street | `keep_avg_strategy_mask`, `keep_ev_mask` | Équivalent des `keep_average_streets` / `keep_ev_streets` de MonkerSolver |
| Modèle de jeu multiway postflop | `multiway_postflop_adapter.c` | Hold'em, PLO4/5/6, Short Deck, pot-limit, préflop configurable, chance turn/river, bunching |
| Arbre déclaratif | `mpf_tree.c` | JSON, profils de mise, profils de range, locks par action, snapshots |
| ISMCTS | `src/engine/solvers/ismcts/` | Solveur alternatif, hors périmètre CFR |

### 1.2 Manques structurants

Chacun est vérifié dans le code et bloque un usage réel du solveur.

| Manque | Preuve |
|---|---|
| **Pas de ranges.** Le modèle porte une main fixe par joueur | `mask_t hole[MPF_MAX_PLAYERS]` + `hole_specified[]` dans `mpf_config_t` |
| **Pas de forme vectorielle.** Une traversée = un matchup de mains connues | aucune structure de valeurs par combo dans le solveur |
| **Pas de chance flop.** Préflop → flop révèle un board figé | `mpf_advance_street()` appelle `mpf_update_board(st, 3)` ; `mpf_enter_chance()` ne couvre que turn et river |
| **L'exploitabilité publique n'est qu'une borne supérieure.** `cfr_best_response_value()` / `cfr_exploitability()` maximisent par *état*, pas par infoset | `best_response_recursive()` ; documenté dans `tests/game_theory/README.md` |
| **Le BR correct existe mais n'est ni public ni dimensionné.** `cfr_best_response_value_infoset()` est un BR par infoset itéré jusqu'au point fixe — mais il est `static`, et sa table d'infosets est balayée linéairement à chaque insertion | `cfr_core.c`, utilisé seulement par `cfr_audit_multiway()` ; O(n²) en infosets, l'arbre reparcouru à chaque tour |
| **DCFR non canonique.** Le discount s'applique à chaque *visite* d'infoset, pas une fois par itération → le regret est multiplié par `d^N` | `cfr_traverse_recursive()` appelle `cfr_storage_update_regret_at_street(..., discount)` à chaque visite ; `dcfr_beta` sert d'exposant d'averaging au lieu de discounter les regrets négatifs |
| **Traversée monolithique.** Chance, regret matching, DCFR, averaging, locks, relock, flow focus et EV dans une fonction de ~320 lignes | `cfr_traverse_recursive()` |
| **Aucun parallélisme dans le solveur.** Zéro `#pragma omp` sous `src/engine/solvers/` | OpenMP n'apparaît que dans `src/ofc/`, `src/core/`, `src/utils/` |
| **Solveur GPU autonome non fonctionnel** | `gpu_cfr_solve()` est une boucle vide avec `TODO` ; `gpu_cfr_adapter.c` est un CFR vanilla exhaustif nommé « Monte Carlo », 2 joueurs, et confond `state_key` (pointeur heap) avec la clé d'infoset |
| **Abstraction non câblée sur le chemin général** | `pe_board_texture_id()` / bucketing utilisés dans les adapters river/turn spécialisés uniquement ; `mpf_infoset_key()` hache le board canonique exact |
| **Globaux dans le domaine** | `cfr_storage_set_strategy_mode()` (statique global) et 7 variables thread-local dans `cfr_core.c` |
| **I/O dans le domaine** | `fprintf(stderr, ...)` en plein traversal ; `checkpoint_path` / `resume_path` dans `cfr_config_t` |

### 1.3 Point de vocabulaire

La privacy des infosets est **déjà correcte** sur le chemin MPF : `mpf_pattern_hash()` ne
hache que `board_mask | hole[joueur_actif]`. Ce n'est pas un chantier de développement,
c'est un invariant à protéger par des tests.

---

## 2. Les cinq décisions structurantes

Elles définissent v3 et sont ce qui la sépare de v2.

### D1 — Deux lanes de traversal, un seul port

« Un solveur qui fait tout » n'est pas un moteur, c'est deux moteurs derrière un port commun.

| | **Lane A — vectoriel exact** | **Lane B — abstrait / échantillonné** |
|---|---|---|
| domaine d'emploi | postflop range vs range, 2 à 3 joueurs | multiway profond, préflop→river |
| référence | Pio, GTO+, TexasSolver | MonkerSolver, Pluribus |
| représentation | vecteur de valeurs contrefactuelles par combo | valeur scalaire par bucket |
| terminaux | showdown trié + retrait des blockers, `O(n log n)` par nœud | évaluation par bucket |
| abstraction | aucune (exacte) | strength buckets + texture filter |
| précision | exacte à la convergence | bornée par l'abstraction |
| GPU | **excellent** — vecteurs denses, opérations de type BLAS | limité — accès irréguliers, dominé par la RAM |
| sampling | non (FULL ou chance-sampled) | External / Outcome MCCFR |

Vouloir un moteur unique qui couvre les deux produit un moteur médiocre sur les deux.
L'hexagone les rend interchangeables : même port `pe_traversal_ops_t`, choix par preset,
combinaisons invalides refusées par le registry.

**Priorité : lane A d'abord.** C'est elle qui produit un solveur utilisable, elle qui rend
le GPU rentable, et elle qui donne l'oracle exact contre lequel lane B se mesure.

### D2 — Les ranges privées sont le prérequis absolu

Tant que le modèle porte `hole[p]` fixe, ni CFR+, ni DCFR, ni GPU ne produisent quoi que
ce soit d'utile : on résout un matchup de mains connues, pas un jeu à information
imparfaite. Les ranges viennent **avant** toute optimisation.

### D3 — Le best-response à information imparfaite doit devenir public et passer à l'échelle

Sans lui, il n'existe aucun critère d'arrêt fondé et aucune mesure de qualité. C'est le
différenciant principal face à MonkerSolver, dont le pont MCP documente que
`target_exploitability` reste non supporté faute de métrique identifiée.

**Le dépôt en possède déjà un.** `cfr_best_response_value_infoset()` sélectionne une action
par infoset, pondère par les reach contrefactuels et itère jusqu'au point fixe ; il gère la
chance et le multiway, et il est correct. Le travail n'est donc pas de l'écrire mais de
lever ses trois limites :

1. il est `static` — l'API publique expose encore le BR à information parfaite ;
2. sa table d'infosets est balayée linéairement à chaque insertion, et l'arbre est
   reparcouru à chaque tour du point fixe : c'est un outil d'audit dimensionné pour Kuhn et
   Leduc, pas un moteur de production ;
3. il rend une valeur brute, sans conversion en mbb/g ni critère d'arrêt.

En forme vectorielle (lane A), la même sélection par infoset devient une opération sur les
vecteurs de reach, ce qui règle le point 2 par construction.

### D4 — Les ports se franchissent par lots, jamais par nœud

L'hexagone en C coûte un appel indirect par franchissement. Sur un hot path appelé des
milliards de fois, c'est rédhibitoire. La discipline : `apply_update_batch(100 000 infosets)`,
jamais `update_one(node)`. C'est la même contrainte qui rend le GPU rentable — un seul
choix de conception résout les deux problèmes.

### D5 — MonkerSolver sert d'oracle et de spécification, pas de modèle algorithmique

La rétro-ingénierie disponible couvre la **surface de pilotage** (formats, grammaires,
réglages, mesures). Les workers CFR (`c.a`, `c.bk`, `c.bl`, `c.bm`, 155 méthodes) ne sont
pas désassemblés et n'ont pas à l'être. Ce qui est réutilisable : les formats `.tree` et
`.mkr`, la grammaire de filtres, la notation PPT, et des mesures chiffrées qui servent de
cibles. Voir §11.

---

## 3. L'architecture hexagonale

```text
              ADAPTATEURS PRIMAIRES  (driving / gauche)
   CLI   ·   API C + FFI   ·   serveur MCP   ·   bindings Python
   importeurs MonkerSolver (.tree / .mkr)   ·   harnais de tests
                            │
        ┌───────────────────┴────────────────────────────────┐
        │  PORTS PRIMAIRES                                   │
        │    pe_solver_port      cycle de vie du solve        │
        │    pe_equity_port      équité enum / MC / ranges    │
        │    pe_tree_port        construction d'arbre         │
        │    pe_range_port       parsing de ranges            │
        │    pe_abstraction_port entraînement des buckets     │
        ├────────────────────────────────────────────────────┤
        │                                                    │
        │                    D O M A I N E                   │
        │            C pur — aucune I/O, aucun global        │
        │                                                    │
        │   Rules          Tree + Abstraction    CFR engine  │
        │   (par variante) (buckets, texture,    traversal   │
        │                   isomorphisme)        regret      │
        │   Equity core                          averaging   │
        │   (existant)     Storage (dense-ID)    pruning     │
        │                                        plan/registry│
        │                                                    │
        ├────────────────────────────────────────────────────┤
        │  PORTS SECONDAIRES                                 │
        │    pe_compute_port     lots stratégie / update      │
        │    pe_evaluator_port   évaluation de mains par lot  │
        │    pe_storage_port     RAM / mmap / quantifié       │
        │    pe_persist_port     checkpoint, .pe_sol, .mkr    │
        │    pe_telemetry_port   métriques, progression, logs │
        └───────────────────┬────────────────────────────────┘
                            │
              ADAPTATEURS SECONDAIRES  (driven / droite)
   CPU-ref  ·  CPU-parallel (OpenMP)  ·  CUDA  ·  OpenCL
   evaluator SIMD existant  ·  mmap  ·  système de fichiers
```

### 3.1 Les trois règles qui font tenir l'hexagone

1. **Le domaine ne connaît ni CUDA, ni OpenCL, ni le disque, ni `stderr`.** Tout ce qui
   sort passe par `pe_telemetry_port` ou `pe_persist_port`. Aujourd'hui violé (§1.2).
2. **Le backend ne possède jamais la boucle de solve.** Il expose des opérations de lot et
   se fait appeler. `gpu_cfr_solve()` viole ce principe frontalement, d'où sa dépréciation.
3. **Zéro état global dans le domaine.** Tout état vit dans un contexte explicite passé en
   paramètre. Sans cela, ni parallélisme ni ports ne sont possibles.

### 3.2 Ce que l'hexagone achète concrètement

- **Ajouter une variante** = implémenter `pe_game_rules_t`. Aucune modification du CFR.
- **Ajouter un backend** = implémenter `pe_compute_ops_t`. Aucune modification du domaine.
- **Ajouter un algorithme** = fournir `pe_regret_ops_t` + `pe_average_ops_t`, et déclarer
  la combinaison au registry.
- **Tester le domaine sans GPU, sans disque, sans threads** : un backend `CPU-ref`
  déterministe suffit, et c'est lui l'oracle de parité.

---

## 4. Les ports

Signatures indicatives : elles fixent le contrat et la granularité, pas le détail final.

### 4.1 `pe_solver_port` — primaire

```c
typedef struct pe_solver_t pe_solver_t;

pe_solver_t *pe_solver_create(const pe_solver_config_t *cfg,
                              const pe_solver_deps_t *deps);   /* injection des ports */
int   pe_solver_validate(const pe_solver_t *s, pe_diagnostics_t *out);
int   pe_solver_capabilities(const pe_solver_t *s, uint64_t *out_caps);
int   pe_solver_estimate(const pe_solver_t *s, pe_estimate_t *out); /* RAM, VRAM, durée */
int   pe_solver_plan(const pe_solver_t *s, pe_execution_plan_t *out);
int   pe_solver_run(pe_solver_t *s);
int   pe_solver_pause(pe_solver_t *s);
int   pe_solver_resume(pe_solver_t *s);
int   pe_solver_stop(pe_solver_t *s);
int   pe_solver_progress(const pe_solver_t *s, pe_progress_t *out);
int   pe_solver_strategy(const pe_solver_t *s, pe_strategy_query_t q, pe_strategy_view_t *out);
int   pe_solver_metrics(const pe_solver_t *s, pe_metrics_t *out);
int   pe_solver_save(const pe_solver_t *s, pe_persist_target_t t);
int   pe_solver_load(pe_solver_t *s, pe_persist_source_t src);
void  pe_solver_destroy(pe_solver_t *s);
```

`pe_solver_deps_t` porte les ports secondaires injectés. C'est le point d'inversion de
dépendance : le domaine reçoit ses adaptateurs, il ne les choisit pas.

`pe_cfr_*` et `cfr_solve()` restent comme adaptateurs de compatibilité.

### 4.2 `pe_game_rules_t` — port de variante (domaine → règles)

Le point d'extension qui rend « tous les jeux connus » réaliste.

```c
typedef struct {
    const char *name;
    enum_game_t equity_game;          /* raccord au moteur d'équité existant */

    int  num_hole_cards;
    int  num_board_cards;
    int  num_streets;
    int  has_low;
    int  is_draw;                     /* les jeux de tirage ont un chance privé par street */

    /* Structure de mise */
    pe_betting_limit_t limit;         /* NO_LIMIT | POT_LIMIT | FIXED_LIMIT */

    /* Chance */
    int  (*chance_kind)(const pe_state_t *st);   /* NONE|PRIVATE|FLOP_THREE|BOARD_ONE|DRAW */
    int  (*chance_outcomes)(const pe_state_t *st);
    void (*chance_apply)(const pe_state_t *in, int outcome, pe_state_t *out);
    int  (*chance_sample)(const pe_state_t *st, pe_rng_t *rng, pe_chance_sample_t *out);

    /* Transitions */
    int  (*legal_actions)(const pe_state_t *st, pe_action_t *out, int max);
    void (*apply_action)(const pe_state_t *in, pe_action_t a, pe_state_t *out);
    int  (*is_terminal)(const pe_state_t *st);

    /* Terminaux : forme vectorielle obligatoire pour la lane A */
    int  (*terminal_payoff_vector)(const pe_state_t *st,
                                   const pe_reach_vec_t *opp_reach,
                                   pe_value_vec_t *out_values);
    int  (*terminal_payoff_scalar)(const pe_state_t *st, int player, double *out);

    /* Identité d'infoset : jamais d'information privée adverse */
    uint64_t (*infoset_key)(const pe_state_t *st, const pe_abstraction_t *abs);
} pe_game_rules_t;
```

Le moteur d'équité couvre 49 variantes ; les règles de mise et de chance n'en couvriront
pas 49 dans la première livraison. La matrice de capabilities (§5) dit lesquelles et à quel
niveau, plutôt que de laisser croire à une couverture uniforme.

### 4.3 `pe_traversal_ops_t` — port des lanes

```c
typedef struct {
    const char *name;                       /* full_vector, chance_vector, external, outcome */
    uint64_t    required_caps;
    int  (*begin_iteration)(pe_traversal_ctx_t *ctx, uint64_t iter);
    int  (*run_iteration)(pe_traversal_ctx_t *ctx, pe_update_batch_t *out_batch);
    int  (*end_iteration)(pe_traversal_ctx_t *ctx, uint64_t iter);
} pe_traversal_ops_t;
```

Une traversée **produit des deltas de regret bruts** dans un lot ; elle ne décide jamais
comment ils s'accumulent. Aucun `if (enable_dcfr)` dans une traversée.

### 4.4 `pe_regret_ops_t` / `pe_average_ops_t` / `pe_pruning_ops_t`

```c
typedef struct {
    const char *name;                       /* vanilla, plus, dcfr, legacy_exp */
    void (*begin_iteration)(pe_regret_ctx_t *c, uint64_t t);
    void (*compute_strategy)(pe_regret_ctx_t *c, const pe_infoset_range_t *r,
                             pe_strategy_span_t out);
    void (*apply_delta_batch)(pe_regret_ctx_t *c, const pe_update_batch_t *b);
    void (*end_iteration)(pe_regret_ctx_t *c, uint64_t t);   /* discount ici, une fois */
} pe_regret_ops_t;
```

`end_iteration` est l'endroit — le seul — où s'applique un discount DCFR. C'est ce qui
corrige le défaut du `d^N` actuel par construction, pas par vigilance.

### 4.5 `pe_compute_ops_t` — port de calcul (secondaire)

```c
typedef struct {
    const char *name;                       /* cpu_ref, cpu_par, cuda, opencl */
    uint64_t (*capabilities)(void *self);
    int  (*create)(void **self, const pe_compute_config_t *cfg);
    void (*destroy)(void *self);

    int  (*strategy_batch)(void *self, const pe_infoset_batch_t *in, pe_strategy_batch_t *out);
    int  (*apply_update_batch)(void *self, const pe_update_batch_t *b);
    int  (*terminal_eval_batch)(void *self, const pe_terminal_batch_t *in, pe_value_batch_t *out);
    int  (*vector_showdown)(void *self, const pe_showdown_job_t *j, pe_value_vec_t *out);
    int  (*sync)(void *self);
} pe_compute_ops_t;
```

`vector_showdown` est l'opération clé de la lane A : elle encapsule le showdown trié avec
retrait des blockers. C'est l'unité qui sera portée sur GPU en premier après l'évaluation
terminale.

### 4.6 `pe_evaluator_port`, `pe_storage_port`, `pe_persist_port`, `pe_telemetry_port`

- `pe_evaluator_port` enveloppe le moteur d'équité et l'évaluation GPU existants. Le
  domaine ne connaît ni `EvalContext`, ni CUDA.
- `pe_storage_port` abstrait RAM / mmap / quantifié. Permet de brancher
  `mpf_compact_storage` sans que le CFR sache qu'il existe.
- `pe_persist_port` couvre checkpoint v2, `.pe_sol`, `.pe_tree`, et les formats Monker en
  lecture.
- `pe_telemetry_port` remplace tout `fprintf` du domaine.

---

## 5. Configuration, registry et plan d'exécution

Repris de v2, qui avait raison sur ce point. Axes indépendants, presets en surface,
validation par matrice, plan résolu immuable.

```c
typedef enum {
    PE_TRAVERSAL_FULL_VECTOR = 0,   /* lane A, exhaustif                     */
    PE_TRAVERSAL_CHANCE_VECTOR,     /* lane A, chance échantillonnée          */
    PE_TRAVERSAL_FULL_SCALAR,       /* oracle de référence, jeux jouets       */
    PE_TRAVERSAL_EXTERNAL_SAMPLING, /* lane B                                 */
    PE_TRAVERSAL_OUTCOME_SAMPLING   /* lane B                                 */
} pe_traversal_mode_t;

typedef enum { PE_REGRET_VANILLA=0, PE_REGRET_PLUS, PE_REGRET_DCFR, PE_REGRET_LEGACY_EXP }
        pe_regret_mode_t;
typedef enum { PE_POLICY_REGRET_MATCHING=0, PE_POLICY_EXPONENTIAL } pe_policy_mode_t;
typedef enum { PE_AVG_UNIFORM=0, PE_AVG_LINEAR, PE_AVG_POWER, PE_AVG_DELAYED_LINEAR }
        pe_averaging_mode_t;
typedef enum { PE_PRUNE_NONE=0, PE_PRUNE_RBP } pe_pruning_mode_t;
typedef enum { PE_PREC_F64=0, PE_PREC_F32, PE_PREC_MIXED, PE_PREC_FIXED16 }
        pe_precision_mode_t;

typedef struct {
    pe_compute_kind_t traversal;    /* AUTO|CPU_REF|CPU_PAR|CUDA|OPENCL */
    pe_compute_kind_t update;
    pe_compute_kind_t terminal_eval;
} pe_stage_backends_t;
```

`PE_PREC_FIXED16` est ajouté par rapport à v2 : c'est le mode de la lane B à grande
échelle, et c'est ce que fait MonkerSolver (`c.bQ` : `int[][][]` de regrets +
`long[][][]` cumulés ; `c.bR` : variante compacte).

**Capabilities** (matrice de validation) :

```text
VECTOR_FORM          PRIVATE_RANGES        FLOP_CHANCE          DRAW_CHANCE
MULTIWAY             ZERO_SUM_GUARANTEE    NON_ZERO_SUM         NONLINEAR_UTILITY
ENUMERATED_CHANCE    DIRECT_CHANCE_SAMPLING
ABSTRACTION          SUIT_ISOMORPHISM      BATCH_UPDATES        CPU_PARALLEL
GPU_TERMINAL_EVAL    GPU_VECTOR_SHOWDOWN   GPU_REGRET_UPDATE    GPU_TRAVERSAL
RBP                  LOCKED_STRATEGY       PERIODIC_RELOCK      SUBGAME_RESOLVE
CHECKPOINT           DETERMINISTIC         IMPERFECT_INFO_BR
```

Le resolver rend `ERROR` / `WARNING` / `FALLBACK` puis un plan résolu, exposé à
l'utilisateur (`--print-execution-plan`). Une combinaison non validée est **refusée**, pas
silencieusement dégradée.

### Matrice d'algorithmes initiale

| Preset | Lane | Traversal | Regret | Average | Statut cible |
|---|---|---|---|---|---|
| `cfr` | — | FULL_SCALAR | VANILLA | UNIFORM | oracle de référence, jeux jouets |
| `cfr-vector` | A | FULL_VECTOR | VANILLA | UNIFORM | référence postflop |
| `cfr+` | A | FULL_VECTOR | PLUS | DELAYED_LINEAR | **cible principale** |
| `dcfr` | A | FULL_VECTOR | DCFR | POWER | prioritaire, après oracle |
| `cfr+-chance` | A | CHANCE_VECTOR | PLUS | DELAYED_LINEAR | turn/river multi-runout |
| `external-mccfr` | B | EXTERNAL | VANILLA | validé sampling | multiway |
| `external-dcfr` | B | EXTERNAL | DCFR | POWER | après validation d'External |
| `outcome-mccfr` | B | OUTCOME | VANILLA | OS-aware | après External |
| `ecfr` | — | * | LEGACY_EXP | configurable | expérimental, préserve l'existant |

---

## 6. Storage

Deux niveaux, un seul port.

**Métadonnées et identité :**

```c
typedef uint32_t pe_infoset_id_t;

typedef struct {
    uint64_t key;
    uint32_t action_offset;      /* index dans les tableaux ragged */
    uint32_t combo_offset;       /* lane A : index dans les tableaux par combo */
    uint16_t action_count;
    uint16_t combo_count;        /* lane A ; 1 en lane B */
    int8_t   street;
    uint8_t  flags;              /* locked, abstracted, pruned */
} pe_infoset_meta_t;
```

Hachage `key -> id`, puis tableaux SoA/ragged. La lane A indexe
`[infoset][action][combo]` ; la lane B dégénère à `combo_count == 1`, donc **une seule
disposition mémoire pour les deux lanes**.

```text
regret[total_slots]            F64 | F32 | fixed16
avg_strategy[total_slots]
current_strategy[total_slots]  optionnel
locked_strategy[total_slots]   optionnel
```

Résidence : `HOST_CANONICAL`, `DEVICE_RESIDENT`, `MIRRORED_DEBUG`.

**Le budget mémoire est une entrée de configuration, pas une conséquence.**
`pe_solver_estimate()` doit répondre avant tout solve, et `pe_solver_validate()` refuse un
plan qui dépasse le budget déclaré. C'est la contrainte n°1 d'un solveur réel, et v2 ne la
traitait pas.

Migration : storage historique → adaptateur d'IDs denses → storage v2 → buffers device.
`mpf_compact_storage` devient un adaptateur de `pe_storage_port`.

---

## 7. Modèle de jeu, ranges et chance

### 7.1 Ranges privées

```c
typedef struct { mask_t cards; double weight; } pe_hand_combo_t;
typedef struct { const pe_hand_combo_t *combos; size_t count; } pe_player_range_t;
```

Chaque joueur porte une range, pas une main. Le vecteur de reach d'un joueur est indexé
par combo. Le retrait des cartes (blockers) est appliqué **au moment de l'évaluation
terminale**, jamais dans la clé d'infoset.

### 7.2 Types de chance

```text
NONE            PRIVATE_HANDS       FLOP_THREE      BOARD_ONE       DRAW_N
```

Le cache `chance_children[52]` existant ne reste valide que pour `BOARD_ONE`.
`FLOP_THREE` énumère des **combinaisons** de 3 cartes, jamais des permutations
séquentielles. `DRAW_N` ouvre les variantes de tirage (lowball, triple draw, badugi).

Flux complet :

```text
PRIVATE_HANDS → préflop → FLOP_THREE → flop → BOARD_ONE → turn → BOARD_ONE → river
```

### 7.3 Isomorphisme de couleurs

`board_canonical.c` existe et n'est pas exploité comme réduction d'espace. Sur un solve
flop, l'isomorphisme réduit 22 100 flops à 1 755 classes — c'est un facteur ~12,6 gratuit,
et c'est exactement le balayage que le pont MonkerSolver décrit. Capability
`SUIT_ISOMORPHISM`.

### 7.4 Abstraction (lane B)

Les briques existent : `strength_bucketing` (k-means EHS/EHS²), `board_texture` (5
niveaux), `hand_clustering`. Le travail est de les **câbler dans `infoset_key()` du chemin
général**, pas de les écrire. Le port `pe_abstraction_port` expose entraînement,
sérialisation `.pe_sbk` et réutilisation entre solves.

---

## 8. Garanties, exploitabilité et honnêteté des résultats

### 8.1 Best-response à information imparfaite

L'algorithme existe : `cfr_best_response_value_infoset()` maximise **par infoset**, pas par
état, pondère par les reach contrefactuels et itère jusqu'au point fixe. v3 le promeut au
lieu de le réécrire.

Trois changements. Il devient public. Il passe en forme vectorielle : pour chaque infoset
du joueur BR, la valeur de chaque action est la somme sur les combos pondérée par le reach
contrefactuel adverse — ce qui remplace du même coup son balayage linéaire de la table
d'infosets et le reparcours complet de l'arbre à chaque tour. Et il devient la source de
l'exploitabilité publique.

`best_response_recursive()` reste disponible sous le nom
`cfr_best_response_perfect_info()`, avec sa sémantique documentée de borne supérieure. Les
deux ne sont pas interchangeables : le BR à information parfaite majore toujours celui par
infoset, et une assertion d'égalité entre les deux ne tient que par coïncidence — c'est ce
qui a fait échouer `test_best_response_exploitability` quand `EXT-07` a changé la stratégie
produite.

Sortie : exploitabilité en **mbb/g** (milli-big-blinds par main), l'unité que les
utilisateurs attendent, et critère d'arrêt `target_exploitability` réellement supporté.

### 8.2 Multiway et non-zéro-somme

2 joueurs zéro-somme : Nash, exploitabilité standard.
Multiway ou non-zéro-somme : exposer `NO_REGRET_ONLY` ou `EMPIRICAL`, avec BR gaps, CCE
gap et déséquilibre d'utilité. **Ne jamais présenter automatiquement la stratégie comme
« Nash » ou « GTO »** dans ces cas.

---

## 9. Backends : CPU et GPU

### 9.1 CPU

**`cpu_ref`** : un thread, F64, batch = 1, ordre de parcours stable, seed déterministe.
C'est l'oracle de parité de tous les autres backends. Il ne doit jamais être supprimé.

**`cpu_par`** : trajectoires thread-local → accumulateur de deltas thread-local → réduction
déterministe → mise à jour du storage. **Pas d'atomiques directes sur les regrets** dans la
première implémentation : la reproductibilité passe avant le débit.

### 9.2 Niveaux de maturité GPU

```text
GPU-0   noyaux et utilitaires existants (eval_batched_*)          — acquis
GPU-1   évaluation terminale par lot                              — premier gain, faible risque
GPU-2   showdown vectoriel (tri + retrait des blockers)           — le gain décisif de la lane A
GPU-3   noyaux stratégie / regret par lot                         — vecteurs denses
GPU-4   lots d'update complets, storage résident device           — après parité GPU-3
GPU-5   MCCFR hybride (sampler CPU, terminaux + réduction GPU)    — lane B
GPU-6   traversée packed / flat-tree                              — dernier, si justifié
```

Chaque niveau a une **gate de parité** contre `cpu_ref` avant d'être sélectionnable par
`AUTO`. Un niveau peut être fusionné derrière un flag de capability désactivé.

`AUTO` initial : traversal `cpu_par`, update `cpu_par`, terminal_eval `CUDA → OpenCL → CPU`.

`gpu_cfr_solve()` et `gpu_cfr_adapter.c` deviennent legacy et sortent du chemin principal.
Les noyaux réutilisables sont conservés.

---

## 10. Validation

Ordre obligatoire. Une étape ne s'ouvre que si la précédente est verte.

```text
V0  jeux jouets analytiques        AKQ, jam-or-fold, matrices  — existant
V1  Kuhn 2p / 3p / 4p              vs fixtures OpenSpiel        — existant
V2  Leduc                          vs énumération complète      — existant
V3  LP séquentiel exact            vs simplex interne           — existant
V4  BR à information imparfaite    exploitabilité → 0 sur équilibre exact,
                                   et parité avec le BR par infoset existant
V5  lane A vs lane scalaire        même jeu, même valeur, tolérance serrée  — NOUVEAU
V6  Hold'em river range vs range   vs énumération exhaustive
V7  Hold'em turn, puis flop
V8  PLO4 / PLO5 / PLO6
V9  multiway 3+
V10 parité cpu_ref / cpu_par / CUDA / OpenCL
V11 parité F64 / mixed / F32 / fixed16
V12 interop MonkerSolver           stratégie combo par combo vs .mkr        — NOUVEAU
```

**V4, V5 et V12 sont les ajouts de v3**, et ce sont les trois qui manquaient réellement :
sans V4 aucune mesure de qualité, sans V5 aucune preuve que le vectoriel est exact, sans
V12 aucun contrôle à l'échelle réelle.

---

## 11. Interopérabilité MonkerSolver

Justification : entre « Leduc converge » et « mon solve PLO 4-max est juste », le dépôt
n'a aujourd'hui **aucun contrôle**. MonkerSolver fournit le seul oracle externe disponible
à l'échelle du poker réel.

### 11.1 Formats — lecture d'abord

**`.tree` (signature 33487 / 33486, lecteur 2.3.10 : 33486–33490)** :

```text
int64  signature
int32  format interne
int32  joueurs · int32 premier à agir · int32 street (0 = préflop)
int32  committed par joueur          (uniquement si street == 0)
int32  dead money
int32  stacks par joueur
       flux récursif de nœuds : char action, char nombre d'enfants
bool   présence des ranges
int32  ranges : joueurs × 270 725 combos Omaha (1 326 en Hold'em),
       virgule fixe, diviseur 2 147 483 647
```

Deux conséquences : le format **n'encode ni board ni committed postflop**, et les ranges y
sont directement exploitables comme entrée de `pe_range_port`.

**`.mkr`** : conteneur ZIP (noms d'entrées en **UTF-16BE avec BOM** — particularité du
writer Java), entrées `storedstrategyN` en zlib puis sérialisation Java, plus des scalaires
(`iterations`, `flopBuckets`, `rakepercent`, `rakecap`, `rakeflags`, `evs`, `conv`…).
`storedstrategy0` contient les fréquences en virgule fixe sur 2 octets par classe de main.

Priorité : **lecture uniquement**. L'écriture n'a aucun besoin identifié.

### 11.2 Grammaire de filtres de nœuds

36 mots-clés, grammaire confrontée à 7 370 chaînes contre le parseur réel, zéro désaccord.

```text
filtre  := ou
ou      := et ("," et)*
et      := atome (":" atome)*
atome   := "!"? "^"? mot_clé (opérateur valeur)?
         | "!"? "(" ou ")"
opérateur := ">" | "<" | "=" | ">=" | "<="
```

Le MPF actuel en réimplémente 6 règles (`$cb`, `SPR>`, `SPR<`, `POS=`, `BET`, `AUTO`).
Adopter la grammaire complète donne un langage de construction d'arbre éprouvé **et**
l'interop.

Trois pièges mesurés à reprendre tels quels :

- `pot` s'exprime en **petites blindes**, pas en jetons (le prédicat divise par 1000) ;
- **ni `pot` ni `spr` ne comptent le dead money** ;
- `^` est un préfixe de mot-clé (`^street=1` compile, `street=^1` non), et l'ordre des
  préfixes est fixe (`!^pre` compile, `^!pre` non).

### 11.3 Notation PPT pour les ranges

Grammaire établie et vérifiée par comptage : jetons portant un rang optionnel (`2`–`A`, ou
`r` variable) et une couleur optionnelle (`s`/`h`/`d`/`c`, ou `x`/`y`/`z` variables) ; une
main correspond s'il existe une affectation **injective** des jetons à ses cartes, variables
liées de façon cohérente, deux variables distinctes prenant des valeurs distinctes.
Opérateurs : `,` (OU), `:` (ET), `!` (différence binaire, pas une négation unaire),
`+` (cette paire ou mieux), `( )`.

À ajouter comme dialecte de `pe_range_port` à côté de la syntaxe native
(`AdvancedRangeParser`).

### 11.4 Ce qu'on ne reprend pas

Les workers CFR de MonkerSolver ne sont pas désassemblés et n'ont pas à l'être. v3 ne
copie **aucun algorithme**.

---

## 12. Cibles de performance

Mesures MonkerSolver 2.1.9 relevées sur poste réel (macOS, `phys_footprint` noyau).
Elles servent de référence, pas de plafond.

| Configuration | Monker | Cible poker-eval v3 |
|---|---|---|
| Tree postflop HU, 243 nœuds, PLO, 30 buckets/street | **13,2 Go** d'empreinte, tas 11,25 Go | lane A exacte, **< 2 Go**, sans abstraction |
| Pré-calcul flop, 30 buckets | 28,8 s | < 10 s (abstraction réutilisable, `.pe_sbk`) |
| Pré-calcul flop, 40 buckets | 53,9 s (+87 %, non linéaire) | croissance linéaire attendue |
| Première itération CFR Omaha (AoF flop) | 20 s | < 5 s |
| Biais de nodelock à la convergence | **−1,4 % systématique** | **exact** (lock strict déjà implémenté) |
| Latence de retrait d'un lock | **309 s** | immédiat |
| Critère d'arrêt sur exploitabilité | **non supporté** | mbb/g, supporté |
| Runs simultanés par processus | 1 | N |

Les trois dernières lignes sont les différenciants réels : ce ne sont pas des gains de
vitesse, ce sont des capacités que le produit de référence n'a pas.

---

## 13. Métriques et checkpoints

**Métriques** — le plan résolu doit être observable, pas seulement configuré : itérations,
échantillons, nœuds, chance samples, infosets, actions, évaluations terminales, lots,
updates fusionnés, nœuds/s, exploitabilité (mbb/g), BR gaps, CCE gap, RAM, VRAM,
transferts, temps par noyau, **backend et précision effectivement résolus par étage**.

**Checkpoints v2** — indépendants du backend : version, config résolue, hash du jeu et de
l'arbre, infosets, regrets, moyennes, locks, itération, version et état du RNG, précision
numérique portable. Les checkpoints v1 restent lisibles tant que la couverture v2 n'est pas
complète.

---

## 14. Migration

Sans big-bang. Chaque jalon est mergeable et laisse le dépôt vert.

| Jalon | Contenu | Gate |
|---|---|---|
| **M0** | Contrats : en-têtes de ports, registry, plan, capabilities. Aucun changement de comportement. | build + tests existants verts |
| **M1** | Extraction : `cfr_solve` découpé en traversal / regret / average. Discount déplacé dans `end_iteration` (corrige `d^N`). Suppression des globaux et des `fprintf` du domaine. | V0–V3 inchangés au bit près |
| **M2** | Storage dense-ID ragged, adaptateur sur l'existant. | V0–V3 verts, pas de régression mémoire |
| **M3** | **Ranges privées + root private chance + FLOP_THREE + isomorphisme.** | nouveaux tests de chance et de privacy |
| **M4** | **Lane A : CFR vectoriel + showdown trié avec blockers.** | **V5** : lane A == lane scalaire |
| **M5** | **BR à information imparfaite, exploitabilité mbb/g.** | **V4** |
| **M6** | CFR+ et DCFR canoniques comme `pe_regret_ops_t`. | V0–V3 + oracles de convergence |
| **M7** | `cpu_par` déterministe. | parité bit-à-bit avec `cpu_ref` |
| **M8** | Interop Monker : lecteurs `.tree` / `.mkr`, grammaire de filtres, PPT. | **V12** |
| **M9** | GPU-1 puis GPU-2 (évaluation terminale, showdown vectoriel). | gates de parité |
| **M10** | Abstraction câblée sur le chemin général + lane B (External MCCFR). | V6–V9 |
| **M11** | GPU-3/GPU-4, RBP, Outcome MCCFR. | gates de parité |
| **M12** | API C stable, CLI, checkpoint v2, dépréciation de `gpu_cfr_*`. | V10–V11 |

**Ordre non négociable : M3 → M4 → M5.** Les ranges avant le vectoriel, le vectoriel avant
la mesure, la mesure avant toute optimisation. Optimiser avant M5, c'est optimiser sans
savoir si le résultat est juste.

---

## 15. Arborescence cible

```text
include/poker_eval/solver/
  pe_solver.h            pe_solver_config.h     pe_solver_plan.h
  pe_capabilities.h      pe_ports.h             pe_game_rules.h
  pe_traversal.h         pe_regret.h            pe_average.h      pe_pruning.h
  pe_storage.h           pe_batch.h             pe_compute.h
  pe_range.h             pe_abstraction.h       pe_telemetry.h
  pe_rng.h

src/solver/domain/
  solver.c               config.c               plan.c            registry.c
  rng.c                  storage_v2.c           batch.c
  traversal_full_scalar.c
  traversal_full_vector.c        traversal_chance_vector.c
  traversal_external.c           traversal_outcome.c
  regret_vanilla.c  regret_plus.c  regret_dcfr.c  regret_legacy_exp.c
  average.c              pruning_rbp.c
  best_response_ii.c     showdown_vector.c      blockers.c
  abstraction.c          isomorphism.c

src/solver/rules/
  rules_holdem.c  rules_plo.c  rules_shortdeck.c  rules_stud.c  rules_draw.c

src/solver/adapters/
  compute_cpu_ref.c      compute_cpu_par.c
  compute_cuda.c         compute_opencl.c
  evaluator_equity.c     evaluator_gpu.c
  storage_ram.c          storage_compact.c
  persist_checkpoint.c   persist_pe_sol.c
  telemetry_stderr.c     telemetry_callback.c
  monker_tree_reader.c   monker_mkr_reader.c
  monker_filters.c       range_ppt.c

src/gpu/
  eval_batched_*          (réutilisé tel quel)
  kernels_showdown_*      kernels_regret_*
```

L'ancien `src/engine/solvers/cfr/` reste en place pendant la migration et devient une
couche de compatibilité.

---

## 16. Definition of Done

L'architecture v3 est atteinte quand :

- [ ] les presets principaux passent par le registry, et une combinaison invalide est refusée
- [ ] le domaine ne contient ni global mutable, ni I/O, ni référence à CUDA/OpenCL
- [ ] `cpu_ref` est déterministe et sert d'oracle de parité à tous les backends
- [ ] les ranges privées, le chance flop et l'isomorphisme de couleurs fonctionnent
- [ ] la lane A donne la même valeur que la lane scalaire sur les jeux de référence
- [ ] l'exploitabilité est mesurée à information imparfaite, en mbb/g, et sert de critère d'arrêt
- [ ] CFR+ et DCFR canoniques sont des `pe_regret_ops_t` séparés, sans branche dans les traversées
- [ ] `pe_solver_estimate()` répond avant tout solve et `validate()` refuse un dépassement de budget
- [ ] une variante s'ajoute en implémentant `pe_game_rules_t`, sans toucher au CFR
- [ ] un `.tree` MonkerSolver se charge et un `.mkr` se compare combo par combo
- [ ] GPU-1 et GPU-2 sont derrière une gate de parité vérifiée
- [ ] les checkpoints sont indépendants du backend
- [ ] les métriques exposent le plan réellement exécuté
- [ ] `gpu_cfr_solve()` n'est requis par aucun chemin principal

---

## 17. Hors périmètre, et pourquoi

**Résoudre les 49 variantes à la même profondeur.** Le moteur d'équité les couvre ; le
solveur ne les couvrira pas toutes de la même manière. Un stud 8 joueurs sur 5 streets
n'est pas résoluble, avec ou sans GPU. La matrice de capabilities doit le dire par variante
plutôt que de laisser croire à une couverture uniforme.

**Le CFR profond / réseaux de neurones.** Deep CFR et ReBeL sont une autre architecture
(modèle appris, rejeu, inférence). Ils pourraient devenir une lane C ultérieure derrière le
même port de traversal, mais rien dans v3 ne les prépare et rien ne doit être compromis
pour eux.

**Réécrire l'évaluation de mains.** Elle est mature, testée et rapide. Elle est
enveloppée par un port, pas retouchée.

**Rétro-concevoir les workers CFR de MonkerSolver.** Aucun besoin identifié, coût élevé,
et v3 ne copie aucun algorithme.

---

## 18. Références

- Zinkevich, Johanson, Bowling, Piccione — *Regret Minimization in Games with Incomplete
  Information* (CFR).
- Lanctot, Waugh, Zinkevich, Bowling — *Monte Carlo Sampling for Regret Minimization in
  Extensive Games* (External / Outcome sampling).
- Tammelin — *Solving Large Imperfect Information Games Using CFR+*.
- Brown, Sandholm — *Solving Imperfect-Information Games via Discounted Regret
  Minimization* (DCFR).
- Burch, Johanson, Bowling — *Solving Imperfect Information Games Using Decomposition*
  (gadget CFR-D ; déjà implémenté dans `cfr_resolve.c`).
- Gilpin, Sandholm — abstraction EHS² (base de `strength_bucketing.c`).
- Johanson, Waugh, Bowling, Zinkevich — *Accelerating Best Response Calculation in Large
  Extensive Games* (best-response vectoriel).
- OpenSpiel — oracles externes Kuhn / Leduc, déjà intégrés en fixtures.
- Rétro-ingénierie MonkerSolver 2.1.9 : formats `.tree` / `.mkr`, grammaire de filtres,
  notation PPT, mesures d'abstraction et de nodelock.

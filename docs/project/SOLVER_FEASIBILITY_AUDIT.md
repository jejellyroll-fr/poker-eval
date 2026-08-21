# Audit de Faisabilité : Solver Toutes Variantes + GPU + Frontend

> **Date** : 2026-08-21
> **Auteur** : Audit automatisé par analyse statique exhaustive du code source
> **Méthode** : Lecture ligne-par-ligne des fichiers critiques, comptage de lignes,
> vérification des vtables CFR, inspection des kernels GPU, exécution de la suite
> de tests complète (202/202 passent, 8 GPU désactivés).

---

## Résumé Exécutif

**Le projet `poker-eval` est une bibliothèque d'évaluation de mains de poker
exceptionnellement complète (évaluateurs, ranges, equity, variantes, SIMD) mais
son solveur CFR et son accélération GPU sont à un stade précoce, avec des
lacunes structurelles majeures qui empêchent aujourd'hui de résoudre
"n'importe quelle variante avec GPU".**

Le diagnostic initial (score 8-9/10 sur le solver, 7/10 sur le GPU) était
**significativement surévalué**. Après analyse ligne par ligne, le score réel
se situe plutôt autour de :

| Composant | Score Initial | Score Corrigé | Verdict |
| :--- | :---: | :---: | :--- |
| Évaluateurs & Combinatoire | 9.5/10 | **9.5/10** | ✅ Confirmé excellent |
| Solver CFR Core | 8.5/10 | **5.5/10** | ⚠️ Vanilla CFR uniquement, pas de pruning |
| Adapters de variantes | 8/10 | **3.5/10** | ❌ River HU seulement, 0 adapter pour PLO5/6/Draw/Badugi |
| GPU Acceleration | 7/10 | **2.5/10** | ❌ Stubs avec `TODO` dans le code |
| API & Bindings CFR | 7/10 | **1/10** | ❌ `PE_ERROR_NOT_SUPPORTED` hardcodé |
| Pipeline End-to-End | 7/10 | **4/10** | ⚠️ Fonctionne sur des sous-jeux JSON prédéfinis |

---

## 1. Évaluateurs de Mains — ✅ CONFIRMÉ EXCELLENT

Le diagnostic initial est **entièrement confirmé**. La couche d'évaluation est
de calibre production :

- **15+ variantes** avec évaluateurs spécialisés (Hold'em, Omaha 4/5/6, Stud,
  Razz, Badugi, Drawmaha, Pineapple, Short Deck, Joker, OFC, Triple Draw,
  Lowball A-5, Lowball 2-7, Manila, Fusion).
- **SIMD vectorisé** (SSE/AVX/NEON) pour l'évaluation 7-card.
- **Tables préflop pré-calculées** et canonicalisation isomorphe des boards.
- **202 tests passent à 100%** en ~50 secondes.

> [!TIP]
> Cette couche est la vraie force du projet. Elle constitue une base solide pour
> tout solver futur.

---

## 2. Solver CFR Core — ⚠️ SURÉVALUÉ

### Ce qui est réel

- [`cfr_core.c`](file:///Users/jde/Documents/github/poker-eval/src/engine/solvers/cfr/cfr_core.c)
  (1920 lignes) : Implémente un **Vanilla CFR** fonctionnel avec support DCFR
  (Linear Discounting), traversée récursive, et best-response pour le calcul
  d'exploitabilité.
- [`cfr_storage.c`](file:///Users/jde/Documents/github/poker-eval/src/engine/solvers/cfr/cfr_storage.c)
  (1112 lignes) : Hash-map thread-safe pour les regrets et stratégies.
- [`cfr_resolve.c`](file:///Users/jde/Documents/github/poker-eval/src/engine/solvers/cfr/cfr_resolve.c)
  (1055 lignes) : Subgame re-solving CFR-D fonctionnel.
- [`mpf_compact_storage.c`](file:///Users/jde/Documents/github/poker-eval/src/engine/solvers/cfr/mpf_compact_storage.c)
  (1385 lignes) : Format `.pe_sol` avec quantification 16-bit et mmap.
- Validation contre **Kuhn Poker, Leduc, AKQ** via OpenSpiel et Gambit exact LP.

### Ce qui a été surévalué

| Affirmation initiale | Réalité du code |
| :--- | :--- |
| "Support MCCFR / External Sampling" | ❌ **Vanilla CFR uniquement**. Dans `cfr_traverse_recursive` (L803-804), chaque action est traversée systématiquement. Aux nœuds de chance (L652), toutes les issues sont énumérées exhaustivement. |
| "Pruning intelligent" | ❌ **Aucun pruning**. Ni Regret-Based Pruning (RBP), ni seuil d'élagage. Le solver traverse naïvement les branches à regret négatif. |
| "Supporte préflop-to-river" | ❌ **Postflop uniquement en pratique**. L'adapter principal s'appelle littéralement `multiway_postflop_adapter`. Sans MCCFR, résoudre depuis le préflop nécessiterait d'énumérer C(50,3) = 19 600 branches au flop × C(47,1) = 47 à la turn × C(46,1) = 46 à la river, soit ~42 millions de boards par action. |

### Impact concret

Sans MCCFR et sans pruning, le solver est limité à des **sous-jeux de taille
modeste** (river spots, petits arbres turn-to-river). Il ne peut pas résoudre
un arbre préflop-to-river Hold'em complet en temps raisonnable, encore moins
un arbre PLO.

---

## 3. Adapters de Variantes pour le CFR — ❌ TRÈS LARGEMENT SURÉVALUÉ

### Adapters réellement implémentés

| Adapter | Fichier | Lignes | Couverture |
| :--- | :--- | ---: | :--- |
| Hold'em River HU | `holdem_river_adapter.c` | 534 | ✅ Complet, vtable CFR, évaluation réelle |
| Omaha River HU | `omaha_river_adapter.c` | 388 | ✅ Complet |
| Omaha8 River HU | `omaha8_river_adapter.c` | 400 | ✅ Complet |
| Razz River HU | `razz_river_adapter.c` | 276 | ✅ Complet |
| Short Deck River HU | `shortdeck_river_adapter.c` | 322 | ✅ Complet |
| Stud River HU | `stud_river_adapter.c` | 267 | ✅ Complet |
| Stud8 River HU | `stud8_river_adapter.c` | 322 | ✅ Complet |
| Hold'em Multi River HU | `holdem_multi_river_adapter.c` | 250 | ✅ Multi-sizing |
| Hold'em Turn → River | `holdem_turn_adapter.c` | 143 | ⚠️ Générateur, pas un adapter : sample 1 river card → délègue au river adapter |
| Multiway Postflop | `multiway_postflop_adapter.c` | 2514 | ✅ Substantiel, multi-joueurs, mais nécessite un arbre JSON pré-écrit |

### Adapters qui N'EXISTENT PAS

> [!CAUTION]
> **Aucun adapter CFR n'existe pour les variantes suivantes** — une recherche
> exhaustive dans `src/engine/solvers/cfr/` ne retourne aucun résultat :

- **PLO5** (Omaha 5 cartes)
- **PLO6** (Omaha 6 cartes)
- **Badugi**
- **2-7 Triple Draw** / **A-5 Triple Draw**
- **Drawmaha**
- **Pineapple / Crazy Pineapple**
- **OFC (Open-Face Chinese)**
- **Manila**
- **Fusion**
- **Lowball** (A-5 / 2-7 single draw)

Le fichier `draw_abstraction.c` (78 lignes) fournit un bucketing structurel
basique pour Badugi et 2-7 Triple Draw, mais **aucune intégration dans un
game tree CFR**.

### Implications

Affirmer que le projet peut "résoudre toutes les variantes" est aujourd'hui
**factuellement faux**. Il peut résoudre des **spots river heads-up** dans 7
variantes, et des **sous-jeux multiway postflop** pour Hold'em/Omaha si l'on
fournit un arbre JSON manuellement.

---

## 4. Accélération GPU — ❌ DRASTIQUEMENT SURÉVALUÉ

### Ce qui est réel

- **Évaluation de mains en batch sur GPU** : Les kernels CUDA et OpenCL pour
  l'évaluation 5/7-card Hold'em et Omaha sont fonctionnels
  ([`eval_cuda_kernel.cu`](file:///Users/jde/Documents/github/poker-eval/src/gpu/cuda/eval_cuda_kernel.cu),
  356 lignes ; [`eval_omaha_kernel.cu`](file:///Users/jde/Documents/github/poker-eval/src/gpu/cuda/eval_omaha_kernel.cu),
  179 lignes).
- Les kernels low (`eval_low_kernel.cu`, `eval_low_kernel.cl`) sont complets.
- Les kernels OFC Monte Carlo existent (`ofc_monte_carlo.cu`, `ofc_monte_carlo.cl`).

### Ce qui est un STUB

> [!WARNING]
> **Le GPU CFR solver est un squelette avec des `TODO` dans le code source.**

**Preuve directe dans `gpu_cfr.c`** (lignes 271-278) :

```c
/* TODO: Implement actual GPU-CFR iterations */
```

**Preuve directe dans `gpu_cfr_cuda.cu`** (ligne 318) :

```c
/* 2. TODO: Compute regret deltas (game tree traversal) */
/* For now, use placeholder: random deltas */
launch_zero_array(ctx->d_deltas, matrix_size, ctx->stream);
```

Le système GPU CFR fait actuellement ceci :
1. ✅ Transfert CPU → GPU des matrices regret/stratégie (`gpu_cfr_load_state`)
2. ✅ Mise à jour des regrets sur GPU via AXPY (opérations matricielles)
3. ❌ **La traversée de l'arbre de jeu est absente** — les deltas sont mis à zéro
4. ✅ Transfert GPU → CPU (`gpu_cfr_download_state`)

Le fichier `gpu_cfr_adapter.c` (407 lignes) fait le pont : il traverse l'arbre
**sur CPU** et passe les deltas au GPU pour les mises à jour matricielles, mais
cela **n'accélère pas le goulot d'étranglement** qui est la traversée elle-même.

### Tests GPU

Les **8 tests GPU sont tous désactivés** (`***Not Run (Disabled)`) dans la
suite de tests. Ils ne valident aucune fonctionnalité GPU réelle. Les tests
OpenCL qui passent (`test_opencl_kernel_guards`, `test_opencl_low_parity`)
compilent les kernels sur le CPU hôte pour vérifier la parité, pas
l'exécution GPU.

---

## 5. API Bindings CFR — ❌ COMPLÈTEMENT STUB

L'API C stable (`bindings/c/poker_eval_api.c`) pour le solver CFR est
**entièrement non-implémentée** :

```c
// bindings/c/poker_eval_api.c, lignes 508-514
pe_error_t pe_cfr_solve(pe_cfr_handle_t cfr, int iterations) {
    if (!cfr || iterations <= 0) return PE_ERROR_INVALID_ARGUMENT;
    /* CFR solving requires full library integration */
    set_error(cfr->parent, "CFR solving requires linking with poker_engine library");
    return PE_ERROR_NOT_SUPPORTED;
}
```

Les fonctions `pe_cfr_get_strategy`, `pe_cfr_save`, `pe_cfr_load` et
`pe_cfr_get_exploitability` retournent toutes `PE_ERROR_NOT_SUPPORTED`.

---

## 6. Tree Builder — ⚠️ PARSEUR JSON, PAS GÉNÉRATEUR

Le fichier `mpf_tree.c` (3456 lignes) est un **désérialiseur JSON** utilisant
la bibliothèque `jsmn`. Il lit des arbres de jeu pré-écrits en JSON et les
transforme en structures C.

Il n'y a **aucun générateur automatique** qui prend en entrée une configuration
(stacks, blindes, sizings, nombre de joueurs, variante) et produit un arbre de
jeu complet. Chaque spot à résoudre nécessite un fichier JSON écrit à la main
ou généré par un outil externe.

---

## 7. Composants Fonctionnels et Solides

Pour être juste, voici ce qui fonctionne réellement bien :

| Composant | Fichier(s) | Verdict |
| :--- | :--- | :--- |
| Hand Clustering k-means | `hand_clustering.c` (1288 L) | ✅ Complet (Hold'em/Omaha) |
| Strength Bucketing | `strength_bucketing.c` (927 L) | ✅ Complet |
| Board Canonicalisation | `board_canonical.c` (140 L) | ✅ Isomorphisme de couleurs |
| Board Texture Analysis | `board_texture.c` (278 L) | ✅ Dry/Wet/Paired/Monotone |
| ISMCTS pour OFC | `ismcts.c` (468 L) | ⚠️ Squelette — scoring OFC simulé |
| Bet Sizing Optimiser | `bet_sizing.c` (100 L) | ⚠️ Minimal |
| ICM Utility | `icm_utility.c` (102 L) | ⚠️ Fonctionnel mais basique |
| GTO Trainer CLI | `gto_trainer.c` (126 L) | ✅ Lit les `.pe_sol`, quiz CLI |
| MPF Tree Parser | `mpf_tree.c` (3456 L) | ✅ Parsing JSON robuste |
| Compact Storage `.pe_sol` | `mpf_compact_storage.c` (1385 L) | ✅ Quantification 16-bit + mmap |
| CFR Resolve (CFR-D) | `cfr_resolve.c` (1055 L) | ✅ Subgame re-solving |
| Opponent Model / Node Lock | Intégré dans mpf_tree/cfr | ✅ Verrouillage de fréquences |

---

## 8. Bilan des Lacunes pour un Solver "Toutes Variantes + GPU"

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│                    ÉCART ENTRE L'ÉTAT ACTUEL ET L'OBJECTIF                 │
├───────────────────────────────────┬─────────────────────────────────────────┤
│ GAP 1 : MCCFR / Sampling         │ CRITIQUE — Sans External Sampling MCCFR│
│                                   │ le solver ne peut pas traiter des       │
│                                   │ arbres de plus de ~10^6 nœuds.         │
│                                   │ Effort estimé : 2-3 semaines           │
├───────────────────────────────────┼─────────────────────────────────────────┤
│ GAP 2 : Pruning (RBP)            │ IMPORTANT — Le Regret-Based Pruning     │
│                                   │ accélère le CFR de 5-10× sur les       │
│                                   │ dernières itérations. Son absence       │
│                                   │ rend le solver inutilement lent.       │
│                                   │ Effort estimé : 1 semaine              │
├───────────────────────────────────┼─────────────────────────────────────────┤
│ GAP 3 : Adapters Manquants       │ CRITIQUE — 10+ variantes sans aucun    │
│ (PLO5/6, Draw, Badugi, OFC...)   │ adapter CFR. Chaque adapter nécessite   │
│                                   │ la modélisation du game tree            │
│                                   │ (streets, discards, showdown rules).   │
│                                   │ Effort estimé : 1-2 semaines/variante  │
├───────────────────────────────────┼─────────────────────────────────────────┤
│ GAP 4 : Tree Builder Automatique │ IMPORTANT — Sans générateur, chaque    │
│                                   │ spot nécessite un JSON manuel.          │
│                                   │ Effort estimé : 3-4 semaines            │
├───────────────────────────────────┼─────────────────────────────────────────┤
│ GAP 5 : GPU CFR (Traversée)      │ MAJEUR — Le code actuel est un stub    │
│                                   │ avec TODO. La traversée GPU est         │
│                                   │ un projet de recherche en soi.          │
│                                   │ Effort estimé : 2-3 mois               │
├───────────────────────────────────┼─────────────────────────────────────────┤
│ GAP 6 : API Bindings CFR         │ MODÉRÉ — Les stubs doivent être         │
│                                   │ connectés à la vraie logique.           │
│                                   │ Effort estimé : 1-2 semaines            │
├───────────────────────────────────┼─────────────────────────────────────────┤
│ GAP 7 : Préflop Solving          │ CRITIQUE — Requiert MCCFR (GAP 1) +    │
│                                   │ abstraction préflop + chance sampling.  │
│                                   │ Effort estimé : 1-2 mois               │
├───────────────────────────────────┼─────────────────────────────────────────┤
│ GAP 8 : Couche Serveur / IPC     │ NÉCESSAIRE POUR FRONTEND — Aucun       │
│                                   │ mécanisme de communication entre le     │
│                                   │ solver C et un processus UI.            │
│                                   │ Effort estimé : 2-3 semaines            │
└───────────────────────────────────┴─────────────────────────────────────────┘
```

---

## 9. Plan d'Action Recommandé (Ordre de Priorité)

### Phase 0 — Fondations Algorithmiques (Prérequis pour tout le reste)

1. **Implémenter External Sampling MCCFR** dans `cfr_core.c`
   - Ajouter le sampling des actions adverses au lieu de la traversée exhaustive
   - Ajouter le chance sampling (échantillonner 1 board au lieu d'énumérer 19 600)
   - Ceci débloque le preflop solving et les arbres PLO

2. **Ajouter Regret-Based Pruning (RBP)**
   - Sauter les actions dont le regret est < seuil pendant les premières itérations
   - Gain de performance 5-10× pour la convergence

### Phase 1 — Tree Builder & Adapters (Débloque "Toutes Variantes")

3. **Construire un Tree Builder programmatique**
   - Entrée : variante + stacks + blindes + sizings + nombre de joueurs
   - Sortie : arbre `mpf_tree` en mémoire (pas de JSON nécessaire)

4. **Créer les adapters manquants** (par priorité de demande marché)
   - PLO5/PLO6 → étendre l'adapter Omaha
   - 2-7 Triple Draw → nouveau (3 tours de discard + showdown)
   - Badugi → nouveau (3 tours de discard + 4-card low)
   - Pineapple → nouveau (discard post-flop)

### Phase 2 — GPU Accélération Réelle

5. **Implémenter la traversée GPU CFR**
   - Linéariser l'arbre de jeu en buffer contigu
   - Exécuter le traversal en warp-parallel sur GPU
   - Exploiter la formulation matricielle existante pour les mises à jour

6. **Compléter les kernels d'évaluation GPU manquants**
   - Kernel Badugi sur GPU
   - Kernel Drawmaha (split pot) sur GPU

### Phase 3 — API & Frontend

7. **Connecter les bindings C API** au vrai moteur CFR
8. **Construire la couche serveur** (WebSocket / gRPC)
9. **Développer le frontend** (cf. plan détaillé du message précédent)

---

## 10. Conclusion

Le projet `poker-eval` dispose d'une **fondation d'évaluation de mains
exceptionnelle** — probablement l'une des plus complètes en open-source pour le
poker. Le solver CFR fonctionne correctement sur des **sous-jeux river
heads-up** et des **arbres postflop multiway prédéfinis en JSON**, ce qui est
déjà un accomplissement significatif.

Cependant, le chemin vers un "solver toutes variantes avec GPU" est encore
**substantiel** :

- **~3-6 mois de développement** pour les fondations algorithmiques (MCCFR,
  pruning, tree builder, adapters)
- **~2-3 mois supplémentaires** pour le GPU CFR réel
- **~2-3 mois** pour la couche serveur et le frontend

Le projet est à **environ 35-40% du chemin** vers l'objectif d'un solver
universel accéléré sur GPU, mais les 60% restants sont les plus difficiles car
ils impliquent de la recherche algorithmique (MCCFR sur GPU, abstraction
automatique pour PLO5/6) en plus de l'ingénierie pure.

> [!IMPORTANT]
> **Recommandation** : Avant de lancer le frontend, consolider les fondations
> algorithmiques (MCCFR + RBP + tree builder). Un frontend sans un solver
> capable de résoudre des spots réels en temps raisonnable serait une coquille
> vide.

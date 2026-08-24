# Analyse de marché — poker-eval face aux solvers 2026

**Date :** 24 août 2026
**Branche analysée :** `feat/pr-01-solver-contracts` (HEAD `98ebc9fc`)
**Référence :** panorama analytique des solvers de poker 2026 (29 produits/projets : PioSOLVER, GTO+, Simple Poker, MonkerSolver, GTO Wizard, Deepsolver, HRC, ICMIZER/Postflopizer, TexasSolver, postflop-solver, Shark, OpenSpiel, PokerRL, RLCard, noambrown/poker_solver, Oleg Solvers, etc.)

---

## 1. Positionnement : ce que poker-eval est réellement

poker-eval n'est aujourd'hui **ni un produit joueur (Pio/GTO+) ni une plateforme cloud
(Wizard/Deepsolver)** : c'est un **moteur-bibliothèque C de solving multiway
multi-variantes**, avec deux générations de CFR (legacy mature + v3 en migration), une
assise évaluation/equity bien plus large que n'importe quel concurrent, et une rigueur de
validation de niveau recherche (oracles analytiques, LP exact vs Gambit, fixtures
OpenSpiel).

Sa position naturelle sur la cartographie du marché : **la case « open source /
recherche » — mais en beaucoup plus large que postflop-solver, Shark ou TexasSolver**, et
avec une licence (BSD-3-Clause pour le matériel récent) plus permissive que toute la
famille AGPL du solving OSS.

---

## 2. Forces déjà supérieures au marché (à ne pas sous-estimer)

| Capacité | poker-eval | Marché |
|---|---|---|
| **Multiway natif** (CFR ≤ 7-8 joueurs, side pots) | ✅ testé, exemple 4-way | Monker 499 €, Simple 3-Way 249 $/an, Wizard partiel |
| **Variantes en solving** : Hold'em, PLO4/5/6, Short Deck (multiway) + adapters river Omaha8/Stud/Stud8/Razz | ✅ | Oleg Solvers 11 500–17 500 $ pour des niches moins larges |
| **Interop MonkerSolver** (import `.tree`/`.mkr` + comparaison par exploitabilité) | ✅ unique | personne d'autre ne le fait |
| **Largeur algorithmique** : CFR, CFR+, DCFR (corrigé Brown & Sandholm), Linear, ECFR, MCCFR externe/outcome, RBP | ✅ dépasse les 5 familles de noambrown/poker_solver | les commerciaux ne publient même pas leur algo |
| **Node locking avancé** : opponent models JSON, relock périodique, mesure de perte d'EV | ✅ au niveau Pio/GTO+/Wizard | — |
| **Card bunching** (folded ranges, FEAT-14) | ✅ | rare même commercialement (postflop-solver seulement) |
| **ICM + rake branchés dans le CFR** via utilité terminale générique (`pe_utility_fn`) | ✅ testé | ICMIZER/Postflopizer propriétaires |
| **Transparence + licence BSD** | ✅ | aucun équivalent : tout l'OSS solver est AGPL |

Évidences clés : `src/engine/solvers/cfr/` (moteur legacy), `src/solver/domain/` +
`src/solver/adapters/` (v3), `tests/game_theory/` (qualification math),
`tools/mpf_run_with_metrics.c` (CLI solver), `examples/4way_postflop/`.

---

## 3. Manques fonctionnels face au marché

### Priorité 1 — bloquants pour exister face aux produits installés

1. **Solver préflop scalable.** Support structurel présent (`start_street = preflop`,
   snapshot préflop, chance flop C(n,3)) mais le code lui-même indique que ce n'est pas
   l'approche à l'échelle. Marché : Pio Edge, Simple Preflop Holdem (2–10 joueurs),
   GTO Wizard (9 joueurs). Correspond à la **Lane B (M10+)** du backlog v3 — non construite.
2. **Pas de GUI ni de couche produit joueur.** Pio, GTO+, TexasSolver, Shark, GTO Nexus
   (gratuit) ont tous une interface. `tools/gto_trainer.c` fait 126 lignes et affiche des
   actions génériques (`.pe_sol` ne stocke pas les libellés). Sans GUI/viewer, le projet
   reste invisible du segment qui achète.
3. **Play-vs-solution / trainer riche.** Pio Trainer, GTO+ play-vs-solution,
   Deepsolver Trainer 360°, Lucid drills : le marché 2026 monétise l'entraînement plus que
   le solving. Le trainer actuel est une ébauche (issue #154).
4. **Agrégation et reporting.** Deepsolver/GTO+ vendent les *aggregated flop reports* et
   *runout reports* ; poker-eval a de l'export JSON/CSV par street mais pas d'agrégation
   de type « rapport de flop sur tout le range ».

### Priorité 2 — segments de marché entiers non couverts

5. **Tournois avancés** : pas de FGS, pas de PKO/bounty, ICM limité à Malmuth-Harville
   analytique (`src/economics/icm.c`). HRC/ICMIZER/Postflopizer/GTO LAB vivent sur ce
   créneau. L'issue #153 (ICM multiway asymétrique) est le bon ticket d'entrée.
6. **Push/fold Nash** (Simple Nash, HRC) : le moteur CFR pourrait le résoudre, aucun
   produit dédié n'existe dans le dépôt.
7. **Import de hand histories** : GTO Wizard, GTOBase, HRC, ICMIZER l'ont tous ; absent
   ici. C'est la porte d'entrée du workflow joueur moderne.
8. **Solving des draw games** : `draw_abstraction.c` et `PE_CHANCE_DRAW_N` sont préparés
   mais aucun adapter ne les consomme. C'est exactement la niche d'Oleg Solvers, et les
   évaluateurs 2-7 TD/Badugi sont déjà prêts — potentiellement le seul solver open source
   de draw games au monde.
9. **Pas de couche neuronale / depth-limited solving** (tendance 2026 : GTO Wizard AI,
   Deepsolver). Choix assumable pour un moteur exact, mais c'est la trajectoire du marché
   grand public.

---

## 4. Manques techniques

1. **Parallélisme du CFR principal** : le moteur legacy (`src/engine/solvers/cfr/`) n'a
   **aucun OpenMP** ; seul le compute port v3 (`src/solver/adapters/compute_cpu_par.c`)
   est parallélisé. Pio vend 16/64 threads ; c'est le critère n°1 d'un solver local.
   Écart de performance le plus concret.
2. **SIMD dans le solver** : le SIMD (AVX2/AVX-512/NEON, bien réel dans
   `src/equity/simd_operations.c`) sert l'equity, pas le CFR. postflop-solver et Shark en
   font leur argument central.
3. **Bindings du solver** : `pe_cfr_*` dans l'API C stable (`bindings/c/`) = stubs
   retournant `PE_ERROR_NOT_SUPPORTED` ; Python (`bindings/python/`) n'expose rien du
   solver. Le wheel pip est un atout inutilisé côté solving.
4. **Compression** : quantification 16 bits ✅ (`.pe_sol`, storage v3 int16) mais pas de
   zstd (postflop-solver : bincode+zstd) ; pas d'API de budget mémoire explicite alors que
   la RAM est le facteur limitant reconnu du marché.
5. **Isomorphismes de rang** : l'orbite complète des 24 permutations de couleurs existe
   (`board_canonical.c`, ISO-01), pas les symétries de rangs.
6. **Cycle de vie v3 partiellement incomplet** : la cible
   `target_exploitability_mbb` est désormais exécutée par le parcours
   `PE_TRAVERSAL_FULL_VECTOR` (mesure BR périodique, métriques mBB et arrêt anticipé).
   Restent `pe_tree_port` et les parcours scalar/samplés ; Lane B partielle.
7. **GPU-CFR** : déprécié/à l'état de stubs (`gpu_cfr_solve()` = boucle vide avec TODO)
   alors que `bench_gpu_cfr.c` promet « ×200–×400 speedup ». À nettoyer : c'est
   exactement le genre de claim reproché aux boîtes noires dans le panorama marché.
8. **Pas de WASM** (postflop-solver a WASM Postflop) ni de solving distribué/cluster
   (Simple Poker cluster, PokerRL Ray) — deux niches cloud pertinentes pour un moteur.

---

## 5. Manques de crédibilité doc-vs-code

L'avantage marché du projet est la **transparence** (tout l'inverse des « cœurs non
divulgués » du tableau marché). Or l'audit révèle des trous qui la minent :

- **README annonce Metal** (2 fois) → zéro code Metal. Sur macOS, c'est pourtant le
  backend GPU le plus pertinent.
- `docs/cfr/guides/README.md` liste **7 guides « ✅ » dont 4 n'existent pas** (CFR Tree
  Format, CFR Metrics, CFR Export Results, CFR Performance, CFR Data Pipeline).
- L'API C stable déclare une surface CFR non implémentée (stubs `pe_cfr_*`).
- Recette **Conan cassée** (`conanfile.py` : chemins d'export inexistants, option
  dépréciée) ; CI conan/vcpkg en quarantaine « jamais passée ».
- Bug de build : `src/benchmarks/CMakeLists.txt` référence `gpu/ofc_gpu_benchmark.c` au
  lieu de `src/gpu/ofc_gpu_benchmark.c` (casse les builds GPU + exemples).
- `pyproject.toml` passe l'option dépréciée `BUILD_STATIC_LIBS`.
- Binding C++ listé mais vide (uniquement `ReadMe.txt`).

---

## 6. Recommandations priorisées

1. **Finir le cycle de vie v3** (stopping vectoriel par cible d'exploitabilité ✅,
   parallélisation OpenMP complète et ports scalar/samplés restants) — chemin critique
   du backlog v3, ferme l'écart de performance n°1.
2. **Purger les claims faux** (Metal, bench GPU-CFR, guides manquants, stubs `pe_cfr_*`
   ou leur documentation honnête) — coût faible, crédibilité forte.
3. **Un viewer/GUI léger** (même web/WASM sur `.pe_sol`/`.pe_tree`) — fait passer de
   « bibliothèque pour chercheurs » à « alternative Shark/TexasSolver » visible.
4. **Draw-game adapters** — différenciateur absolu, quasi prêt (abstraction +
   évaluateurs déjà présents).
5. **Exposer le solver dans Python** (le wheel existe déjà) + import de hand histories —
   les deux portes d'entrée du plus large public.
6. **Lane B préflop et ICM/PKO** ensuite, selon la roadmap existante (#153, M10–M11).

---

## 7. Verdict

Il ne manque pas tant de *fonctionnalités de moteur* — poker-eval dépasse déjà l'open
source existant en largeur algorithmique, multiway et variantes — que de **couche
produit** (GUI, trainer, reports, HH import) et de **performance parallèle**, plus un
nettoyage des écarts doc/code.

Sur le créneau **« moteur multiway/variantes transparent, BSD, avec interop
MonkerSolver »**, le projet n'a aujourd'hui **aucun concurrent direct**.

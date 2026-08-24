# Analyse de marché — poker-eval face aux solvers 2026

**Date :** 24 août 2026
**Branche analysée :** `feat/pr-01-solver-contracts` (HEAD à mettre à jour après le commit de cette tranche)
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

1. **Solver préflop scalable.** La **Lane B est maintenant branchée dans le cycle public
   `pe_solver`** : `external-mccfr` et `outcome-mccfr` échantillonnent les deals privés,
   relisent les regrets du storage et appliquent les mises à jour par batch. Le sampler
   corrélé couvre Hold'em/PLO4/PLO5/PLO6, jusqu'à huit joueurs, avec card removal et
   ratio d'importance. `pe_preflop_betting_game` relie ce flux à l'état de mise et
   libère les états enfants à chaque deal : la mémoire ne croît plus avec le nombre
   d'itérations. La preuve d'intégration couvre un arbre Hold'em et un PLO5 trois-way.
   `pe_external_best_response_sampled` ajoute une mesure BR empirique explicitement
   étiquetée. `pe-preflop-tree` construit désormais un arbre préflop JSON borné à
   partir des stacks, tailles et ranges déclarées. Le nouveau pont
   `pe_cfr_external_adapter` expose aussi les arbres `cfr_game_t` multi-rues au
   chemin Lane B, y compris les nœuds de chance volumineux, et
   `mpf_run_with_metrics --lane-b --sample-batch N` permet de les exécuter via le
   solver v3 avec regroupement des trajectoires. Cela ferme le branchement
   fonctionnel ; les benchmarks de production 2–10/9 joueurs et le parallélisme
   des trajectoires restent à qualifier.
2. **GUI / couche produit joueur.** `pe-solution-report` produit désormais un JSON v2
   et un viewer HTML autonome avec filtre interactif par street/flop/board/nœud. La
   cible `poker-eval-trainer-gui` produit maintenant une application C + SDL2
   redimensionnable sur macOS, Linux et Windows, structurée en `Setup`, `Solve` et
   `Explore` : glisser-déposer `.pe_sol`/CSV, arbre visuel, contexte du spot, index
   d'infosets, fréquences d'actions, trainer et export de session JSON. Il manque
   encore l'éditeur de ranges intégré et le sélecteur de fichiers natif.
3. **Play-vs-solution / trainer riche.** `poker-eval-trainer` suit les transitions
   `next_key`, affiche street/board/runout/position/pot lorsqu'ils sont fournis, et
   exporte une session JSON avec les réponses, meilleurs choix, pertes de stratégie et
   chemin d'entraînement. `pe-hand-history-import` normalise les hand histories
   PokerStars courantes en `pe-hand-history/v1`; l'export HTML couvre les drills
   adaptatifs de base. Il manque encore le mapping room par room et les drills
   calibrés sur l'historique du joueur.
4. **Agrégation et reporting.** `pe-solution-report --aggregate board|flop|runout` produit
   des rapports déterministes sur un sidecar `key,street,board,weight[,flop,runout]`.
   `pe-runout-report` énumère les boards conditionnels exacts avec masse de probabilité
   contrôlée. Le mode `.tree` + `.mkr` lie les slots aux nœuds ; les clés hashées brutes
   sont refusées comme non inversibles lorsqu'aucune métadonnée n'est fournie.

### Priorité 2 — segments de marché entiers non couverts

5. **Tournois avancés** : pas de FGS, pas de PKO/bounty, ICM limité à Malmuth-Harville
   analytique (`src/economics/icm.c`). HRC/ICMIZER/Postflopizer/GTO LAB vivent sur ce
   créneau. L'issue #153 (ICM multiway asymétrique) est le bon ticket d'entrée.
6. **Push/fold Nash** (Simple Nash, HRC) : le moteur CFR pourrait le résoudre, aucun
   produit dédié n'existe dans le dépôt.
7. **Import de hand histories** : un importeur PokerStars textuel et versionné existe
   maintenant ; restent les formats réseau/room et le mapping automatique vers les
   clés d'infosets. C'est la porte d'entrée du workflow joueur moderne.
8. **Solving des draw games** : `draw_abstraction.c` et `PE_CHANCE_DRAW_N` sont préparés
   mais aucun adapter de règles ne les consomme encore. Le pont legacy→v3 existe
   maintenant pour les jeux qui exposent déjà `cfr_game_t` (Stud/Short Deck inclus) ;
   Draw reste exactement la niche d'Oleg Solvers, avec les évaluateurs 2-7 TD/Badugi
   déjà prêts.
9. **Pas de couche neuronale / depth-limited solving** (tendance 2026 : GTO Wizard AI,
   Deepsolver). Choix assumable pour un moteur exact, mais c'est la trajectoire du marché
   grand public.

### Priorité 2 — tranche implémentée dans le noyau C

Les briques suivantes sont maintenant livrées et testées, avec leurs limites explicites :

5. **ICM asymétrique** : `pe_icm_calculate_asymmetric()` applique exactement la récursion
   Malmuth-Harville à une matrice de gains joueur/position. `pe-icm` expose l'ICM standard
   en JSON (`pe-icm/v1`) et accepte `--asym-payouts MATRIX.csv` pour cette variante. La
   matrice permet de représenter une utilité bounty/PKO déjà calculée par l'appelant ; la
   conversion automatique des bounties, le FGS dynamique et le modèle complet de tournoi
   restent à construire.
6. **Push/fold Nash** : `pe-push-fold` résout l'abstraction zéro-somme explicite
   fold/push contre fold/call par regret matching et retourne fréquences, EV et
   exploitabilité. `pe-push-fold-multiway` accepte une équité conditionnelle par sous-ensemble
   de callers et résout une abstraction de coalition jusqu'à quatre adversaires ; ce n'est
   pas encore un solveur HRC range-aware avec arbre d'actions complet.
7. **Mapping hand history** : `pe-hand-history-import --mapping LABELS.csv` enrichit les
   lignes PokerStars normalisées avec `infoset_key`, `mapped`/`unmapped` et les compteurs
   de couverture. Le sidecar accepte les formats compact et riche du trainer ; les
   `--input-format normalized` permet désormais à n'importe quel parser de room externe de
   fournir le même contrat CSV ; les parsers spécifiques réseau et l'inférence de position
   à partir d'une main sans métadonnées restent à ajouter.
8. **Draw chance** : `pe_draw_chance_*()` consomme réellement `PE_CHANCE_DRAW_N` pour
   Badugi et Triple Draw 2-7, énumère les combinaisons de remplacement et reconstruit la
   main privée. `pe_draw_cfr_build_game()` fournit maintenant un round CFR à deux joueurs
   avec menus de discard bornés, transitions chance et terminal callback ; les arbres
   multi-rues, betting, showdown spécialisé et paytables restent à compléter.
9. **Depth-limited / modèle externe** : `cfr_config_t.depth_value_fn` permet de remplacer
   proprement l'erreur de profondeur par une valeur vectorielle fournie par un modèle
   externe ou neuronal. Le dépôt ne fournit volontairement pas de réseau pré-entraîné.

Les outils `pe-fgs` et la couche `pe_pko_calculate()` complètent cette tranche : FGS agrège
des scénarios futurs pondérés via l'ICM existant, tandis que PKO ajoute les bounties à partir
d'une matrice explicite de probabilités d'élimination. Ils ne déduisent pas encore les
scénarios depuis un arbre de tournoi ni les éliminations depuis des ranges.

Cette tranche ferme donc les contrats d'intégration et les CLI vérifiables ; elle ne doit
pas être présentée comme un clone complet de HRC/ICMIZER/GTO Wizard ou comme un solver
neuronal prêt à l'emploi.

### Priorité 2 — primitives tournoi et modèle embarqué ajoutées

Les quatre manques de la tranche suivante disposent maintenant d'une implémentation C
testée :

10. **Arbre HRC multiway** : `pe_hrc_solve()` (`economics/hrc.h`) énumère les profils de
    ranges indépendants en supprimant exactement les collisions de cartes, valide un arbre
    de betting acyclique, puis applique le regret matching sur ses nœuds publics. Le callback
    terminal reçoit les montants et le chemin d'actions, ce qui permet d'y brancher pots,
    antes et side pots. La stratégie est maintenant maintenue par nœud et par combo privé ;
    `action_probability` expose aussi la vue publique agrégée et
    `pe_hrc_result_combo_probability()` la décision privée. Il reste l'import HRC
    propriétaire et le solveur est volontairement borné par `max_profiles`.
11. **FGS dynamique** : `pe_fgs_calculate_tree()` (`economics/fgs.h`) parcourt un arbre de
    transitions probabilistes de profondeur bornée, vérifie les probabilités locales et
    agrège l'ICM de chaque feuille. Cela remplace le simple fichier de scénarios plat ; un
    générateur de tournoi room-specific reste un adapter à écrire.
12. **PKO depuis ranges** : `pe_pko_calculate_from_ranges()` énumère les profils pondérés
    sans cartes communes, appelle un évaluateur de showdown/tournoi pour chaque profil et
    construit automatiquement la matrice élimination → bounty → ICM. Les éliminations
    multi-étapes, la modélisation des ranges qui se resserrent après chaque action et le
    calcul PKO complet depuis un arbre restent à brancher au callback.
13. **MLP embarqué** : `pe_nn_depth_value_callback()` adapte le MLP CPU déjà présent au
    callback depth-limited de CFR. L'inférence est locale, sans runtime externe ni réseau
    pré-entraîné fourni ; le modèle et l'extracteur de features sont injectés par l'appelant.
14. **Import HRC/PKO et pots** : `pe_hrc_import_json_file()` accepte le contrat portable
    `pe-hrc/v1`/`pe-pko/v1`, construit les ranges réelles via `pe_range_parse()`, restaure
    les nœuds/actions et conserve stacks, antes, bounties et payouts. `pe_hrc_trace_pot()`
    rejoue un chemin d'actions incrémentales et produit les slices main/side pots avec
    éligibilité après fold/all-in ; `pe_hrc_import_make_pko_input()` relie ensuite ces
    ranges aux calculs PKO existants. `pe-hrc-import` permet de valider un fichier depuis
    la ligne de commande.

Les tests `test_hrc`, `test_fgs_tree`, `test_pko_ranges`, `test_hrc_import` et
`test_nn_depth_value` couvrent respectivement le parcours multiway avec card removal,
l'agrégation dynamique, la dérivation PKO depuis ranges, l'import JSON avec pot accounting
et le branchement du MLP. Cette livraison ferme les contrats noyau, pas les quatre produits
finis : il reste l'éditeur visuel d'arbres, les adapters room-specific FGS/PKO, des poids
entraînés et la modélisation des ranges qui évoluent après chaque action.

---

## 4. Manques techniques

1. **Parallélisme du CFR principal** : le compute port v3 (`compute_cpu_par.c`) est
   parallélisé. Le legacy expose `cfr_config_t::num_threads` pour les balayages
   indépendants du stockage et `cfr_solve_parallel_batch()` pour lancer de vraies
   traversées récursives concurrentes avec un jeu et un stockage privés par worker,
   puis une fusion déterministe. Cette lane est sûre et portable, mais c'est du CFR
   par lots : elle ne prétend pas reproduire bit à bit la continuation séquentielle.
   La traversée legacy entièrement multi-threadée avec mémoire partagée reste hors
   scope tant que les callbacks de jeu ne fournissent pas un contrat d'isolation.
2. **SIMD dans le solver** : le regret matching legacy utilise maintenant une réduction
   AVX2 ou NEON (avec repli scalaire) dans `cfr_storage.c`. Ce n'est pas encore une
   vectorisation complète de la traversée ni un backend AVX-512.
3. **Bindings du solver** : l'API C stable conserve la création historique sans arbre,
   mais `pe_cfr_create_callbacks()` fournit désormais un chemin réel vers `cfr_solve`,
   la stratégie, les snapshots et l'exploitabilité. Le module Python expose déjà le
   solveur v3 callback-backed. Il reste à empaqueter une API haut niveau de jeu/ranges.
4. **Compression** : `.pe_sol` propose maintenant `pe_cfr_save_storage_zstd()` quand
   zstd est disponible, avec repli explicite `ENOTSUP`. La mémoire est déjà un paramètre
   v3 (`max_ram_bytes`) validé avant allocation et détaillé par `pe_solver_estimate()`.
5. **Isomorphismes de rang** : l'orbite complète des 24 permutations de couleurs existe
   (`board_canonical.c`, ISO-01). Un canonicaliseur de rangs est maintenant disponible
   uniquement pour une variante qui fournit explicitement la liste de ses automorphismes
   et un validateur sémantique. Le standard poker n'en active aucune : les quintes et
   l'ordre des hauteurs rendent les permutations arbitraires incorrectes.
6. **Cycle de vie v3 partiellement incomplet** : la cible
   `target_exploitability_mbb` est désormais exécutée par le parcours
   `PE_TRAVERSAL_FULL_VECTOR` (mesure BR périodique, métriques mBB et arrêt anticipé).
   Restent `pe_tree_port`, la mesure BR sur Lane B et les adapters de jeux préflop
   concrets ; Lane B est exécutable mais pas encore un solveur préflop produit complet.
7. **GPU-CFR** : l'ancien `gpu_cfr_solve()` n'est plus une boucle vide et exécute
   maintenant le sous-problème matriciel regret-matching/average-strategy. Comme cette
   API ne reçoit pas d'arbre, elle ne prétend plus résoudre un jeu ni fournir ×200–×400 ;
   le solveur complet passe par `pe_solver_run` et ses compute ports.
8. **WASM et distribué** : un profil `cmake/wasm.cmake` produit maintenant une cible
   portable sans GPU/OpenMP/zstd. Le header `pe_solver_cluster.h` fournit un sharding
   stable et un manifeste de tâches/checkpoints lisible par un orchestrateur externe,
   testé et indépendant de la machine. Le transport réseau, les leases et le merge de
   solves distants restent à construire ; le profil WASM n'est pas encore une GUI.

---

## 5. Manques de crédibilité doc-vs-code

L'avantage marché du projet est la **transparence** (tout l'inverse des « cœurs non
divulgués » du tableau marché). Or l'audit révèle des trous qui la minent :

- **README annonce Metal** (2 fois) → zéro code Metal. Sur macOS, c'est pourtant le
  backend GPU le plus pertinent.
- `docs/cfr/guides/README.md` liste **7 guides « ✅ » dont 4 n'existent pas** (CFR Tree
  Format, CFR Metrics, CFR Export Results, CFR Performance, CFR Data Pipeline).
- L'ancienne création CFR sans description de jeu reste limitée, mais
  `pe_cfr_create_callbacks()` et la façade `pe_solver_api_*` exposent désormais des
  chemins de solving réels et testés.
- Recette **Conan cassée** (`conanfile.py` : chemins d'export inexistants, option
  dépréciée) ; CI conan/vcpkg en quarantaine « jamais passée ».
- Bug de build : `src/benchmarks/CMakeLists.txt` référence `gpu/ofc_gpu_benchmark.c` au
  lieu de `src/gpu/ofc_gpu_benchmark.c` (casse les builds GPU + exemples).
- `pyproject.toml` passe l'option dépréciée `BUILD_STATIC_LIBS`.
- Binding C++ listé mais vide (uniquement `ReadMe.txt`).

---

## 6. Recommandations priorisées

1. **Compléter Lane B côté produit** (transitions de street, codecs de ranges et
   mesure BR plus précise) — l'arbre préflop JSON et la BR empirique sont désormais
   disponibles.
2. **Purger les claims faux** (Metal, bench GPU-CFR, guides manquants, stubs `pe_cfr_*`
   ou leur documentation honnête) — coût faible, crédibilité forte.
3. **Étendre le viewer/trainer** vers les formats de rooms, les drills adaptatifs
   calibrés et l'éditeur de ranges ; le parcours interactif multi-OS, l'import
   PokerStars et les rapports de flop/runout sont disponibles.
4. **Draw-game adapters** — différenciateur absolu, prochaine étape après le pont
   Stud/Short Deck livré (abstraction + évaluateurs déjà présents).
5. **Exposer le solver dans Python** (le wheel existe déjà) + import de hand histories —
   les deux portes d'entrée du plus large public. La façade C v3 est maintenant la
   base native de cette étape.
6. **Finaliser le solveur préflop produit et ICM/PKO** ensuite, selon la roadmap
   existante (#153, M10–M11).

---

## 7. Verdict

Il ne manque pas tant de *fonctionnalités de moteur* — poker-eval dépasse déjà l'open
source existant en largeur algorithmique, multiway et variantes — que de **couche
produit** (GUI, trainer, reports, HH import) et de **performance parallèle**, plus un
nettoyage des écarts doc/code.

Sur le créneau **« moteur multiway/variantes transparent, BSD, avec interop
MonkerSolver »**, le projet n'a aujourd'hui **aucun concurrent direct**.

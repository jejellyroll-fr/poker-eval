# Couche produit v1 : viewer, trainer desktop et rapports

Cette tranche fournit une sortie exploitable sans modifier le format binaire `.pe_sol`.
Les métadonnées de spot restent séparées afin de préserver la compatibilité avec les
solutions déjà produites.

## Rapport agrégé

Le sidecar CSV utilise quatre colonnes de base et accepte `flop`/`runout` pour les
rapports de gamme :

```csv
key,street,board,weight
0x1234,flop,AsKd7c,1.0
0x4567,flop,AsKd7c,0.75
```

```csv
key,street,board,weight,flop,runout
0x1234,turn,AsKd7cQs,1.0,AsKd7c,AsKd7cQs
0x4567,river,AsKd7cQs2h,0.75,AsKd7c,AsKd7cQs2h
```

La commande suivante produit un JSON versionné et une page HTML autonome :

```sh
pe-solution-report \
  --solution solve.pe_sol \
  --metadata spots.csv \
  --aggregate flop \
  --json report.json \
  --html report.html
```

Le JSON v2 contient, pour chaque groupe `street/board`, `street/flop` ou
`street/runout`, le nombre d'infosets, le poids,
l'entropie en bits et la fréquence agrégée de chaque action. Sans sidecar, les lignes
sont regroupées sous `unknown/unknown` : le programme ne prétend pas décoder un key
Monker dont le codec n'est pas fourni.

Le HTML est un viewer autonome filtrable par street, flop, board ou nœud ; il ne
nécessite ni serveur ni dépendance JavaScript externe.

Pour une archive Monker, le rapport peut maintenant décoder directement les artefacts
natifs :

```sh
pe-solution-report \
  --monker-tree spot.tree \
  --monker-mkr run.mkr \
  --html monker-report.html
```

Les slots sont liés aux nœuds, le nombre de classes est vérifié, puis les octets sont
agrégés par nœud et par street. `--entry storedstrategyN` sélectionne une entrée précise.

## Trainer avec libellés

Le trainer conserve le mode interactif existant et accepte un fichier de labels :

```csv
key,street,board,action,label,next_key
0x1234,preflop,,0,fold,0x2000
0x1234,preflop,,1,call,0x3000
0x2000,flop,AsKd7c,0,check,0x4000
0x2000,flop,AsKd7c,1,bet,0x5000
```

```sh
poker-eval-trainer --solution solve.pe_sol --labels labels.csv --rounds 20 \
  --session-json session.json
```

Une session peut être reprise avec `--resume-session session.json` : le score,
la perte de stratégie et le niveau de difficulté sont restaurés avant les
nouveaux spots, puis réécrits dans le prochain export.

Le trainer joueur est maintenant une application desktop portable en C + SDL2. La même
cible fonctionne sur macOS, Linux et Windows. L'interface est organisée comme un vrai
outil de solving : `Setup` pour le spot, `Solve` pour le suivi de l'arbre et du budget,
`Explore` pour l'index des infosets, les fréquences et le contexte street/board/position.
Les sélecteurs de variante (Hold'em/PLO4/PLO5/PLO6), de joueurs (2–8) et de moteur
sont actifs. Le moteur `Vector CPU` est branché : le bouton `Solve this spot` appelle
le binaire `pe-vector-sim` à côté de la GUI et affiche sa sortie EV/deals/tree. Les
entrées Legacy CFR et GPU signalent explicitement qu'elles restent à brancher. Les chemins `.tree` et `.mkr` peuvent
être déposés ou saisis dans les champs de configuration. Elle accepte aussi les
fichiers `.pe_sol`/CSV, les boutons d'actions,
le feedback, le score et l'export de session JSON :

```sh
cmake --build build --target poker-eval-trainer-gui
build/tools/poker-eval-trainer-gui --solution solve.pe_sol --labels labels.csv
```

Pour une solution sans sidecar de labels, les boutons peuvent être nommés
directement dans la commande :

```sh
build/tools/poker-eval-trainer-gui --solution solve.pe_sol \
  --actions fold,check,call,bet,raise
```

Les labels CSV restent prioritaires lorsqu’ils existent pour un infoset donné.
Le chemin de session peut être choisi explicitement avec `--session-json`; sinon
le GUI écrit `trainer-session.json` dans le répertoire courant.
Une session existante peut être reprise avec `--resume-session FILE` ; le score,
la perte de stratégie et la difficulté sont restaurés au démarrage.

Sans arguments, il suffit de déposer le `.pe_sol` puis le CSV de labels dans la fenêtre.
Le CSV est important : il transforme les indices techniques en `fold`, `call`, `bet`,
etc. et fournit street, board, position et pot.
Les onglets sont aussi accessibles au clavier avec `1`, `2`, `3` ou `Tab`; `N` passe
au spot suivant et `S` sauvegarde la session.

Le mode `--export-html` de `poker-eval-trainer` reste disponible pour partager une
session sans installer l'application, mais il n'est plus la surface principale du
produit.

Les libellés sont une vue produit ; la stratégie quantifiée reste la source de vérité.
Quand `next_key` est présent, le trainer suit la transition choisie vers le prochain
infoset au lieu de tirer un spot indépendant. Le trainer exporte une session JSON
avec les spots, les réponses, la perte de probabilité et les transitions suivies ;
les labels riches peuvent porter runout, position et pot.

## Lane B préflop

La façade solver accepte désormais un `pe_external_game_t` via
`pe_solver_deps_t::external_game`. Les presets `external-mccfr` et `outcome-mccfr`
utilisent ce chemin ; le proxy de stratégie relit les regrets du storage à chaque
visite, et les deals/chances sont fournis par l'adapter du jeu avec leur ratio
d'importance. Le module `pe_preflop_deal_sampler` fournit maintenant les deals
corrélés Hold'em/PLO4/PLO5/PLO6, le card removal séquentiel et le ratio d'importance
optionnel contre la normalisation exacte de la range.

`pe_preflop_betting_game` relie ces deals à `pe_betting_state_t` et expose le
parcours préflop complet au sampler Lane B : chaque itération tire un deal privé
corrélé, applique les actions sémantiques de l'arbre et atteint un terminal de mise.
Les reports de runout exacts sont disponibles via `pe-runout-report`, qui énumère
les boards turn/river conditionnels et vérifie que leur masse vaut 1.

`pe_external_best_response_sampled` mesure la valeur de la stratégie et une déviation
unilatérale par échantillonnage. Le résultat est marqué `empirical` : il sert au suivi
Lane B, mais ne se présente pas comme une BR exacte.

Le chemin CLI peut être mesuré sans parser la sortie humaine :

```sh
mpf_run_with_metrics --tree spot.tree --lane-b --iterations 10000 \
  --sample-batch 32 --benchmark-json lane-b.json
```

Le fichier `pe-lane-b-benchmark/v1` contient le nombre de trajectoires, le temps CPU,
le débit et le statut du solve. Ces chiffres sont des mesures de throughput local, pas
une garantie de temps de résolution ou d'exploitabilité.

Un arbre de mise préflop déclaratif peut être produit avec les ranges et les tailles
d'actions du spot :

```sh
pe-preflop-tree --players 2 --stack 100 --stack 100 --raises 2,4,8 \
  --range 0 '22+,AKs' --range 1 'random' --output tree.json
```

Le document `pe-preflop-tree/v2` contient les nœuds, les transitions légales, le pot,
le montant à payer et les ranges d'entrée.

Le générateur compile désormais les ranges au lieu de seulement les recopier. La
sortie `pe-preflop-tree/v2` contient `rangeProfiles` au format MPF, avec chaque combo
card-removalée et son poids normalisé :

```sh
pe-preflop-tree --game holdem --players 2 --stack 100 --stack 100 \
  --raises 2,4,8 --range 0 '22+,AKs' --range 1 'JJ+,AQs+' \
  --output tree.json
```

`--max-combos` protège les exports PLO très larges contre un fichier généré
accidentellement gigantesque.

Le trainer CLI propose trois politiques de drill :

```sh
poker-eval-trainer --solution solve.pe_sol --labels spots.csv \
  --drill-mode progressive --rounds 50 --session-json session.json
```

`progressive` sélectionne les spots dont la difficulté entropique est accessible,
`balanced` échantillonne tout le portefeuille et `review` rejoue le dernier spot
raté. Le mode utilisé est conservé dans `pe-trainer-session/v1`.

Les hand histories courantes de type PokerStars peuvent être normalisées :

```sh
pe-hand-history-import --input hand.txt --format json --output hand.json
```

Le format `pe-hand-history/v1` conserve hand id, street, board, joueur, action et
montant pour les futurs adaptateurs de room.

Pour une clé brute, `pe-solution-report --decode-key 0x...` indique explicitement
si elle est décodable. Les clés d'infoset Monker/FNV sont des hash et ne peuvent pas
être inversées en board sans métadonnée ; un champ packé n'est décodé que si son
shift et son nombre de cartes sont fournis avec le format documenté.

Les dumps Monker peuvent être audités sans les importer dans le solveur :

```sh
pe-monker-validate --tree spot.tree --mkr run.mkr --json validation.json
```

Le validateur contrôle la topologie, le binding des slots, les ranges, le nombre de
classes et `iscount`. Le codec PLO4 est marqué `exact-plo4`; Hold'em, PLO5 et PLO6
sont marqués `structural-only-reference-required` tant qu'un dump de référence
indépendant n'a pas fourni leur table de classes Monker.

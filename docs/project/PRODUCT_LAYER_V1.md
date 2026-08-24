# Couche produit v1 : viewer, trainer et rapports

Cette tranche fournit une sortie exploitable sans modifier le format binaire `.pe_sol`.
Les métadonnées de spot restent séparées afin de préserver la compatibilité avec les
solutions déjà produites.

## Rapport agrégé

Le sidecar CSV utilise quatre colonnes :

```csv
key,street,board,weight
0x1234,flop,AsKd7c,1.0
0x4567,flop,AsKd7c,0.75
```

La commande suivante produit un JSON versionné et une page HTML autonome :

```sh
pe-solution-report \
  --solution solve.pe_sol \
  --metadata spots.csv \
  --json report.json \
  --html report.html
```

Le JSON contient, pour chaque groupe `street/board`, le nombre d'infosets, le poids,
l'entropie en bits et la fréquence agrégée de chaque action. Sans sidecar, les lignes
sont regroupées sous `unknown/unknown` : le programme ne prétend pas décoder un key
Monker dont le codec n'est pas fourni.

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
poker-eval-trainer --solution solve.pe_sol --labels labels.csv --rounds 20
```

Les libellés sont une vue produit ; la stratégie quantifiée reste la source de vérité.
Quand `next_key` est présent, le trainer suit la transition choisie vers le prochain
infoset au lieu de tirer un spot indépendant. Le manifeste porte donc la street, le
board et le chemin de décision ; les transitions automatiques depuis un arbre Monker
restent à relier au trainer.

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

Pour une clé brute, `pe-solution-report --decode-key 0x...` indique explicitement
si elle est décodable. Les clés d'infoset Monker/FNV sont des hash et ne peuvent pas
être inversées en board sans métadonnée ; un champ packé n'est décodé que si son
shift et son nombre de cartes sont fournis avec le format documenté.

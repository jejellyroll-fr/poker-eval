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

## Trainer avec libellés

Le trainer conserve le mode interactif existant et accepte un fichier de labels :

```csv
key,action,label
0x1234,0,fold
0x1234,1,call
```

```sh
poker-eval-trainer --solution solve.pe_sol --labels labels.csv --rounds 20
```

Les libellés sont une vue produit ; la stratégie quantifiée reste la source de vérité.
Les cartes, les transitions multi-rues et le replay d'une main ne sont pas encore
simulés par ce trainer v1.

## Lane B préflop

La façade solver accepte désormais un `pe_external_game_t` via
`pe_solver_deps_t::external_game`. Les presets `external-mccfr` et `outcome-mccfr`
utilisent ce chemin ; le proxy de stratégie relit les regrets du storage à chaque
visite, et les deals/chances sont fournis par l'adapter du jeu avec leur ratio
d'importance. C'est l'interface d'exécution scalable ; l'adapter Hold'em/PLO avec
ranges corrélées et card removal reste le prochain travail de variante.

# Plan d'implémentation — simulations multi-variantes

Statut : plan d'exécution proposé  
Date : 24 août 2026  
Point de départ : `1637e55f` (`feat(monker): assemble spot comparison from CLI`)

## 1. Décision d'architecture

L'objectif n'est pas de construire un moteur complet distinct pour chaque variante.
Il faut construire :

1. un noyau générique pour l'état, les mises, les tours de parole, les nœuds de
   chance et les terminaux ;
2. des modules de règles compacts pour les différences entre variantes ;
3. des adaptateurs de formats externes, dont Monker, qui traduisent vers ce
   modèle sans introduire leurs conventions dans le domaine ;
4. une seule chaîne vectorielle pour résoudre, simuler et mesurer
   l'exploitabilité avec les mêmes règles.

Le prochain chantier prioritaire est donc le noyau de règles générique. Ajouter
directement PLO5/PLO6 au chemin historique multiplierait les cas particuliers et
ne résoudrait pas l'écart de sémantique déjà observé sur un arbre all-in.

## 2. Ce que signifie « n'importe quel game »

Le terme doit être découpé en niveaux réalistes. Les jeux à cartes communes et
les jeux de type Stud/Draw n'ont pas le même protocole de distribution ni la
même structure d'information.

### Niveau A — cible du présent plan

- No-Limit Hold'em high ;
- Pot-Limit Omaha high à 4, 5 et 6 cartes privées ;
- heads-up et multiway ;
- préflop, flop, turn et river ;
- boards fixés ou runouts exactement énumérés ;
- ranges pondérées, card removal et mains impossibles ;
- fold, check, call, bet, raise, min-raise et all-in ;
- résolution CFR, évaluation d'une stratégie importée et comparaison par
  exploitabilité ;
- import d'arbres et de stratégies Monker pour les variantes dont le codec de
  classes est vérifié.

### Niveau B — extension sans changement du noyau

- Short Deck Hold'em ;
- Omaha Hi/Lo ;
- autres variantes à cartes communes dont les streets restent compatibles avec
  le protocole flop/turn/river.

Ces variantes doivent seulement fournir leur paquet, leurs règles de validité et
leur évaluateur de showdown.

### Niveau C — extension après généralisation du protocole de distribution

- Stud et Razz ;
- jeux de Draw ;
- jeux mixtes et changements de variante entre les mains.

Ils nécessitent des cartes privées/publiques distribuées en plusieurs étapes et,
pour le Draw, des décisions de remplacement. Ils ne doivent pas bloquer le
niveau A, mais l'API du noyau ne doit pas les rendre impossibles.

## 3. État réel au point de départ

### Déjà opérationnel

- lecture des `.tree` Monker, topologie, tailles, stacks et métadonnées ;
- lecture des archives `.mkr` et de leurs stratégies ;
- correspondance slot → nœud et octet → fréquence ;
- numérotation exacte des 16 432 classes PLO4, vérifiée sur les 270 725 mains ;
- exposition d'une stratégie importée sous forme de vecteurs par combo ;
- API générique `pe_vector_game_t` avec chance, informations, stratégie,
  transitions et valeurs terminales ;
- meilleure réponse à information imparfaite et exploitabilité vectorielle via
  `pe_exploitability_vector` ;
- CLI assemblant `.tree`, `.mkr`, board, ranges et paramètres du spot ;
- validation stricte des configurations dont la sémantique ne peut pas être
  garantie.

### Encore transitoire

- l'application des actions de poker vit principalement dans
  `multiway_postflop_adapter.c` ;
- le chemin CLI Monker utilise encore le moteur multiway historique pour une
  partie de la comparaison ;
- les indices d'action d'un fichier externe peuvent être confondus avec les
  actions internes si leur signification n'est pas explicitement traduite ;
- le test synthétique all-in a révélé un désaccord de topologie/sémantique et est
  correctement rejeté, mais ce cas n'est pas encore représentable de bout en
  bout ;
- Hold'em, PLO5 et PLO6 ne possèdent pas encore de codec Monker vérifié comme
  celui de PLO4 ;
- il manque un objet concret qui branche un spot poker réel sur
  `pe_vector_game_t` sans passer par l'adaptateur historique.

### Conséquence

Le CLI actuel est un pont d'intégration utile, pas encore le moteur universel.
La prochaine preuve importante n'est pas une option CLI supplémentaire : c'est
une même partie qui produit les mêmes transitions et valeurs dans les chemins
scalaire, vectoriel et importé.

## 4. Architecture cible

### 4.1 Séparation des responsabilités

```text
Entrées externes
  ├── CLI / manifeste JSON
  ├── Monker .tree
  └── Monker .mkr
           │ traduction + validation
           ▼
Description de partie immuable
  ├── variante et paquet
  ├── joueurs, stacks, pot, rake
  ├── board et ranges
  └── arbre de tailles / actions sémantiques
           │
           ▼
Noyau de règles générique
  ├── état de mise
  ├── transitions d'action
  ├── distribution / chance
  ├── clés d'information
  └── terminaux / side pots
           │
           ▼
Module de variante
  ├── validation des cartes
  ├── énumération des combos
  ├── évaluation showdown
  └── codec externe éventuel
           │
           ▼
pe_vector_game_t
  ├── solveur
  ├── simulation de politique
  └── exploitabilité à information imparfaite
```

### 4.2 Types de domaine proposés

Les noms exacts pourront évoluer, mais les frontières suivantes doivent rester
stables.

#### `pe_game_spec_t`

Description immuable d'un spot :

- identifiant de variante ;
- nombre de joueurs et bouton/position ;
- stacks, contributions initiales et pot mort ;
- structure de blinds/antes ;
- street initiale et board connu ;
- ranges privées pondérées ;
- rake/cap ;
- arbre d'actions sémantiques ;
- politique de runout et précision numérique.

#### `pe_game_state_t`

État dynamique d'un nœud :

- joueurs actifs et all-in ;
- joueur à parler ;
- stacks, contributions de street et contributions totales ;
- montant à suivre, dernière relance complète et nombre de relances ;
- street et cartes distribuées ;
- nœud courant de l'arbre de tailles ;
- pots principal et secondaires dérivables ;
- drapeaux terminal/chance.

L'état ne contient ni stratégie ni convention Monker.

#### `pe_action_t`

Une action doit porter une signification, pas seulement un indice :

```c
typedef enum {
    PE_ACTION_FOLD,
    PE_ACTION_CHECK,
    PE_ACTION_CALL,
    PE_ACTION_BET,
    PE_ACTION_RAISE,
    PE_ACTION_ALL_IN
} pe_action_kind_t;

typedef enum {
    PE_AMOUNT_NONE,
    PE_AMOUNT_CHIPS,
    PE_AMOUNT_POT_FRACTION,
    PE_AMOUNT_MINIMUM,
    PE_AMOUNT_MAXIMUM,
    PE_AMOUNT_AUTO
} pe_amount_kind_t;
```

La structure associe le type d'action, le mode de montant et sa valeur. Les
indices locaux d'un arbre externe sont traduits une seule fois en
`pe_action_t`. Les transitions internes ne doivent jamais deviner la
signification d'un entier.

#### `pe_variant_ops_t`

Interface fournie par chaque famille de variantes :

- description du paquet et nombre de cartes privées ;
- validation du board et des cartes mortes ;
- énumération/indexation des combos internes ;
- filtrage par card removal ;
- évaluation high/low au showdown ;
- protocole de distribution de la prochaine street ;
- sérialisation lisible d'une main pour les diagnostics.

Le codec de classes Monker est une interface d'import distincte. Il traduit une
main interne vers un slot externe, mais ne définit pas l'ordre interne du
solveur.

#### `pe_betting_rules_t`

Paramètres communs du moteur de mise :

- no-limit, pot-limit ou fixed-limit ;
- calcul du minimum de relance ;
- plafond de relances ;
- réouverture de l'action après un short all-in ;
- arrondi des montants ;
- rake et cap ;
- règle de terminaison de la street.

### 4.3 Invariants obligatoires

Après chaque transition :

- aucune stack ni contribution n'est négative ;
- la conservation des jetons est exacte à la tolérance configurée ;
- `to_call` est au moins la contribution maximale de street ;
- un joueur foldé ou all-in ne redevient jamais acteur ;
- le joueur à parler possède au moins une action légale ;
- un all-in engage exactement la stack restante ;
- un call n'engage jamais plus que la stack restante ;
- une relance ne réouvre l'action que si les règles le permettent ;
- un nœud terminal n'a ni acteur ni action ;
- un nœud de chance n'a pas d'action de joueur ;
- la somme des poids de chance valides est normalisable et non nulle ;
- une carte connue ou privée ne peut pas être distribuée à nouveau ;
- les clés d'information n'exposent aucune carte privée adverse.

Ces invariants doivent être vérifiables en build de test et lors de la lecture
d'un format externe.

## 5. Lots d'implémentation

Chaque lot doit se terminer par un commit autonome, des tests ciblés et une mise
à jour du présent document.

### UGS-00 — Figer les contrats et les oracles

Objectif : empêcher qu'une refactorisation valide seulement sa propre sortie.

Travaux :

- documenter les conventions de montants : chips ajoutés, mise totale de street
  ou relance au-dessus du call ;
- capturer des traces de nœuds réels Monker : acteur, actions affichées, montants,
  prochain nœud et terminal ;
- construire de petits jeux analytiques indépendants du moteur historique ;
- conserver les hashes/tableaux de référence PLO4 existants ;
- définir le schéma d'un « trace oracle » sérialisable pour rejouer une ligne.

Tests de sortie :

- fold terminal heads-up ;
- check/check river ;
- bet/call, bet/fold et raise/call ;
- all-in inférieur au call ;
- short all-in qui ne réouvre pas l'action ;
- multiway avec un joueur all-in et deux joueurs encore actifs.

Critère de fin : chaque trace possède un résultat attendu produit soit
analytiquement, soit par une source externe vérifiée.

Commit conseillé : `test(rules): freeze action and terminal oracles`.

### UGS-01 — Normaliser la sémantique des actions

Objectif : rendre impossible la confusion entre un index d'action et sa
signification poker.

Travaux :

- introduire `pe_action_t` et les politiques de montant ;
- traduire les actions du `.tree` vers cette représentation au chargement ;
- centraliser le calcul de call, bet, raise, min-raise, pot-limit et all-in ;
- représenter explicitement `auto`, `minimum` et `maximum` ;
- valider les branches impossibles avant simulation ;
- conserver temporairement un adaptateur vers les constantes `MPF_ACTION_*`.

Tests de sortie :

- mêmes actions légales quelle que soit leur position dans un tableau ;
- permutation des indices externes sans modification de la partie ;
- rejet d'un montant ambigu ;
- résolution de la fixture all-in aujourd'hui rejetée ;
- mutations : confondre call/all-in, ignorer un min-raise ou inverser deux
  actions doit faire échouer les tests.

Critère de fin : aucune transition du nouveau code ne branche directement sur
un entier provenant d'un fichier Monker.

Commit conseillé : `feat(rules): make tree actions semantic and terminal-safe`.

### UGS-02 — Extraire le noyau d'état et de mise

Objectif : sortir les règles communes de `multiway_postflop_adapter.c`.

Travaux :

- créer `pe_game_spec_t`, `pe_game_state_t` et `pe_betting_rules_t` ;
- extraire l'initialisation du spot ;
- extraire `legal_actions` et `apply_action` ;
- extraire la détection de fin de tour et de fin de main ;
- gérer les all-ins, les side pots et la conservation des jetons ;
- rendre les transitions pures du point de vue du solveur : état source immuable,
  état destination explicite ;
- maintenir un adaptateur de compatibilité MPF pendant la migration.

Tests de sortie :

- tests unitaires de chaque invariant de la section 4.3 ;
- tests de propriété sur des séquences d'actions aléatoires légales ;
- parité nœud par nœud avec les cas historiques actuellement supportés ;
- mémoire : aucune fuite lors de la création/destruction répétée d'états.

Critère de fin : le noyau de mise ne dépend ni d'un évaluateur de cartes, ni de
Monker, ni du solveur CFR.

Commit conseillé : `feat(rules): extract generic betting state machine`.

### UGS-03 — Brancher un vrai jeu poker sur `pe_vector_game_t`

Objectif : utiliser le moteur générique d'exploitabilité sans passer par le
chemin multiway historique.

Travaux :

- implémenter les callbacks terminal, acteur, actions et transitions ;
- exposer les nœuds de chance avec poids exacts ;
- générer des clés d'information stables, indépendantes des cartes adverses ;
- propager les vecteurs de reach par combo ;
- calculer les valeurs terminales par joueur et par combo ;
- brancher les stratégies internes et importées via la même callback ;
- router la comparaison finale vers `pe_exploitability_vector`.

Point de contrôle essentiel : `cfr_exploitability_multiway` peut rester comme
oracle de migration, mais le résultat public final doit provenir de la meilleure
réponse vectorielle à information imparfaite. Une meilleure réponse qui voit les
mains adverses mesure une borne de clairvoyance, pas l'exploitabilité poker.

Tests de sortie :

- parité des valeurs de politique entre traversée scalaire et vectorielle ;
- jeux à stratégie pure, mixte et indifférente ;
- mutation d'une carte privée adverse sans changement de la clé d'information ;
- test qui distingue explicitement meilleure réponse légale et meilleure réponse
  parfaite-information ;
- comparaison de deux politiques identiques donnant un écart nul à la tolérance.

Critère de fin : un spot poker réel atteint `pe_exploitability_vector` et produit
une métrique finie sans callback spécifique Monker.

Commit conseillé : `feat(rules): adapt generic poker states to vector games`.

### UGS-04 — Hold'em high de bout en bout

Objectif : première variante complète sur le nouveau noyau.

Travaux :

- module Hold'em : 2 cartes privées, paquet de 52 cartes, évaluateur high ;
- énumération exacte des 1 326 combos et card removal ;
- ranges pondérées et normalisation ;
- chance exacte flop/turn/river ;
- terminaux fold et showdown heads-up/multiway ;
- support des boards partiels et fixés ;
- validation du rake et des side pots.

Tests de sortie :

- river range contre range par énumération exhaustive ;
- turn puis flop avec somme des probabilités de runout égale à 1 ;
- symétrie des couleurs et invariance par permutation des suits ;
- multiway avec dead cards et ranges qui se chevauchent ;
- conservation zéro-somme avant rake, puis somme égale au rake après rake ;
- parité CPU avec les évaluateurs existants.

Critère de fin : le CLI peut résoudre et évaluer un spot Hold'em sans données
Monker.

Commit conseillé : `feat(games): run holdem through the generic vector kernel`.

### UGS-05 — Généraliser la famille Omaha high

Objectif : PLO4, PLO5 et PLO6 partagent une implémentation paramétrée.

Travaux :

- module Omaha high paramétré par 4, 5 ou 6 cartes privées ;
- règle exacte « 2 cartes privées + 3 cartes du board » ;
- indexation interne des combos pour chaque taille ;
- suppression des doublons et card removal ;
- évaluation terminale vectorisée ou mise en cache ;
- limites explicites de taille/mémoire avant allocation.

Ordres de grandeur à traiter comme contraintes de conception :

- PLO4 : `C(52,4) = 270 725` combos bruts ;
- PLO5 : `C(52,5) = 2 598 960` combos bruts ;
- PLO6 : `C(52,6) = 20 358 520` combos bruts.

Il n'est pas acceptable d'allouer systématiquement un vecteur dense PLO6 par
nœud. Le stockage doit être sparse ou limité aux combos présents dans les
ranges, avec un mapping dense local au spot.

Tests de sortie :

- évaluation contre une implémentation exhaustive indépendante ;
- cas où utiliser 1 ou 3 cartes privées produirait à tort une meilleure main ;
- mains partageant des cartes avec le board/range adverse ;
- parité PLO4 avec les fixtures existantes ;
- budgets mémoire et temps vérifiés sur ranges réalistes PLO5/PLO6.

Critère de fin : les trois variantes passent par les mêmes transitions et ne
diffèrent que par le module de variante et les combos actifs.

Commit conseillé : `feat(games): parameterize omaha high for four to six cards`.

### UGS-06 — Streets, chance et multiway complets

Objectif : garantir que toutes les rues et topologies du niveau A utilisent les
mêmes règles.

Travaux :

- unifier les transitions préflop → flop → turn → river → showdown ;
- énumérer exactement les cartes communes compatibles ;
- supporter un runout fixé, partiellement fixé ou libre ;
- supporter les joueurs foldés/all-in pendant les streets suivantes ;
- construire les pots principal et secondaires au terminal ;
- appliquer le rake une seule fois, selon une politique testable ;
- borner ou expliciter tout mode Monte-Carlo, sans le confondre avec l'exact.

Tests de sortie :

- sommes des poids de chance sur chaque street ;
- aucune collision de cartes sur une traversée complète ;
- trois joueurs avec deux niveaux d'all-in ;
- quatre joueurs, folds intermédiaires et showdown partiel ;
- exact et Monte-Carlo convergent sur de petits espaces où l'exact est connu.

Critère de fin : aucune branche spéciale « river seulement » n'est requise pour
le niveau A.

Commit conseillé : `feat(games): complete shared streets chance and side pots`.

### UGS-07 — Compléter les codecs Monker

Objectif : importer une stratégie Monker pour chaque variante du niveau A sans
inférer sa numérotation.

Travaux :

- extraire par réflexion ou oracle équivalent la table Hold'em ;
- extraire les tables PLO5 et PLO6 ;
- vérifier chaque entrée, pas seulement le nombre de classes ;
- enregistrer un hash de table et des métadonnées de version ;
- vérifier que le nombre de fréquences d'un nœud correspond au nombre d'actions
  et de classes attendu ;
- refuser une version/variante inconnue plutôt que tenter une heuristique ;
- rendre la stratégie importée indépendante de l'ordre interne des combos.

Tests de sortie :

- bijection complète contre le vrai MonkerSolver pour chaque codec ;
- mutations d'ordre de rang, couleur, lex/colex et canonicalisation ;
- import de nœuds à 2, 3 et plus d'actions ;
- nœuds byte-array et int-array ;
- détection d'une archive tronquée ou d'une variante incompatible.

Critère de fin : le CLI annonce explicitement le codec/version vérifié et ne
produit aucune stratégie si cette preuve manque.

Commit conseillé : `feat(monker): verify hand codecs for holdem plo5 and plo6`.

### UGS-08 — Intégration Monker réelle et CLI stable

Objectif : transformer le pont CLI en outil reproductible de simulation et de
comparaison.

Travaux :

- définir un manifeste JSON versionné comme entrée canonique ;
- accepter `.tree`, `.mkr`, board, ranges, stacks et paramètres en arguments ou
  via manifeste ;
- afficher la configuration normalisée avant calcul ;
- ajouter `--validate-only`, `--trace-node`, `--exact` et un budget explicite
  pour Monte-Carlo ;
- produire un résultat JSON stable : valeur de politique, BR par joueur, gaps,
  exploitabilité, convergence, temps et compte de nœuds ;
- inclure les hashes des entrées et le codec utilisé ;
- tester plusieurs exports Monker réels, pas seulement des ZIP synthétiques ;
- comparer les valeurs/EV Monker lorsque l'export les expose, puis comparer
  l'exploitabilité calculée localement.

Exemple de cible :

```sh
mpf_run_with_metrics \
  --manifest spot.json \
  --tree solve.tree \
  --strategy solve.mkr \
  --exact \
  --output result.json
```

Critère de fin : une commande reproductible importe un spot réel supporté,
valide sa sémantique, exécute la politique et calcule son exploitabilité via le
moteur vectoriel.

Commit conseillé : `feat(cli): run reproducible multi-game simulations`.

### UGS-09 — Niveau B : Short Deck et Omaha Hi/Lo

Objectif : prouver que le noyau est réellement extensible.

Travaux :

- paquet réduit et ordre des mains configurable pour Short Deck ;
- valeurs high/low séparées et partage du pot pour Omaha Hi/Lo ;
- règles de qualification du low ;
- composition avec side pots et rake ;
- fixtures analytiques pour scoop, chop et quartering.

Critère de fin : aucune modification du moteur de mise ou de
`pe_vector_game_t` n'est nécessaire pour ajouter ces variantes.

### UGS-10 — Niveau C : protocole Stud/Draw

Objectif : étendre la distribution et les observations, sans réécrire le
solveur.

Travaux :

- événement de distribution générique : privé, public, upcard, downcard ;
- observation propre à chaque joueur ;
- action de draw avec masque de remplacement ;
- ordre de parole dérivé des cartes visibles si la variante l'exige ;
- changements de limites/variante pour les mixed games.

Ce lot reste hors du chemin critique du niveau A.

## 6. Ordre de dépendance

| Lot | Dépend de | Débloque |
|---|---|---|
| UGS-00 Oracles | état actuel | toutes les migrations sûres |
| UGS-01 Actions sémantiques | UGS-00 | all-in fiable, import externe fiable |
| UGS-02 Noyau de mise | UGS-01 | adaptateur générique |
| UGS-03 Jeu vectoriel | UGS-02 | exploitabilité poker correcte |
| UGS-04 Hold'em | UGS-03 | première simulation complète |
| UGS-05 Omaha 4/5/6 | UGS-03 | simulations PLO génériques |
| UGS-06 Streets/multiway | UGS-04, UGS-05 | niveau A complet |
| UGS-07 Codecs Monker | UGS-04, UGS-05 | import multi-variantes |
| UGS-08 CLI réel | UGS-06, UGS-07 | workflow utilisateur reproductible |
| UGS-09 Niveau B | UGS-06 | variantes communautaires avancées |
| UGS-10 Niveau C | UGS-03 | Stud/Draw/mixed games |

UGS-04 et UGS-05 peuvent avancer en parallèle après UGS-03. UGS-07 peut
commencer plus tôt comme travail d'investigation, mais ne doit pas définir le
modèle interne.

## 7. Stratégie de tests et portes de qualité

### Porte A — vérité des transitions

- traces analytiques et Monker rejouées nœud par nœud ;
- invariants vérifiés après chaque action ;
- mutations sémantiques obligatoirement détectées ;
- aucune désactivation silencieuse de l'arbre en cas de branche absente.

### Porte B — vérité des cartes

- énumération exhaustive sur espaces réduits ;
- parité des évaluateurs CPU existants ;
- card removal vérifié pour chaque joueur et chaque runout ;
- tests de permutation des couleurs et des sièges lorsque la symétrie
  s'applique.

### Porte C — vérité de l'information

- une infoset key ne dépend que des observations légales ;
- les actions BR d'un même infoset sont agrégées sur toutes les mains adverses ;
- test négatif de fuite perfect-information ;
- convergence et tolérance publiées avec le résultat.

### Porte D — vérité de l'import

- classes de mains vérifiées intégralement contre Monker ;
- dimensions stratégie × actions × classes vérifiées à chaque nœud ;
- ordre d'actions vérifié par traces, jamais supposé ;
- archive/version inconnue rejetée avec un diagnostic précis.

### Porte E — non-régression

- tests ciblés à chaque commit ;
- suite complète sans exécutions concurrentes sur la même machine ;
- sanitizers sur les nouveaux états et vecteurs ;
- benchmark mémoire/temps versionné pour Hold'em, PLO4, PLO5 et PLO6 ;
- aucune régression silencieuse vers un vecteur dense de toutes les mains PLO6.

## 8. Risques principaux et réponses

| Risque | Impact | Réponse prévue |
|---|---|---|
| Convention de montant ambiguë | arbre différent de Monker | actions sémantiques et traces oracles |
| Ordre d'actions différent | stratégie appliquée à la mauvaise branche | mapping explicite par nœud |
| Numérotation de mains seulement « plausible » | stratégie presque entièrement permutée | extraction et comparaison exhaustive |
| Fuite d'information dans la BR | exploitabilité artificiellement haute | chemin vectoriel à infosets et test négatif |
| Explosion mémoire PLO5/PLO6 | simulation impossible | indices locaux sparse par ranges actives |
| Side pots incorrects | EV faux en multiway | ledger de contributions et oracles analytiques |
| Chance mal normalisée | EV biaisé | poids exacts et somme contrôlée à chaque nœud |
| Format Monker changeant | import silencieusement faux | version/hash et rejet par défaut |
| Couplage au moteur historique | variantes coûteuses à ajouter | extraction du noyau avant extension |
| Tests auto-référentiels | faux sentiment de correction | oracles indépendants et mutations |

## 9. Jalons livrables

### Jalon M0 — Actions vraies

UGS-00 et UGS-01 terminés. La fixture all-in est acceptée et sa ligne est
rejouée exactement.

### Jalon M1 — Noyau universel

UGS-02 et UGS-03 terminés. Un jeu poker concret utilise
`pe_exploitability_vector` sans dépendre des transitions MPF historiques.

### Jalon M2 — Hold'em complet

UGS-04 et la partie Hold'em d'UGS-06 terminées. Simulation exacte de toute
street, heads-up et multiway.

### Jalon M3 — Famille PLO complète

UGS-05 et la partie PLO d'UGS-06 terminées. PLO4/5/6 fonctionnent avec stockage
borné par les ranges actives.

### Jalon M4 — Comparaison Monker multi-variantes

UGS-07 et UGS-08 terminés. Un manifeste suffit pour importer et mesurer des
spots réels Hold'em/PLO supportés.

### Jalon M5 — Preuve d'extension

UGS-09 terminé sans changement du noyau de mise ni de l'interface vectorielle.

## 10. Les trois prochains commits recommandés

1. `test(rules): freeze action and terminal oracles`
   - ajoute les traces minimales, dont le cas all-in actuellement bloquant ;
   - fixe les conventions de montant et les terminaux attendus.
2. `feat(rules): make tree actions semantic and terminal-safe`
   - introduit `pe_action_t` ;
   - traduit les actions Monker ;
   - élimine la dépendance directe aux indices externes.
3. `feat(rules): extract generic betting state machine`
   - extrait l'état et les transitions communes ;
   - maintient un shim MPF pour mesurer la parité pendant la migration.

Ce séquencement est préférable à l'ajout immédiat du codec PLO5 : il corrige le
dernier désaccord sémantique connu et fournit la base sur laquelle tous les
codecs et variantes seront effectivement utilisables.

## 11. Définition de fini pour le niveau A

Le niveau A est terminé seulement si toutes les affirmations suivantes sont
vraies :

- [ ] Hold'em, PLO4, PLO5 et PLO6 utilisent le même noyau d'état et de mise ;
- [ ] chaque action externe est traduite en action sémantique validée ;
- [ ] toutes les streets et tous les runouts peuvent être énumérés exactement ;
- [ ] heads-up et multiway gèrent folds, all-ins, side pots et rake ;
- [ ] les ranges pondérées et le card removal sont appliqués avant allocation ;
- [ ] le stockage PLO5/PLO6 dépend des combos actifs, pas de l'univers complet
  par nœud ;
- [ ] les valeurs terminales sont vérifiées par un oracle indépendant ;
- [ ] l'exploitabilité finale est calculée à information imparfaite ;
- [ ] les codecs Monker de chaque variante annoncée sont vérifiés intégralement ;
- [ ] au moins un export réel par variante traverse import, validation,
  simulation et comparaison ;
- [ ] les résultats JSON identifient entrées, règles, codec, convergence et
  précision ;
- [ ] la suite complète et les sanitizers passent ;
- [ ] les limites de temps et mémoire sont documentées et testées.

## 12. Conclusion

La chaîne Monker PLO4 a levé le verrou de l'import de stratégie. Le verrou
suivant est bien plus fondamental : garantir que l'arbre importé et le moteur
local parlent exactement le même langage d'actions, puis exposer ces règles au
moteur vectoriel à information imparfaite.

Une fois UGS-01 à UGS-03 terminés, ajouter Hold'em ou PLO5/PLO6 devient un travail
de module de cartes, d'évaluateur et de codec. Avant cela, chaque nouvelle
variante amplifierait les hypothèses du chemin historique. Le plan privilégie
donc d'abord la vérité sémantique, puis la généralisation, puis l'élargissement
des formats et variantes.

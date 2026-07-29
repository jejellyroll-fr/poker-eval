# Nomenclature PLO (Pot-Limit Omaha)

## Vue d'ensemble

Dans les cercles de Pot-Limit Omaha (PLO), les coachs, les solveurs et les forums ont convergé vers une **nomenclature quasi universelle** pour décrire rapidement les mains de départ et les _ranges_.

## Conventions de notation

### 1. Initiales des rangs
- **A-K-Q-J-T-9…2** : Similaires au Hold'em mais listées sur quatre cartes
- Exemples : `AAKQ`, `JT98`, `8765`

### 2. Suffixes de "suitedness"
Ces suffixes indiquent instantanément le potentiel de couleur :

| Suffixe | Signification | Exemple texte | Commentaire rapide |
|---------|---------------|---------------|-------------------|
| **ds** | _double-suited_ (2 couples de couleurs) | `AAKKds` | « Nuts potentiel » : deux couleurs max |
| **ss** | _single-suited_ (une seule paire assortie) | `AQT9ss` | Un seul tirage couleur |
| **r / rainbow** | quatre couleurs différentes | `AKJ7r` | Zéro tirage couleur |

### 3. Place-holders
- **x** ou **xx** : Cartes indifférentes
- Exemple : `AAxx` désigne toute main avec une paire d'As

### 4. Abréviations de structure
- **rundown** : Cartes consécutives (ex: `JT98`)
- **1-gap** : Une carte manquante dans la séquence (ex: `JT86`)
- **double-paired** : Deux paires (ex: `KKQQ`)

### 5. Syntaxe formelle (ProPokerTools / MonkerSolver)
Utilise les opérateurs `, : ! > <` et des macros comme `$ds`, `$0g`, etc., pour filtrer ou combiner des sous-ranges dans les solveurs.

## Les 21 catégories de départ (PLO 4 cartes)

| Bloc | Catégorie | % du deck* | Exemples de main | Idée stratégique pré-flop (100 bb, 6-max) |
|------|-----------|------------|------------------|-------------------------------------------|
| **A — Unpaired** | | | | |
| | 1. Double-suited (DS) | 9,5 % | T♠9♠8♥7♥ / J♠9♠8♦2♣ / K♥9♦6♠3♣ | Main « pure » : on cherche la connectivité. DS = RFI IP ; SS plutôt call ; RB devient souvent fold OOP |
| | 2. Single-suited (SS) | 26,9 % | | |
| | 3. Rainbow (RB) | 6,3 % | | |
| **B — One-Pair (xxyy)** | | | | |
| | 4. Pair DS | 3,6 % | K♠K♦9♠4♦ | 3-bet IP ou VS cold-call ; valeur d'abattage + bloqueurs |
| | 5. Pair SS | 20,6 % | Q♠Q♦T♥6♣ | RFI late ; fold OOP vs 3-bet sans side cards |
| | 6. Pair RB | 23,8 % | J♦J♣8♠3♥ | Défense BB vs open, sinon trop fragile |
| **C — Two-Pair (xxyy)** | | | | |
| | 7. 2-Pair DS | 1,1 % | J♠J♦9♠9♦ / K♠K♦Q♥Q♣ | DS = gros 3-bet-fold blocker, SS/RB plus situationnel |
| | 8. 2-Pair SS | 10,0 % | | |
| | 9. 2-Pair RB | 0,8 % | | |
| **D — Trips (xxx y)** | | | | |
| | 10. Trips DS | 0,18 % | Q♠Q♦Q♥9♠ | Souvent limp/raise SB ou 3-bet IP ; faible jouabilité post-flop |
| | 11. Trips SS | 1,6 % | | |
| | 12. Trips RB | 0,13 % | | |
| **E — Aces (AA xy)** | | | | |
| | 13. AA DS | 0,45 % | A♠A♦K♠J♦ / A♠A♥9♦5♣ | Toujours 4-bet ≤ 100 bb (sauf spots très tight) |
| | 14. AA SS | 3,9 % | | |
| | 15. AA RB | 0,3 % | | |
| **F — Broadway-heavy** | | | | |
| | 16. 3 Broadway DS | 2,2 % | K♠Q♠J♦T♦ / Q♠J♥T♥8♣ | Très bon IP : nut-straights + blocker equity |
| | 17. 3 Broadway SS | 7,7 % | | |
| | 18. 3 Broadway RB | 0,7 % | | |
| **G — Ragged / Low** | | | | |
| | 19. Ragged DS | 2,6 % | 9♠6♠4♦2♥ / 8♠5♣3♦2♣ | Levier d'open-limp deep-stack ; sinon fold majoritaire |
| | 20. Ragged SS | 7,0 % | | |
| | 21. Ragged RB | 2,0 % | | |

*Les pourcentages sont approximatifs et peuvent varier selon les sources.

## Sources et popularisation

Ces conventions ont été popularisées par :
- ProPokerTools
- PLO Mastermind
- Sites pédagogiques : PokerVIP, PokerStrategy, Adda52, 888poker

Elles constituent aujourd'hui la « langue commune » des fiches de ranges préflop et des scripts d'étude en PLO.
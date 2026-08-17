# Cartographie & Taxonomie Complète des 20 Issues Ouvertes

Ce document structure l'ensemble des **20 issues actuellement ouvertes** du dépôt `jejellyroll-fr/poker-eval`.

---

## 1. Vue d'Ensemble des 4 Couches

Les issues sont réparties en 4 couches d'ingénierie et de théorie des jeux :

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ COUCHE 1 — Qualification Mathématique du Solver (Track A)                    │
│ #165 (Oracles/OpenSpiel/Gambit) ──► #168 (Audit BR) ──► #171 (Multiway CCE)  │
│                                 ├─► #166 (Bet Sizing)                        │
│                                 └─► #170 (Generic Utility Abstraction)       │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│ COUCHE 2 — Construction du Jeu, Chance & Abstractions (Track B)              │
│ #150 (Card Bunching & Non-Uniform Chance)                                    │
│ #151 (Stud/Draw/Pineapple Abstraction)                                       │
│ #152 (Custom Rake Engine)                                                    │
│ #153 (Multiway ICM Engine via #170)                                          │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│ COUCHE 3 — Évaluateurs, Variantes & Combinatoire (Track C - Parallèle)      │
│ #159 (Deck Abstraction) ──► #160 (Ranking Rules) ──► #158 (Distributions) ──►│
│ #157 (Draw Optimizer)   ──► #161 (Wildcards)     ──► #162 (Transitions)   ──►│
│ #164 (New Game Variants)                                                     │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│ COUCHE 4 — Fonctionnalités Aval & Outils Utilisateur (Track D - Indépendant) │
│ #163 (Paytable EV & Casino)                                                  │
│ #167 (Bayesian Range Updates)                                                │
│ #169 (Bankroll & Kelly Criterion)                                            │
│ #154 (Interactive GTO Trainer)                                               │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Est-ce que les issues non citées doivent venir "en dernier" ?

**Non !** Il ne faut pas concevoir le projet comme une file d'attente unique linéaire (1 à 20), mais comme **3 tracks de développement indépendants pouvant avancer en parallèle** :

### 🚀 Track C : Évaluateurs & Combinatoire (Indépendant du Solver)
Vous pouvez développer ces issues **à tout moment** sans attendre la qualification du CFR :
- **#159** (Generalized Deck)
- **#160** (Configurable Rankings)
- **#158** (Hand Distributions)
- **#157** (Draw Decision & Equity Optimizer)
- **#161** (Wildcard Policies)
- **#162** (Board Transition Matrices)
- **#164** (Nouvelles variantes : Royal, Archie, Badugi, etc.)

> **Pourquoi ?** Un évaluateur valide les règles de poker et la combinatoire pure. Il alimentera ensuite les adapters de jeu.

---

### 🎲 Track B : Card Bunching & Modèle de Chance (#150)
Faire une PR pour **#150 (Card Bunching)** dès maintenant est **parfaitement justifié et cohérent** :
1. **Volet Combinatoire/Statistique** : Calculer la distribution de retrait des cartes $P(c \mid \text{folds})$ en fonction des ranges foldées est un problème purement probabiliste, 100% indépendant du solveur CFR.
2. **Volet Intégration Solver** : Introduire le support de probabilités de chance non-uniformes dans les adapters prépare l'infrastructure du jeu sans bloquer sur la convergence mathématique.

---

### 🧮 Track D : Outils Analytiques & Économiques (Indépendants)
Ces modules n'ont pas de dépendance bloquante sur le cœur CFR :
- **#163** (Paytable EV & Casino) : Ne dépend que des évaluateurs de combinaisons.
- **#169** (Bankroll / Kelly Criterion / Risk of Ruin) : Calculs mathématiques fermés basés sur l'EV/variance.
- **#167** (Bayesian Range Updates) : Règle de Bayes $P(\text{Hand} \mid \text{Action})$ sur les ranges.
- **#154** (GTO Trainer) : Outil interactif CLI/Python qui consomme les fichiers de solution `.pe_sol`.

---

## 3. Matrice Détaillée des 20 Issues Ouvertes

| Issue # | Titre & Thème | Couche | Dépendances | Peut-on la faire maintenant ? |
| :--- | :--- | :--- | :--- | :--- |
| **#150** | **FEAT-14 — Folded Range Card Bunching & Non-Uniform Chance** | Couche 2 | Indépendant (statistique) | **OUI** (PR en cours) |
| **#151** | **FEAT-15 — Multi-Game Abstraction (Stud/Draw/Pineapple)** | Couche 2 | #164, #157 | Partiellement (après évaluateurs) |
| **#152** | **FEAT-16 — Spot-Filtered Custom Rake Engine** | Couche 2 | #170 (idéalement) | **OUI** (adapter payoff) |
| **#153** | **FEAT-17 — Multiway Asymmetrical ICM Payoff Engine** | Couche 2 | #170 (`pe_utility_fn`) | Après #170 |
| **#154** | **FEAT-18 — Interactive GTO Trainer Tool** | Couche 4 | Format `.pe_sol` | **OUI** (consommateur UI/CLI) |
| **#157** | **ISSUE-01 — Generic Draw Decision & Equity Optimizer** | Couche 3 | Aucune | **OUI** |
| **#158** | **ISSUE-02 — Automated Hand Distribution Engine** | Couche 3 | #159 | **OUI** |
| **#159** | **ISSUE-03 — Generalized Deck Specification** | Couche 3 | Aucune | **OUI** (Fondation deck) |
| **#160** | **ISSUE-04 — Configurable Hand Ranking Rules** | Couche 3 | #159 | **OUI** |
| **#161** | **ISSUE-05 — Generalized Wildcard Policy** | Couche 3 | #159, #160 | **OUI** |
| **#162** | **ISSUE-06 — Board Transition & Hand Improvement Matrix** | Couche 3 | #158 | **OUI** |
| **#163** | **ISSUE-07 — Paytable EV & Casino / Video Poker Engine** | Couche 4 | #160 | **OUI** |
| **#164** | **ISSUE-08 — Implementation of Missing Game Variants** | Couche 3 | #159, #160 | **OUI** |
| **#165** | **ISSUE-09 — Analytical Game Benchmark Suite & Oracles** | Couche 1 | Aucune | **OUI** (Cœur critique P0) |
| **#166** | **ISSUE-10 — Optimal Bet Sizing Optimization** | Couche 1 | #165 | Après #165 |
| **#167** | **ISSUE-11 — Bayesian Range Update API** | Couche 4 | Range engine | **OUI** |
| **#168** | **ISSUE-12 — Audit Best Response & Exploitability Engine** | Couche 1 | #165 | Après #165 |
| **#169** | **ISSUE-13 — Bankroll, Risk of Ruin & Kelly Criterion** | Couche 4 | Aucune | **OUI** |
| **#170** | **ISSUE-14 — Generic Multi-Utility CFR Abstraction** | Couche 1 | #165 | **OUI** (Architecture core) |
| **#171** | **ISSUE-15 — Multiway Game Theory & Non-Zero-Sum Audit** | Couche 1 | #165, #168 | Après #168 |

---

## 4. Synthèse des Recommandations de Workflow

1. **Pour votre PR sur #150 (Card Bunching)** : Allez-y à 100%. L'estimateur probabiliste et le support de chance non-uniforme sont d'excellentes briques.
2. **Pour le Track Mathématique CFR** : Suivre l'enchaînement `#165` $\to$ `#168` $\to$ `#171` / `#166` / `#170` pour certifier le solver.
3. **Pour les Variantes et Outils** : Développez-les au gré des besoins, ils n'interfèrent pas avec le chemin critique mathématique.

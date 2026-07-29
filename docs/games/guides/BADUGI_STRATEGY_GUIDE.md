# Guide Stratégique Badugi avec pokenum

## 📖 Introduction

Ce guide explique comment utiliser `pokenum` pour analyser les stratégies d'échange de cartes en Badugi. Badugi est un jeu de draw lowball où l'objectif est de former la meilleure main de 4 cartes avec des rangs et couleurs uniques.

## 🎯 Règles Badugi Rappel

- **Objectif** : Main de 4 cartes, rangs et couleurs différents
- **Classement** : Plus bas = meilleur (A-2-3-4 est la nuts)
- **Hiérarchie** : 4-card > 3-card > 2-card > 1-card
- **Échange** : Jusqu'à 3 tours, chaque joueur peut échanger 0-4 cartes

## 🚀 Utilisation de base

```bash
# Format général
./pokenum -badugi <main_joueur1> - <main_joueur2> [options]

# Monte Carlo (recommandé pour mains incomplètes)
./pokenum -mc 100000 -badugi <cartes> - <cartes>
```

## 📊 Analyse Pré-Draw (1er tour)

### Scénario 1 : Évaluer une base de départ

```bash
# Vous avez A♠ 2♥, adversaire a K♠ Q♦
./pokenum -badugi As 2h - Ks Qd

# Résultat : As 2h gagne ~98% du temps
# Analyse : Excellente base, gardez et échangez 2 cartes
```

### Scénario 2 : Comparer différentes bases

```bash
# Base basse vs base moyenne
./pokenum -badugi As 3h - 7s 9d
# Résultat attendu : A-3 domine largement

# Deux bases moyennes
./pokenum -badugi 6s 8h - 7d 9c
# Résultat : Plus serré, 6-8 légèrement favori
```

### Scénario 3 : Mains avec duplicates

```bash
# Paire vs cartes distinctes
./pokenum -badugi As Ad - 6h 9c
# La paire doit échanger une carte, désavantage significatif

# Duplicate couleur vs rangs distincts
./pokenum -badugi As 2s - 7h 9d
# Le duplicate couleur est moins pénalisant qu'un duplicate rang
```

## 🔄 Analyse Post-Échange

### Après le 1er échange

```bash
# Vous aviez A♠ 2♥, avez tiré 3♦ J♣ (gardez A-2-3, défaussez J)
./pokenum -badugi As 2h 3d - Opponent_cards

# Vous aviez paire, avez tiré une carte distincte
./pokenum -badugi 4s 7h 9c - Opponent_cards
```

### Analyser les pat hands (mains complètes)

```bash
# Votre 4-card Badugi vs adversaire qui échange encore
./pokenum -badugi As 2h 3d 4c - 6s 7h
# Énorme avantage, mais attention aux nuts draws adverses
```

## 📈 Scénarios Stratégiques Avancés

### 1. Décision "Breaking" (casser une main)

```bash
# Vous avez un 3-card 6-7-8, adversaire semble avoir mieux
./pokenum -mc 50000 -badugi 6s 7h 8d - As 2c

# Vs garder et espérer une bonne 4ème carte
./pokenum -mc 50000 -badugi 6s 7h 8d - As 2c 3h
```

### 2. Analyse des "Smooth" vs "Rough" draws

```bash
# Smooth draw (A-2-3 + carte quelconque)
./pokenum -mc 100000 -badugi As 2h 3d - opponent_hand

# Rough draw (7-8-9 + carte quelconque)
./pokenum -mc 100000 -badugi 7s 8h 9d - opponent_hand
```

### 3. Situations heads-up critiques

```bash
# Dernière carte, vous pat vs adversaire qui échange 1
./pokenum -badugi As 2h 3d 5c - 4s 6h 7d

# Analyser si votre rough Badugi tient contre un bon draw
./pokenum -mc 75000 -badugi 8s 9h Tc Jd - As 2c 3h
```

## 🎲 Monte Carlo vs Énumération Exhaustive

### Quand utiliser `-mc` :
- **Mains incomplètes** (< 4 cartes par joueur)
- **Analyse rapide** (100k échantillons = ~1 seconde)
- **Situations complexes** avec beaucoup de variables

### Quand utiliser l'énumération exhaustive :
- **Mains complètes** (4 cartes chacun)
- **Précision absolue** nécessaire
- **Calculs rapides** (peu de cartes inconnues)

## 📊 Interprétation des Résultats

```
Badugi (4-card lowball, unique suits and ranks): 50000 sampled outcomes
cards           win   %win      lose  %lose       tie   %tie        EV
As 2h 3d      47532  95.06      2468   4.94         0   0.00     0.951
Ks Qh           2468   4.94     47532  95.06         0   0.00     0.049
```

### Lecture :
- **win %** : Pourcentage de victoires
- **EV** : Valeur espérée (0.951 = récupère 95.1% du pot en moyenne)
- **tie %** : Égalités (rares en Badugi)

## 🏆 Stratégies Optimales par Situation

### Position Early Draw :
```bash
# Standards d'ouverture : A-2-X, A-3-X, 2-3-X
./pokenum -badugi As 2h - random_opponent
# Seuil : >70% équité pour value bet
```

### Position Late Draw :
```bash
# Calls possibles avec des draws plus larges
./pokenum -badugi 4s 6h - tight_opener_range
# Seuil : >30% équité pour call défensif
```

### All-in Situations :
```bash
# ICM considerations avec stacks courtes
./pokenum -mc 200000 -badugi your_hand - opponent_range
# Besoin >50% pour call neutre, plus selon ICM
```

## 🔧 Commandes Utiles Pré-configurées

```bash
# Test d'une base A-2 vs opposition aléatoire
alias badugi_a2="./pokenum -mc 100000 -badugi As 2h - "

# Test rapide d'équité
alias badugi_quick="./pokenum -mc 50000 -badugi "

# Analyse précise (exhaustive quand possible)
alias badugi_exact="./pokenum -badugi "
```

## 💡 Conseils d'Optimisation

1. **Utilisez Monte Carlo** pour les analyses rapides en session
2. **Sauvegardez les résultats** pour builds un book de références
3. **Analysez les patterns** : A-2-X vs A-3-X vs 2-3-X
4. **Étudiez les breakevens** selon la position et les stacks
5. **Validez vos intuitions** avec les calculs exacts

## 🚨 Limitations Actuelles

- **Ranges complexes** : pokenum évalue main vs main, pas range vs range
- **Analyse multiway** : Limité à 2 joueurs pour l'instant
- **Dead cards** : Pas de support explicite pour cartes vues

## 📚 Ressources Complémentaires

- **Tests unitaires** : `./build/tests/test_badugi` pour validation
- **Code source** : `lib/badugi_eval.c` pour détails techniques
- **Autres variantes** : Support Badacey (-badacey) et Badeucy disponibles

---

*Ce guide utilise poker-eval avec support Badugi complet. Pour d'autres variantes de poker, consultez la documentation générale.*
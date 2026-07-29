# Guide d'utilisation des jeux avec Joker

## Vue d'ensemble

Le support des jeux avec joker est maintenant pleinement intégré dans pokenum. Le joker est représenté par "Xx" et est traité comme la 53ème carte du deck.

## Jeux supportés

### 1. Lowball A-5 avec joker (`-l`)
Le jeu le plus couramment utilisé avec joker. Le joker peut remplacer n'importe quelle carte pour former la meilleure main low possible.

```bash
# Exemple simple
./pokenum -l 7h 5s 3d Xx - 9s 8h 6d 4c

# Avec plus de joueurs
./pokenum -l Ac 2d - 5h 6s - Xx Kh
```

### 2. 5-card Draw Hi avec joker (`-5d`)
Le joker peut remplacer n'importe quelle carte pour former la meilleure main high.

```bash
./pokenum -5d As Ah Xx - Ks Kh Kd
```

### 3. 5-card Draw Hi/Lo 8-or-better avec joker (`-5d8`)
Le joker peut être utilisé pour le high ou le low.

```bash
./pokenum -5d8 Ac 2c 3c - 8h 8d 8s
```

### 4. 5-card Draw Hi/Lo no qualifier avec joker (`-5dnsq`)
Similaire au précédent mais sans qualificateur pour le low.

```bash
./pokenum -5dnsq 5h 5d - Xx 2c 3d
```

## Syntaxe

### Représentation du joker
- Toujours utiliser "Xx" (X majuscule suivi de x minuscule)
- Exemples valides : `Xx`, `xx`, `XX`, `xX` (tous sont acceptés)

### Format général
```bash
./pokenum [options] <main1> - <main2> - ... [-- <board>] [/ <cartes mortes>]
```

### Options utiles
- `-mc <n>` : Utiliser Monte Carlo avec n itérations (recommandé pour les cas complexes)
- `-t` : Mode terse (sortie sur une ligne)
- `-O` : Calculer l'histogramme d'ordonnancement

## Exemples pratiques

### 1. Lowball classique
```bash
# Joueur 1 a une main presque faite, joueur 2 a le joker
./pokenum -l Ac 2c 3c 4c - Xx 6h 7h 8h
```

### 2. Comparaison multi-joueurs
```bash
# 3 joueurs, un avec le joker
./pokenum -l Xx - Ac 2d 3h - 5s 6s 7s
```

### 3. Avec Monte Carlo (recommandé pour les cas complexes)
```bash
# 100,000 simulations
./pokenum -mc 100000 -l Xx 2h - 5s 6d - Ac Kh
```

### 4. Cartes mortes
```bash
# Le roi de cœur est mort
./pokenum -l Xx 2d 3h - 5s 6s 7s / Kh
```

## Notes importantes

### Performance
- L'énumération exhaustive avec joker génère beaucoup plus de combinaisons (53 cartes au lieu de 52)
- Pour les cas avec peu de cartes fixes, préférer Monte Carlo
- Exemple : avec 2 joueurs ayant chacun 1 carte, l'énumération exhaustive génère des millions de combinaisons

### Règles du joker
- En lowball A-5 : le joker est toujours la meilleure carte possible pour compléter une main low
- En high : le joker complète la meilleure main possible (quinte flush, carré, etc.)
- Le joker ne peut pas être dupliqué (il n'y a qu'un seul joker dans le deck)

### Limitations connues
- Certaines combinaisons très ouvertes peuvent causer des timeouts
- Solution : utiliser `-mc` avec un nombre raisonnable d'itérations

## Dépannage

### Problème : Timeout ou blocage
Solution : Utiliser Monte Carlo
```bash
# Au lieu de
./pokenum -l Xx - Ac

# Utiliser
./pokenum -mc 100000 -l Xx - Ac
```

### Problème : "Joker enumeration not yet implemented"
Solution : Recompiler avec un clean build
```bash
rm -rf build
mkdir build
cd build
cmake ..
make -j4
```

## Exemples de résultats

### Lowball avec joker
```
5-card Draw A-5 Lowball with joker: 45540 enumerated outcomes
cards              win   %win      lose  %lose       tie   %tie        EV
5s 3d 7h Xx      19447  42.70     26093  57.30         0   0.00     0.427
9s 4c 6d 8h      26093  57.30     19447  42.70         0   0.00     0.573
```

Le joker donne au premier joueur une main 7-5-3-2-A, mais le second joueur avec 9-8-6-4-x gagne plus souvent.
#!/bin/bash
# Script de migration automatique des tests vers headers canoniques

echo "=== Migration des Tests vers Headers Canoniques ==="

# Trouver tous les fichiers C dans tests/ (limiter aux plus critiques)
find tests/ -name "test_*.c" -type f | head -20 | while read file; do
    echo "Migration de: $file"

    # Sauvegarder le fichier original
    cp "$file" "$file.backup"

    # Remplacer les includes legacy par canonical
    sed -i '' 's|#include "poker_eval/poker_defs.h"|#include "poker_eval/poker_eval.h"|g' "$file"
    sed -i '' 's|#include "poker_defs.h"|#include "poker_eval/poker_eval.h"|g' "$file"

    # Supprimer les includes inlines legacy
    sed -i '' '/^#include "inlines\//d' "$file"

    # Remplacer les autres includes legacy courants
    sed -i '' 's|#include "handval.h"|// Inclus dans poker_eval.h|g' "$file"
    sed -i '' 's|#include "enumerate.h"|// Inclus dans poker_eval.h|g' "$file"
    sed -i '' 's|#include "deck.h"|// Inclus dans poker_eval.h|g' "$file"

    echo "  ✓ Migré: $file"
done

echo ""
echo "=== Migration Terminée ==="
echo "✓ Tests principaux utilisent maintenant poker_eval/poker_eval.h"
echo "📁 Fichiers de sauvegarde: *.backup"
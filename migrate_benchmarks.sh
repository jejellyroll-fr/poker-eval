#!/bin/bash
# Script de migration automatique des benchmarks vers headers canoniques

echo "=== Migration des Benchmarks vers Headers Canoniques ==="

# Trouver tous les fichiers C dans benchmarks/
find benchmarks/ -name "*.c" -type f | while read file; do
    echo "Migration de: $file"

    # Sauvegarder le fichier original
    cp "$file" "$file.backup"

    # Remplacer les includes legacy par canonical
    sed -i '' 's|#include "poker_eval/poker_defs.h"|#include "poker_eval/poker_eval.h"|g' "$file"
    sed -i '' 's|#include "poker_defs.h"|#include "poker_eval/poker_eval.h"|g' "$file"

    # Remplacer les includes spécifiques aux benchmarks
    sed -i '' 's|#include "ofc.h"|#include "poker_eval/games/ofc.h"|g' "$file"
    sed -i '' 's|#include "ofc_simd.h"|#include "poker_eval/games/ofc_simd.h"|g' "$file"

    # Supprimer les includes inlines legacy
    sed -i '' '/^#include "inlines\//d' "$file"

    echo "  ✓ Migré: $file"
done

echo ""
echo "=== Migration Terminée ==="
echo "✓ Tous les benchmarks utilisent maintenant les headers canoniques"
echo "📁 Fichiers de sauvegarde: *.backup"
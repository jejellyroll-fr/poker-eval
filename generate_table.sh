#!/bin/bash
# Script to generate pre-flop equity table

OUTPUT_FILE="holdem_preflop_169x169.dat"

echo "=== Pre-flop Equity Table Generation ==="
echo ""
echo "This will generate a 169×169 lookup table for instant equity queries."
echo "Output: $OUTPUT_FILE"
echo ""
echo "Estimated time: 5-10 minutes"
echo "Progress will be shown below..."
echo ""

./src/utils/generate_preflop_table "$OUTPUT_FILE"

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Table generated successfully!"
    echo "File: $OUTPUT_FILE"
    ls -lh "$OUTPUT_FILE"
else
    echo ""
    echo "❌ Generation failed"
    exit 1
fi

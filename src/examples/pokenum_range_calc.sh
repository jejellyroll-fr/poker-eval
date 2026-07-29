#!/bin/bash

# pokenum_range_calc.sh - Calculate range equities using pokenum
# This script demonstrates how to use pokenum for range calculations

# Set the path to pokenum
if [ -f "../build/pokenum" ]; then
    POKENUM="../build/pokenum"
elif [ -f "./build/pokenum" ]; then
    POKENUM="./build/pokenum"
elif [ -f "./pokenum" ]; then
    POKENUM="./pokenum"
else
    echo "Error: pokenum not found!"
    exit 1
fi

echo "=============================================="
echo "RANGE EQUITY CALCULATOR USING POKENUM"
echo "=============================================="
echo ""
echo "Using pokenum at: $POKENUM"
echo ""

# Function to extract EV from pokenum output
extract_ev() {
    local player=$1
    local output=$2
    # Extract the EV value for the specified player
    echo "$output" | grep -A 10 "cards" | grep -E "^[A-Za-z0-9 ]+\s+[0-9]+" | sed -n "${player}p" | awk '{print $NF}'
}

# Function to calculate range vs single hand
calculate_range_vs_hand() {
    local range_name=$1
    shift
    local -a range_hands=("$@")
    local vs_hand="${range_hands[-1]}"
    unset 'range_hands[-1]'
    
    echo "Calculating: $range_name vs $vs_hand"
    echo "Range contains ${#range_hands[@]} hands"
    
    local total_ev1=0
    local total_ev2=0
    local count=0
    
    for hand in "${range_hands[@]}"; do
        # Check if hands don't share cards
        local hand1_cards=$(echo "$hand" | sed 's/ //g')
        local hand2_cards=$(echo "$vs_hand" | sed 's/ //g')
        
        # Simple conflict check (could be improved)
        local conflict=0
        for ((i=0; i<${#hand1_cards}; i+=2)); do
            local card="${hand1_cards:$i:2}"
            if [[ "$hand2_cards" == *"$card"* ]]; then
                conflict=1
                break
            fi
        done
        
        if [ $conflict -eq 0 ]; then
            # Run pokenum
            local result=$($POKENUM -h $hand - $vs_hand 2>/dev/null)
            if [ $? -eq 0 ]; then
                local ev1=$(extract_ev 1 "$result")
                local ev2=$(extract_ev 2 "$result")
                
                if [ ! -z "$ev1" ] && [ ! -z "$ev2" ]; then
                    total_ev1=$(echo "$total_ev1 + $ev1" | bc -l)
                    total_ev2=$(echo "$total_ev2 + $ev2" | bc -l)
                    count=$((count + 1))
                    echo -n "."
                fi
            fi
        fi
    done
    echo ""
    
    if [ $count -gt 0 ]; then
        local avg_ev1=$(echo "scale=3; $total_ev1 / $count" | bc -l)
        local avg_ev2=$(echo "scale=3; $total_ev2 / $count" | bc -l)
        echo "Evaluated $count valid matchups"
        echo "$range_name average equity: $avg_ev1"
        echo "$vs_hand equity: $avg_ev2"
    else
        echo "No valid matchups found!"
    fi
    echo ""
}

# Function to calculate range vs range
calculate_range_vs_range() {
    local range1_name=$1
    local range2_name=$2
    shift 2
    local -a all_hands=("$@")
    
    # Find the separator
    local sep_index=-1
    for i in "${!all_hands[@]}"; do
        if [ "${all_hands[$i]}" == "--SEPARATOR--" ]; then
            sep_index=$i
            break
        fi
    done
    
    if [ $sep_index -eq -1 ]; then
        echo "Error: Separator not found!"
        return
    fi
    
    # Split into two ranges
    local -a range1_hands=("${all_hands[@]:0:$sep_index}")
    local -a range2_hands=("${all_hands[@]:$((sep_index+1))}")
    
    echo "Calculating: $range1_name (${#range1_hands[@]} hands) vs $range2_name (${#range2_hands[@]} hands)"
    
    local total_ev1=0
    local total_ev2=0
    local count=0
    local total_matchups=$((${#range1_hands[@]} * ${#range2_hands[@]}))
    
    for hand1 in "${range1_hands[@]}"; do
        for hand2 in "${range2_hands[@]}"; do
            # Check if hands don't share cards
            local hand1_cards=$(echo "$hand1" | sed 's/ //g')
            local hand2_cards=$(echo "$hand2" | sed 's/ //g')
            
            local conflict=0
            for ((i=0; i<${#hand1_cards}; i+=2)); do
                local card="${hand1_cards:$i:2}"
                if [[ "$hand2_cards" == *"$card"* ]]; then
                    conflict=1
                    break
                fi
            done
            
            if [ $conflict -eq 0 ]; then
                local result=$($POKENUM -h $hand1 - $hand2 2>/dev/null)
                if [ $? -eq 0 ]; then
                    local ev1=$(extract_ev 1 "$result")
                    local ev2=$(extract_ev 2 "$result")
                    
                    if [ ! -z "$ev1" ] && [ ! -z "$ev2" ]; then
                        total_ev1=$(echo "$total_ev1 + $ev1" | bc -l)
                        total_ev2=$(echo "$total_ev2 + $ev2" | bc -l)
                        count=$((count + 1))
                        
                        # Progress indicator
                        if [ $((count % 10)) -eq 0 ]; then
                            echo -n "."
                        fi
                    fi
                fi
            fi
        done
    done
    echo ""
    
    if [ $count -gt 0 ]; then
        local avg_ev1=$(echo "scale=3; $total_ev1 / $count" | bc -l)
        local avg_ev2=$(echo "scale=3; $total_ev2 / $count" | bc -l)
        echo "Evaluated $count valid matchups out of $total_matchups possible"
        echo "$range1_name average equity: $avg_ev1"
        echo "$range2_name average equity: $avg_ev2"
    else
        echo "No valid matchups found!"
    fi
    echo ""
}

# Example 1: AA-KK vs QQ
echo "Example 1: {AA, KK} vs QQ"
echo "-------------------------"

# All AA combinations
AA_HANDS=("As Ah" "As Ac" "As Ad" "Ah Ac" "Ah Ad" "Ac Ad")
# All KK combinations  
KK_HANDS=("Ks Kh" "Ks Kc" "Ks Kd" "Kh Kc" "Kh Kd" "Kc Kd")
# Combine AA and KK
AAKK_HANDS=("${AA_HANDS[@]}" "${KK_HANDS[@]}")

# Calculate against specific QQ hand
calculate_range_vs_hand "{AA,KK}" "${AAKK_HANDS[@]}" "Qs Qd"

# Example 2: AK vs pocket pairs
echo "Example 2: AK vs {QQ, JJ}"
echo "-------------------------"

# AK combinations (16 total)
AK_HANDS=()
for s1 in s h c d; do
    for s2 in s h c d; do
        AK_HANDS+=("A$s1 K$s2")
    done
done

# QQ and JJ combinations
QQ_HANDS=("Qs Qh" "Qs Qc" "Qs Qd" "Qh Qc" "Qh Qd" "Qc Qd")
JJ_HANDS=("Js Jh" "Js Jc" "Js Jd" "Jh Jc" "Jh Jd" "Jc Jd")
QQJJ_HANDS=("${QQ_HANDS[@]}" "${JJ_HANDS[@]}")

calculate_range_vs_range "AK" "{QQ,JJ}" "${AK_HANDS[@]}" "--SEPARATOR--" "${QQJJ_HANDS[@]}"

# Example 3: With a flop
echo "Example 3: AA vs KK on flop 9s 8s 2h"
echo "-------------------------------------"

# We'll use fewer combinations to speed up the example
echo "Running pokenum for each matchup..."

# Just use one example of each
result=$($POKENUM -h "As Ah" - "Ks Kh" -- "9s" "8s" "2h")
echo "$result" | grep -A 3 "cards"
echo ""

# Example 4: Monte Carlo simulation for larger ranges
echo "Example 4: Top pairs vs Broadway cards (Monte Carlo)"
echo "---------------------------------------------------"

# Using Monte Carlo for speed
echo "AsAh vs random Broadway: "
$POKENUM -mc 10000 -h "As Ah" - "Kc Qd" | grep -A 3 "cards"
echo ""

echo "KsKh vs AQo:"
$POKENUM -mc 10000 -h "Ks Kh" - "Ac Qd" | grep -A 3 "cards"
echo ""

# Example 5: Omaha ranges
echo "Example 5: Omaha - AAxx vs KKxx"
echo "--------------------------------"

# Simple Omaha example
echo "Double-suited aces vs rainbow kings:"
$POKENUM -o "As Ac Ks Kc" - "Kh Kd Qh Jd" | grep -A 3 "cards"
echo ""

# Summary function to display results nicely
display_summary() {
    echo "=============================================="
    echo "SUMMARY"
    echo "=============================================="
    echo "This script demonstrates how to:"
    echo "1. Calculate single hand vs range equities"
    echo "2. Calculate range vs range equities"
    echo "3. Use pokenum for specific matchups"
    echo "4. Use Monte Carlo for faster approximations"
    echo ""
    echo "For production use, consider:"
    echo "- Implementing proper card conflict detection"
    echo "- Using parallel processing for large ranges"
    echo "- Caching results for common matchups"
    echo "- Creating a proper range syntax parser"
}

display_summary
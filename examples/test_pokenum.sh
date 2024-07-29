#!/bin/bash

# Fonction pour détecter l'OS
detect_os() {
    case "$(uname -s)" in
        Linux*)     echo "Linux";;
        Darwin*)    echo "MacOS";;
        CYGWIN*)    echo "Windows";;
        MINGW*)     echo "Windows";;
        *BSD*)      echo "BSD";;
        *)          echo "Unknown";;
    esac
}

# Détection de l'OS
OS=$(detect_os)

# Définition de la commande pokenum en fonction de l'OS
if [ "$OS" = "Windows" ]; then
    POKENUM="../build/Debug/example_pokenum.exe"
else
    POKENUM="../build/Debug/pokenum"
fi

echo "Detected OS: $OS"
echo "Using command: $POKENUM"

# Tests pour chaque type de jeu
echo "Testing Texas Hold'em"
$POKENUM -h Ac 7c - 5s 4s - Ks Kd

echo "Testing shortdeck Hold'em"
$POKENUM -sd Ac 8c - Ks Kd

echo -e "\nTesting Hold'em Hi/Lo 8-or-better"
$POKENUM -h8 Ac 7c - 5s 4s - Ks Kd

echo -e "\nTesting Omaha Hi"
$POKENUM -o As Kh Qs Jh - 8h 8d 7h 6d

echo -e "\nTesting Omaha Hi 5 cards"
$POKENUM -o5 As Kh Qs Jh Ts - 8h 8d 7h 6d 9c

echo -e "\nTesting Omaha Hi 6 cards"
$POKENUM -o6 As Kh Qs Jh Ts 9d - 8h 8d 7h 6d 9c 6c

echo -e "\nTesting Omaha Hi/Lo 8-or-better"
$POKENUM -o8 As Kh Qs Jh - 8h 8d 7h 6d

echo -e "\nTesting Omaha 5 cards Hi/Lo 8-or-better"
$POKENUM -o85 As Kh Qs Jh Ts - 8h 8d 7h 6d 9c

echo -e "\nTesting 7-Card Stud Hi"
$POKENUM -7s As Ah Ts Th 8h 8d - Kc Qc Jc Td 3c 2d

echo -e "\nTesting 7-Card Stud Hi/Lo 8-or-better"
$POKENUM -7s8 As Ah Ts Th 8h 8d - Kc Qc Jc Td 3c 2d

echo -e "\nTesting 7-Card Stud Hi/Lo no stinking qualifier"
$POKENUM -7snsq As Ah Ts Th 8h 8d - Kc Qc Jc Td 3c 2d

echo -e "\nTesting 7-Card Stud Ace-to-5 Low (Razz)"
$POKENUM -r As 2h 3s 4h 5h 6d - Kc Qc Jc Td 3c 2d

# echo -e "\nTesting 5-Card Draw Hi (with joker)"
# $POKENUM -5d Ah Kh Qh Jh Xx - 9s 8s 7s 6s 5s

# echo -e "\nTesting 5-Card Draw Hi/Lo 8-or-better (with joker)"
# $POKENUM -5d8  9s 8s 7s 6s 5s - Ah 2h 3h 4h Xx

# echo -e "\nTesting 5-Card Draw Hi/Lo no stinking qualifier (with joker)"
# $POKENUM -5dnsq Ah 2h 3h 4h Xx - 9s 8s 7s 6s 5s

# echo -e "\nTesting 5-Card Draw Ace-to-5 Lowball (with joker)"
# $POKENUM -l Ah 2h 3h 4h Xx - 9s 8s 7s 6s 5s

# echo -e "\nTesting 5-Card Draw Deuce-to-Seven Lowball"
# $POKENUM -l27 7h 5h 4h 3h 2d - 9s 8s 6s 5s 4s

# echo -e "\nTesting Monte Carlo simulation"
# $POKENUM -mc 10000 -h Ac 7c - 5s 4s - Ks Kd

# echo -e "\nTesting 5-Card Draw Hi "
# $POKENUM -5d Ah Kh Qh Jh 9d - 9s 8s 7s 6s 5s

# echo -e "\nTesting 5-Card Draw Hi/Lo 8-or-better "
# $POKENUM -5d8  9s 8s 7s 6s 5s - Ah 2h 3h 4h Jh

# echo -e "\nTesting 5-Card Draw Hi/Lo no stinking qualifier"
# $POKENUM -5dnsq Ah 2h 3h 4h Jh - 9s 8s 7s 6s 5s

# echo -e "\nTesting 5-Card Draw Ace-to-5 Lowball "
# $POKENUM -l Ah 2h 3h 4h Ks - 9s 8s 7s 6s 5s
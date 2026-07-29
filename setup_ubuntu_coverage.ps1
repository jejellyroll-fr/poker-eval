# Script pour configurer Ubuntu et générer le rapport de couverture
# À exécuter APRÈS le redémarrage (ne nécessite PAS les droits admin)

Write-Host "================================================" -ForegroundColor Cyan
Write-Host "Configuration d'Ubuntu et génération de la couverture" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# Vérifier que WSL est installé
$wslStatus = wsl --status 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERREUR: WSL n'est pas correctement configuré." -ForegroundColor Red
    Write-Host "Veuillez exécuter enable_wsl.ps1 en tant qu'administrateur d'abord." -ForegroundColor Yellow
    Read-Host "Appuyez sur Entrée pour quitter"
    exit 1
}

# Vérifier que Ubuntu est installé
$distros = wsl --list --quiet 2>&1
if (-not ($distros -match "Ubuntu")) {
    Write-Host "Ubuntu n'est pas encore configuré. Lancement de la première configuration..." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Vous devrez:" -ForegroundColor Cyan
    Write-Host "1. Choisir un nom d'utilisateur Linux" -ForegroundColor Cyan
    Write-Host "2. Créer un mot de passe (il ne s'affichera pas)" -ForegroundColor Cyan
    Write-Host "3. Confirmer le mot de passe" -ForegroundColor Cyan
    Write-Host ""
    Read-Host "Appuyez sur Entrée pour continuer"

    wsl -d Ubuntu
}

Write-Host ""
Write-Host "Installation des outils de développement dans Ubuntu..." -ForegroundColor Green
Write-Host "Cela peut prendre quelques minutes..." -ForegroundColor Yellow
Write-Host ""

# Installer les outils nécessaires dans Ubuntu
wsl -d Ubuntu bash -c @"
echo '================================================'
echo 'Installation des outils de développement'
echo '================================================'
echo ''

# Mettre à jour les paquets
echo 'Mise à jour des paquets...'
sudo apt update

# Installer les outils
echo ''
echo 'Installation de build-essential, cmake, gcovr, lcov...'
sudo apt install -y build-essential cmake gcovr lcov

# Vérifier les installations
echo ''
echo '================================================'
echo 'Vérification des installations:'
echo '================================================'
gcc --version | head -1
gcov --version | head -1
lcov --version | head -1
cmake --version | head -1

echo ''
echo 'Installation terminée!'
"@

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "Génération du rapport de couverture" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# Naviguer vers le projet et générer la couverture
wsl -d Ubuntu bash -c @"
cd /mnt/c/Users/jd/Documents/GitHub/poker-eval-new

echo 'Nettoyage du build précédent...'
rm -rf build

echo 'Création du répertoire build...'
mkdir -p build
cd build

echo ''
echo 'Configuration avec CMake (couverture activée)...'
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++

echo ''
echo 'Compilation avec $(nproc) coeurs...'
make -j\$(nproc)

echo ''
echo '================================================'
echo 'Exécution des tests'
echo '================================================'
make test

echo ''
echo 'Retour au répertoire racine...'
cd ..

echo ''
echo '================================================'
echo 'Génération du rapport de couverture'
echo '================================================'
chmod +x generate_coverage.sh
./generate_coverage.sh

echo ''
echo '================================================'
echo 'Rapport de couverture généré!'
echo '================================================'
echo ''
echo 'Le rapport HTML est disponible dans:'
echo '  coverage_html/index.html'
echo ''
"@

Write-Host ""
Write-Host "================================================" -ForegroundColor Green
Write-Host "Configuration terminée!" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Ouverture du rapport de couverture..." -ForegroundColor Cyan

if (Test-Path "coverage_html\index.html") {
    Start-Process "coverage_html\index.html"
    Write-Host "Le rapport a été ouvert dans votre navigateur." -ForegroundColor Green
} else {
    Write-Host "Le rapport n'a pas pu être trouvé." -ForegroundColor Red
    Write-Host "Vérifiez les messages d'erreur ci-dessus." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Vous pouvez également consulter:" -ForegroundColor Cyan
Write-Host "  - COVERAGE_REPORT.md (analyse détaillée)" -ForegroundColor White
Write-Host "  - coverage_html/index.html (rapport HTML interactif)" -ForegroundColor White
Write-Host ""

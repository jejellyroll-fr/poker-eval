# Script PowerShell pour activer WSL et Virtual Machine Platform
# CE SCRIPT DOIT ÊTRE EXÉCUTÉ EN TANT QU'ADMINISTRATEUR

Write-Host "================================================" -ForegroundColor Cyan
Write-Host "Script d'activation de WSL2 et Virtual Machine Platform" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# Vérifier si le script est exécuté en tant qu'administrateur
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "ERREUR: Ce script doit être exécuté en tant qu'Administrateur!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Pour exécuter ce script:" -ForegroundColor Yellow
    Write-Host "1. Clic droit sur PowerShell dans le menu Démarrer" -ForegroundColor Yellow
    Write-Host "2. Sélectionner 'Exécuter en tant qu'administrateur'" -ForegroundColor Yellow
    Write-Host "3. Naviguer vers ce répertoire et exécuter: .\enable_wsl.ps1" -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Appuyez sur Entrée pour quitter"
    exit 1
}

Write-Host "Activation de la fonctionnalité Virtual Machine Platform..." -ForegroundColor Green
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart

Write-Host ""
Write-Host "Activation de la fonctionnalité Sous-système Windows pour Linux..." -ForegroundColor Green
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "Activation terminée!" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "IMPORTANT: Un redémarrage est nécessaire pour appliquer les changements." -ForegroundColor Yellow
Write-Host ""

$restart = Read-Host "Voulez-vous redémarrer maintenant? (O/N)"

if ($restart -eq "O" -or $restart -eq "o") {
    Write-Host "Redémarrage dans 10 secondes..." -ForegroundColor Yellow
    Write-Host "Appuyez sur Ctrl+C pour annuler" -ForegroundColor Yellow
    Start-Sleep -Seconds 10
    Restart-Computer
} else {
    Write-Host ""
    Write-Host "Veuillez redémarrer manuellement avant de continuer." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Après le redémarrage, exécutez:" -ForegroundColor Cyan
    Write-Host "  wsl -d Ubuntu" -ForegroundColor White
    Write-Host ""
}

# Script pour verifier et corriger la configuration WSL
# A executer en tant qu'administrateur

Write-Host "`nVerification de la configuration WSL...`n" -ForegroundColor Cyan

# Verifier Virtual Machine Platform
Write-Host "1. Verification de Virtual Machine Platform..." -ForegroundColor Yellow
$vmp = dism.exe /online /get-featureinfo /featurename:VirtualMachinePlatform
if ($vmp -match "State : Enabled|État : Activé") {
    Write-Host "   [OK] Virtual Machine Platform est active" -ForegroundColor Green
} else {
    Write-Host "   [!] Virtual Machine Platform n'est pas active. Activation..." -ForegroundColor Red
    dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
    Write-Host "   [OK] Virtual Machine Platform active" -ForegroundColor Green
}

# Verifier WSL
Write-Host "`n2. Verification du Sous-systeme Windows pour Linux..." -ForegroundColor Yellow
$wsl = dism.exe /online /get-featureinfo /featurename:Microsoft-Windows-Subsystem-Linux
if ($wsl -match "State : Enabled|État : Activé") {
    Write-Host "   [OK] WSL est active" -ForegroundColor Green
} else {
    Write-Host "   [!] WSL n'est pas active. Activation..." -ForegroundColor Red
    dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
    Write-Host "   [OK] WSL active" -ForegroundColor Green
}

# Verifier Hyper-V (optionnel mais recommande)
Write-Host "`n3. Verification d'Hyper-V..." -ForegroundColor Yellow
$hyperv = Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All -ErrorAction SilentlyContinue
if ($hyperv -and $hyperv.State -eq "Enabled") {
    Write-Host "   [OK] Hyper-V est active" -ForegroundColor Green
} else {
    Write-Host "   [i] Hyper-V n'est pas active (optionnel)" -ForegroundColor Gray
}

# Verifier si la virtualisation est activee dans le BIOS
Write-Host "`n4. Verification de la virtualisation materielle..." -ForegroundColor Yellow
$vmCompute = Get-Service -Name vmcompute -ErrorAction SilentlyContinue
if ($vmCompute -and $vmCompute.Status -eq "Running") {
    Write-Host "   [OK] Le service de virtualisation est en cours d'execution" -ForegroundColor Green
} else {
    Write-Host "   [!] Le service de virtualisation n'est pas en cours d'execution" -ForegroundColor Yellow
}

# Verifier la virtualisation CPU
Write-Host "`n5. Verification de la virtualisation CPU dans le BIOS..." -ForegroundColor Yellow
try {
    $hypervisorPresent = (Get-WmiObject Win32_ComputerSystem).HypervisorPresent
    if ($hypervisorPresent) {
        Write-Host "   [OK] Virtualisation activee (Hyperviseur detecte)" -ForegroundColor Green
    } else {
        Write-Host "   [!] Virtualisation peut-etre desactivee dans le BIOS" -ForegroundColor Yellow
        Write-Host "   [i] Si WSL ne fonctionne pas apres redemarrage:" -ForegroundColor Gray
        Write-Host "      - Redemarrez et entrez dans le BIOS (F2/F10/Del/Esc)" -ForegroundColor Gray
        Write-Host "      - Cherchez 'Virtualization Technology', 'Intel VT-x' ou 'AMD-V'" -ForegroundColor Gray
        Write-Host "      - Activez-le et sauvegardez (F10)" -ForegroundColor Gray
    }
} catch {
    Write-Host "   [!] Impossible de detecter l'etat de la virtualisation" -ForegroundColor Yellow
}

# Verifier si un redemarrage est necessaire
$pendingReboot = Test-Path "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending"
$pendingReboot2 = Test-Path "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update\RebootRequired"

Write-Host "`n================================================" -ForegroundColor Cyan

if ($pendingReboot -or $pendingReboot2) {
    Write-Host "`n[!] REDEMARRAGE NECESSAIRE" -ForegroundColor Red
    Write-Host "`nUn redemarrage est requis pour appliquer les changements." -ForegroundColor Yellow
    Write-Host "Apres le redemarrage, executez: wsl --install -d Ubuntu" -ForegroundColor Yellow
    Write-Host "`nVoulez-vous redemarrer maintenant? (O/N): " -ForegroundColor Cyan -NoNewline
    $response = Read-Host
    if ($response -eq "O" -or $response -eq "o") {
        Write-Host "`nRedemarrage en cours..." -ForegroundColor Yellow
        Restart-Computer -Force
    } else {
        Write-Host "`nPensez a redemarrer manuellement." -ForegroundColor Yellow
    }
} else {
    Write-Host "`n[OK] CONFIGURATION OK" -ForegroundColor Green
    Write-Host "`nLa configuration WSL est correcte." -ForegroundColor Green
    Write-Host "`nProchaines etapes:" -ForegroundColor Cyan
    Write-Host "1. Installez Ubuntu: wsl --install -d Ubuntu" -ForegroundColor White
    Write-Host "2. Configurez Ubuntu (nom d'utilisateur et mot de passe)" -ForegroundColor White
    Write-Host "3. Executez: .\setup_ubuntu_coverage_fixed.ps1" -ForegroundColor White
}

Write-Host "`n================================================`n" -ForegroundColor Cyan

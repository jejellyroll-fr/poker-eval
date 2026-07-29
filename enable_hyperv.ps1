# Script to enable Hyper-V and finalize WSL configuration
# MUST BE RUN AS ADMINISTRATOR

Write-Host "================================================" -ForegroundColor Cyan
Write-Host "   Hyper-V Activation for WSL2" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# Check administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "ERROR: This script requires administrator privileges!" -ForegroundColor Red
    Write-Host ""
    Write-Host "To run it correctly:" -ForegroundColor Yellow
    Write-Host "1. Press Windows key" -ForegroundColor White
    Write-Host "2. Type PowerShell" -ForegroundColor White
    Write-Host "3. RIGHT CLICK on Windows PowerShell" -ForegroundColor White
    Write-Host "4. Select Run as administrator" -ForegroundColor White
    Write-Host "5. In Admin window, run:" -ForegroundColor White
    Write-Host "   cd C:\Users\jd\Documents\GitHub\poker-eval-new" -ForegroundColor Cyan
    Write-Host "   .\enable_hyperv.ps1" -ForegroundColor Cyan
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "Administrator privileges confirmed" -ForegroundColor Green
Write-Host ""

# Check current state
Write-Host "Checking current state..." -ForegroundColor Cyan
$hypervState = Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V-All -ErrorAction SilentlyContinue

if ($hypervState -and $hypervState.State -eq "Enabled") {
    Write-Host "Hyper-V is already enabled!" -ForegroundColor Green
} else {
    Write-Host "Enabling Hyper-V..." -ForegroundColor Yellow
    Write-Host "   This may take a few minutes..." -ForegroundColor White
    Write-Host ""

    # Enable Hyper-V
    Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V -All -NoRestart

    Write-Host ""
    Write-Host "Hyper-V enabled successfully!" -ForegroundColor Green
}

# Check Virtual Machine Platform
Write-Host ""
Write-Host "Checking Virtual Machine Platform..." -ForegroundColor Cyan
$vmpState = Get-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform -ErrorAction SilentlyContinue

if ($vmpState -and $vmpState.State -eq "Enabled") {
    Write-Host "Virtual Machine Platform is already enabled!" -ForegroundColor Green
} else {
    Write-Host "Enabling Virtual Machine Platform..." -ForegroundColor Yellow
    dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
    Write-Host "Virtual Machine Platform enabled!" -ForegroundColor Green
}

# Check WSL
Write-Host ""
Write-Host "Checking WSL..." -ForegroundColor Cyan
$wslState = Get-WindowsOptionalFeature -Online -FeatureName Microsoft-Windows-Subsystem-Linux -ErrorAction SilentlyContinue

if ($wslState -and $wslState.State -eq "Enabled") {
    Write-Host "WSL is already enabled!" -ForegroundColor Green
} else {
    Write-Host "Enabling WSL..." -ForegroundColor Yellow
    dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
    Write-Host "WSL enabled!" -ForegroundColor Green
}

Write-Host ""
Write-Host "================================================" -ForegroundColor Green
Write-Host "   Configuration Complete!" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Green
Write-Host ""
Write-Host "RESTART REQUIRED" -ForegroundColor Yellow
Write-Host ""
Write-Host "To finalize activation, you must restart your computer." -ForegroundColor White
Write-Host ""
Write-Host "After restart, run:" -ForegroundColor Cyan
Write-Host "   cd C:\Users\jd\Documents\GitHub\poker-eval-new" -ForegroundColor White
Write-Host "   .\setup_ubuntu_coverage_fixed.ps1" -ForegroundColor White
Write-Host ""

$restart = Read-Host "Do you want to restart now? (Y/N)"
if ($restart -eq "O" -or $restart -eq "o" -or $restart -eq "Y" -or $restart -eq "y") {
    Write-Host ""
    Write-Host "Restarting in 10 seconds..." -ForegroundColor Yellow
    Write-Host "Press Ctrl+C to cancel" -ForegroundColor White
    Start-Sleep -Seconds 10
    Restart-Computer -Force
} else {
    Write-Host ""
    Write-Host "Don't forget to restart before continuing!" -ForegroundColor Yellow
    Write-Host ""
}

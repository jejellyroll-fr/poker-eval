# Script to configure Ubuntu and generate coverage report
# Run AFTER restart (does NOT require admin rights)

Write-Host "================================================" -ForegroundColor Cyan
Write-Host "Ubuntu Configuration and Coverage Generation" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# Check that WSL is installed
$wslStatus = wsl --status 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: WSL is not properly configured." -ForegroundColor Red
    Write-Host "Please run enable_hyperv.ps1 as administrator first." -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}

# Find installed Ubuntu distribution
$distros = wsl --list --quiet 2>&1 | Out-String
$ubuntuDistro = ""

if ($distros -match "Ubuntu-24.04") {
    $ubuntuDistro = "Ubuntu-24.04"
} elseif ($distros -match "Ubuntu-22.04") {
    $ubuntuDistro = "Ubuntu-22.04"
} elseif ($distros -match "Ubuntu-20.04") {
    $ubuntuDistro = "Ubuntu-20.04"
} elseif ($distros -match "Ubuntu") {
    $ubuntuDistro = "Ubuntu"
}

if ($ubuntuDistro -eq "") {
    Write-Host "No Ubuntu distribution found." -ForegroundColor Red
    Write-Host ""
    Write-Host "Installing Ubuntu..." -ForegroundColor Yellow
    wsl --install Ubuntu-24.04

    Write-Host ""
    Write-Host "Ubuntu has been installed. Please configure it:" -ForegroundColor Cyan
    Write-Host "1. Create a username" -ForegroundColor White
    Write-Host "2. Create a password" -ForegroundColor White
    Write-Host ""

    ubuntu2404.exe

    $ubuntuDistro = "Ubuntu-24.04"
}

Write-Host "Distribution found: $ubuntuDistro" -ForegroundColor Green
Write-Host ""

Write-Host "Installing development tools in Ubuntu..." -ForegroundColor Green
Write-Host "This may take a few minutes..." -ForegroundColor Yellow
Write-Host ""

# Install necessary tools in Ubuntu
wsl -d $ubuntuDistro bash -c @"
echo '================================================'
echo 'Installing Development Tools'
echo '================================================'
echo ''

# Update packages
echo 'Updating packages...'
sudo apt update -qq

# Install tools
echo ''
echo 'Installing build-essential, cmake, gcovr, lcov...'
sudo apt install -y build-essential cmake gcovr lcov

# Verify installations
echo ''
echo '================================================'
echo 'Verifying installations:'
echo '================================================'
gcc --version | head -1
gcov --version | head -1
lcov --version | head -1
cmake --version | head -1

echo ''
echo 'Installation complete!'
"@

Write-Host ""
Write-Host "================================================" -ForegroundColor Cyan
Write-Host "Generating Coverage Report" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan
Write-Host ""

# Navigate to project and generate coverage
wsl -d $ubuntuDistro bash -c @"
cd /mnt/c/Users/jd/Documents/GitHub/poker-eval-new

echo 'Cleaning previous build...'
rm -rf build

echo 'Creating build directory...'
mkdir -p build
cd build

echo ''
echo 'Configuring with CMake (coverage enabled)...'
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++

echo ''
echo 'Building with \$(nproc) cores...'
make -j\$(nproc)

echo ''
echo '================================================'
echo 'Running Tests'
echo '================================================'
make test

echo ''
echo 'Returning to root directory...'
cd ..

echo ''
echo '================================================'
echo 'Generating Coverage Report'
echo '================================================'
chmod +x generate_coverage.sh
./generate_coverage.sh

echo ''
echo '================================================'
echo 'Coverage Report Generated!'
echo '================================================'
echo ''
echo 'HTML report available at:'
echo '  coverage_html/index.html'
echo ''
"@

Write-Host ""
Write-Host "================================================" -ForegroundColor Green
Write-Host "Configuration Complete!" -ForegroundColor Green
Write-Host "================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Opening coverage report..." -ForegroundColor Cyan

if (Test-Path "coverage_html\index.html") {
    Start-Process "coverage_html\index.html"
    Write-Host "Report opened in your browser." -ForegroundColor Green
} else {
    Write-Host "Report could not be found." -ForegroundColor Red
    Write-Host "Check error messages above." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "You can also check:" -ForegroundColor Cyan
Write-Host "  - COVERAGE_REPORT.md (detailed analysis)" -ForegroundColor White
Write-Host "  - coverage_html/index.html (interactive HTML report)" -ForegroundColor White
Write-Host ""

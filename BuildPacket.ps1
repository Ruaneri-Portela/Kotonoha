# Caminho para o arquivo VERSION.h
$versionPath = ".\include\Kotonoha\version.h"
$versionDir = Split-Path -Path $versionPath -Parent

# Criar diret rio do arquivo de vers o, se necess rio
if (-not (Test-Path -Path $versionDir)) {
    New-Item -ItemType Directory -Path $versionDir -Force
}

# Verifique se o Git est  instalado
if (Get-Command git -ErrorAction SilentlyContinue) {
    # Obtenha o hash e a tag atual do Git
    $GIT_HASH = git rev-parse --short HEAD
    $GIT_TAG = git describe --tags --exact-match 2>$null
    if (-not $GIT_TAG) {
        $GIT_TAG = "untagged"
    }
} else {
    # Defina valores padr o se o Git n o estiver instalado
    $GIT_HASH = "Null"
    $GIT_TAG = "Null"
}

# Criar arquivo version.h
Write-Host "Criando arquivo de vers o..."
@"
#pragma once
#define KOTONOHA_VERSION_HASH "$GIT_HASH"
#define KOTONOHA_VERSION_TAG "$GIT_TAG"

"@ | Set-Content -Path $versionPath

# Diret rio para pacotes
$packetDest = ".\zips\"
$builds = @("Win32", "x64")
# Iterar sobre as plataformas
foreach ($build in $builds) {
    $msbild = if ($build -eq "Win32") { "x86" } else { $build }

    # Compilar para Release
    Write-Host "Compilando $build (Release)..."
    & .\tools\build_cmake_target.ps1 -Target SchoolDaysHQ -Configuration Release -Platform $build
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Falha na compila  o para $msbild em configura  o Release."
        exit $LASTEXITCODE
    }

    # Compilar para Debug
    Write-Host "Compilando $build (Debug)..."
    & .\tools\build_cmake_target.ps1 -Target SchoolDaysHQ -Configuration Debug -Platform $build
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Falha na compila  o para $msbild em configura  o Debug."
        exit $LASTEXITCODE
    }

    # Compactar arquivos
    Write-Host "Compactando pacotes para $build..."
     # Criar o diret rio para a plataforma
    New-Item -ItemType Directory -Path "$packetDest$build" -Force

    # Copiar os arquivos do Release gerado pelo CMake canônico
    $releaseDir = ".\build\vs\Release-$build\Release"
    Copy-Item -Path "$releaseDir\SchoolDaysHQ.exe" -Destination "$packetDest$build" -Force
    Copy-Item -Path "$releaseDir\*.dll" -Destination "$packetDest$build" -Force

    Compress-Archive -Path "$packetDest\$build\*" -DestinationPath "$packetDest\SchoolDaysHQ_MSVC_$msbild($GIT_TAG)_($GIT_HASH)_win.zip" -Force
    Remove-Item -Path "$packetDest$build" -Recurse -Force
}

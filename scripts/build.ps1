param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug",
    [switch]$Release,
    [switch]$Clean,
    [switch]$NoTests,
    [string]$Generator = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir

if ($Release) {
    $Config = "Release"
}

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Command
    )

    Write-Host ""
    Write-Host "==> $Name"
    $global:LASTEXITCODE = 0
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE"
    }
}

Invoke-Step "Configure CMake project" {
    $args = @("-S", $RepoRoot, "-B", $BuildPath, "-DCMAKE_BUILD_TYPE=$Config")
    if ($Generator.Trim().Length -gt 0) {
        $args += @("-G", $Generator)
    }
    cmake @args
}

if ($Clean) {
    Invoke-Step "Clean previous build outputs" {
        cmake --build $BuildPath --config $Config --target clean
    }
}

Invoke-Step "Build project" {
    cmake --build $BuildPath --config $Config --parallel
}

if (-not $NoTests) {
    Invoke-Step "Run tests" {
        Push-Location $BuildPath
        try {
            ctest -C $Config --output-on-failure
        }
        finally {
            Pop-Location
        }
    }
}

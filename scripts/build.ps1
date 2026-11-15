[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot),
    [switch]$Test,
    [switch]$TestRecognition
)

$ErrorActionPreference = 'Stop'
$RepositoryRoot = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "Visual Studio Installer vswhere.exe was not found: $vswhere"
}
$installationPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installationPath) {
    throw 'vswhere did not find a Visual Studio installation with the x86/x64 C++ tools.'
}
$msbuild = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    throw "MSBuild was not found in the Visual Studio installation selected by vswhere: $msbuild"
}
$project = Join-Path $RepositoryRoot 'stronghold-capture.vcxproj'
& $msbuild $project /m /v:minimal /p:Configuration=Release /p:Platform=x64 "/p:RepositoryRoot=$RepositoryRoot"
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }

if ($Test) {
    $testScript = Join-Path $PSScriptRoot 'test.ps1'
    & $testScript -RepositoryRoot $RepositoryRoot
    if ($LASTEXITCODE -ne 0) { throw "Test script failed with exit code $LASTEXITCODE" }
}

if ($TestRecognition) {
    & (Join-Path $RepositoryRoot 'build\x64\Release\stronghold-capture.exe') --self-test-recognition
    if ($LASTEXITCODE -ne 0) { throw "Recognition tests failed with exit code $LASTEXITCODE" }
    & (Join-Path $PSScriptRoot 'test-state-cli.ps1') -RepositoryRoot $RepositoryRoot
}

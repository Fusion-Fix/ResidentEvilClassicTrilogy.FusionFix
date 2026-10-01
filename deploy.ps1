param(
    [Parameter(Mandatory)][ValidateSet('RE1', 'RE2', 'RE3')][string]$Game,
    [Parameter(Mandatory)][ValidateSet('Steam', 'GOG')][string]$Store,
    [Parameter(Mandatory)][string]$GameDirectory,
    [Parameter(Mandatory)][string]$Plugin,
    [Parameter(Mandatory)][string]$Executable
)

$ErrorActionPreference = 'Stop'
$gameRoot = (Resolve-Path -LiteralPath $GameDirectory).Path
$pluginPath = (Resolve-Path -LiteralPath $Plugin).Path
$loader = Join-Path $PSScriptRoot 'data/version.dll'
if (!(Test-Path -LiteralPath $loader -PathType Leaf)) { throw "Missing Win32 Ultimate ASI Loader: $loader" }
if (!(Test-Path -LiteralPath (Join-Path $gameRoot $Executable) -PathType Leaf)) { throw "Missing debugger executable: $Executable" }
$definitions = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'games.json') -Raw | ConvertFrom-Json
$definition = $definitions | Where-Object id -eq $Game
$settings = Join-Path $PSScriptRoot "data/plugins/$($definition.project).ini"
if (!(Test-Path -LiteralPath $settings -PathType Leaf)) { throw "Missing plugin settings: $settings" }
$loaderName = if ($Store -eq 'Steam') { 'version.dll' } else { 'dsound.dll' }
$destinations = if ($Store -eq 'Steam') {
    @($definition.languages.PSObject.Properties | Where-Object {
        $language = $_.Name
        @($_.Value | Where-Object { Test-Path -LiteralPath (Join-Path $gameRoot "$language/$_") -PathType Leaf }).Count -gt 0
    } | ForEach-Object { Join-Path $gameRoot $_.Name })
} else { @($gameRoot) }
if (!$destinations.Count) { throw "No supported executables in $gameRoot" }

# Validate every destination before modifying the installation.
function Get-LoaderHash([string]$Path) {
    $hash = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)) }
    finally { $stream.Dispose(); $hash.Dispose() }
}
$loaderHash = Get-LoaderHash $loader
foreach ($destination in $destinations) {
    $installedLoader = Join-Path $destination $loaderName
    if ((Test-Path -LiteralPath $installedLoader) -and (Get-LoaderHash $installedLoader) -ne $loaderHash) {
        throw "A different $loaderName already exists: $installedLoader"
    }
}
foreach ($destination in $destinations) {
    $installedLoader = Join-Path $destination $loaderName
    if (!(Test-Path -LiteralPath $installedLoader)) {
        Copy-Item -LiteralPath $loader -Destination $installedLoader
    }
    $pluginDirectory = Join-Path $destination 'plugins'
    New-Item -ItemType Directory -Force -Path $pluginDirectory | Out-Null
    Copy-Item -LiteralPath $pluginPath -Destination $pluginDirectory -Force
    $installedSettings = Join-Path $pluginDirectory "$($definition.project).ini"
    if (!(Test-Path -LiteralPath $installedSettings)) {
        Copy-Item -LiteralPath $settings -Destination $installedSettings
    }
    $pdb = [IO.Path]::ChangeExtension($pluginPath, '.pdb')
    if (Test-Path -LiteralPath $pdb -PathType Leaf) { Copy-Item -LiteralPath $pdb -Destination $pluginDirectory -Force }
    Write-Host "Installed $Game $Store Fusion Fix: $pluginDirectory"
}

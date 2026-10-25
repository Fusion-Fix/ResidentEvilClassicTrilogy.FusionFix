param(
    [Parameter(Mandatory)][ValidateSet('RE1', 'RE2', 'RE3')][string]$Game,
    [Parameter(Mandatory)][ValidateSet('Steam', 'GOG')][string]$Store
)

$ErrorActionPreference = 'Stop'
$definitions = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'games.json') -Raw | ConvertFrom-Json
$definition = $definitions | Where-Object id -eq $Game
$name = $definition.project
$plugin = Join-Path $PSScriptRoot "bin/$Game-$Store-Release/$name.asi"
$loader = Join-Path $PSScriptRoot 'data/version.dll'
foreach ($file in @($plugin, $loader)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing package input: $file" }
}
# FusionFixHub.exe (repository root, downloaded by CI) goes next to the game's executable when present.
# The plugin writes its settings file with the defaults on the first start; the package has none.
$hub = Join-Path $PSScriptRoot 'FusionFixHub.exe'
if (!(Test-Path -LiteralPath $hub -PathType Leaf)) { $hub = $null }
$loaderName = if ($Store -eq 'Steam') { 'version.dll' } else { 'dsound.dll' }
$staging = Join-Path $PSScriptRoot ("build/packages/$Game-$Store-" + [guid]::NewGuid())
$destinations = if ($Store -eq 'Steam') {
    @($definition.languages.PSObject.Properties.Name | ForEach-Object { Join-Path $staging $_ })
} else { @($staging) }
foreach ($destination in $destinations) {
    $pluginDirectory = Join-Path $destination 'plugins'
    New-Item -ItemType Directory -Force -Path $pluginDirectory | Out-Null
    Copy-Item -LiteralPath $loader -Destination (Join-Path $destination $loaderName)
    Copy-Item -LiteralPath $plugin -Destination $pluginDirectory
    if ($hub) { Copy-Item -LiteralPath $hub -Destination $destination }
}
$output = Join-Path $PSScriptRoot "releases/$name.$Store.zip"
New-Item -ItemType Directory -Force -Path (Split-Path $output -Parent) | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
[IO.Compression.ZipFile]::CreateFromDirectory($staging, $output)
Remove-Item -LiteralPath $staging -Recurse -Force
Write-Host "Created $output"

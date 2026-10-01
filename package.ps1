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
$settings = Join-Path $PSScriptRoot "data/plugins/$name.ini"
foreach ($file in @($plugin, $loader, $settings)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing package input: $file" }
}
$loaderName = if ($Store -eq 'Steam') { 'version.dll' } else { 'dsound.dll' }
$staging = Join-Path $PSScriptRoot ("build/packages/$Game-$Store-" + [guid]::NewGuid())
New-Item -ItemType Directory -Force -Path $staging | Out-Null
if ($Store -eq 'Steam') {
    foreach ($language in $definition.languages.PSObject.Properties.Name) {
        $destination = Join-Path $staging $language
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Copy-Item -LiteralPath $loader -Destination (Join-Path $destination $loaderName)
        $pluginDirectory = Join-Path $destination 'plugins'
        New-Item -ItemType Directory -Force -Path $pluginDirectory | Out-Null
        Copy-Item -LiteralPath $plugin -Destination $pluginDirectory
        Copy-Item -LiteralPath $settings -Destination $pluginDirectory

    }
} else {
    Copy-Item -LiteralPath $loader -Destination (Join-Path $staging $loaderName)
    $destination = Join-Path $staging 'plugins'
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -LiteralPath $plugin -Destination $destination
    Copy-Item -LiteralPath $settings -Destination $destination

}
$output = Join-Path $PSScriptRoot "releases/$name.$Store.zip"
New-Item -ItemType Directory -Force -Path (Split-Path $output -Parent) | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
[IO.Compression.ZipFile]::CreateFromDirectory($staging, $output)
Write-Host "Created $output"

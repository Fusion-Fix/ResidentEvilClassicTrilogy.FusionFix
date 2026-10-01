param(
    [ValidateSet('RE1', 'RE2', 'RE3')][string[]]$Game = @('RE1', 'RE2', 'RE3')
)

$ErrorActionPreference = 'Stop'
$definitions = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'games.json') -Raw | ConvertFrom-Json
$definitions = $definitions | Where-Object { $_.id -in $Game }
foreach ($definition in $definitions) {
    $gameId = $definition.id
    $name = $definition.project
    foreach ($store in 'Steam', 'GOG') {
        $plugin = Join-Path $PSScriptRoot "bin/$gameId-$store-Release/$name.asi"
        if (!(Test-Path -LiteralPath $plugin -PathType Leaf)) { throw "Build $gameId-$store-Release first." }
    }
}
foreach ($definition in $definitions) {
    $gameId = $definition.id
    $name = $definition.project
    foreach ($store in 'Steam', 'GOG') {
        $plugin = Join-Path $PSScriptRoot "bin/$gameId-$store-Release/$name.asi"
        & (Join-Path $PSScriptRoot 'tools/EmbedPDB/EmbedPDB.exe') $plugin
        if ($LASTEXITCODE) { throw "PDB embedding failed: $plugin" }
        if ($env:CODE_SIGNING_PFX -and $env:CODE_SIGNING_PASSWORD) {
            & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'sign.ps1') -SearchPathsRaw $plugin
            if ($LASTEXITCODE) { throw "Signing failed: $plugin" }
        }
        & (Join-Path $PSScriptRoot 'package.ps1') -Game $gameId -Store $store
    }
}

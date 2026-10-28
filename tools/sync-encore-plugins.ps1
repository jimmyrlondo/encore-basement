# Copies the Encore plugins (Vox, Verb, Equalize, Master) into encore/ from
# the plugin repo they're developed in. Run after changing the plugins there:
#   powershell -File tools\sync-encore-plugins.ps1 -From C:\path\to\plugin-repo
param([Parameter(Mandatory = $true)][string]$From)
$to = Join-Path $PSScriptRoot "..\encore"
foreach ($p in "Vox", "Verb", "EQ", "Master") {
    $dest = Join-Path $to "plugins\$p\Source"
    Remove-Item $dest -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item (Join-Path $From "plugins\$p\Source\*") $dest -Recurse
}
Remove-Item (Join-Path $to "shared") -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $From "shared") (Join-Path $to "shared") -Recurse
Remove-Item (Join-Path $to "shared\dsp\lite") -Recurse -Force -ErrorAction SilentlyContinue  # browser-only stand-ins
"Synced the Encore plugins from $From"

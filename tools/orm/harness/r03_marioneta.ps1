# Builds the puppet Link pack from your own dump (nothing from the game goes to the repository).
#   powershell -ExecutionPolicy Bypass -File tools/orm/harness\r03_marioneta.ps1
# Folders (orm_paths.py): ORM_ROOT, ORM_INST_DIR (a portable instance n1 must exist there), ORM_DUMP, ORM_PUPPET_LOAD's
# parent (the pack goes to <ORM_ROOT>\puppet_user), ORM_TREE (the source tree with tools\puppet_pack), ORM_GAME_DIR.
# 1. unpack the .nup with the PORTABLE instance copy (<inst>\n1, never %APPDATA%) into <dump>
# 2. python -m puppet_pack.build --romfs <dump romfs> --user-dir <ORM_ROOT>\puppet_user
# orm_inst.prepare then copies <ORM_ROOT>\puppet_user\load into every instance.
$ErrorActionPreference = "Continue"
$Repo = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$OrmRoot = if ($env:ORM_ROOT) { $env:ORM_ROOT } else { Join-Path $HOME "orm-work" }  # see orm_paths.py
$Reports = if ($env:ORM_REPORTS) { $env:ORM_REPORTS } else { Join-Path $OrmRoot "reports" }
$LogRoot = if ($env:ORM_LOG_ROOT) { $env:ORM_LOG_ROOT } else { Join-Path $OrmRoot "logs" }
$InstDir = if ($env:ORM_INST_DIR) { $env:ORM_INST_DIR } else { Join-Path $OrmRoot "inst" }
$Dump = if ($env:ORM_DUMP) { $env:ORM_DUMP } else { Join-Path $OrmRoot "dump" }
$Tree = if ($env:ORM_TREE) { $env:ORM_TREE } else { Join-Path $OrmRoot "tree" }
$PuppetUser = Join-Path $OrmRoot "puppet_user"
New-Item -ItemType Directory -Force (Join-Path $Reports "pareja"), $LogRoot | Out-Null
$sum = Join-Path $Reports "pareja\marioneta_resumen.txt"
$log = Join-Path $LogRoot "marioneta.log"
"puppet pack $(Get-Date -Format s)" | Set-Content -Encoding utf8 $sum
"" | Set-Content -Encoding utf8 $log
$gameDirs = @($env:ORM_GAME_DIR, (Join-Path $OrmRoot "game"), (Join-Path $Repo "juego")) | Where-Object { $_ -and (Test-Path $_) }
$nup = ($gameDirs | ForEach-Object { Get-ChildItem $_ -Filter *.nup } | Select-Object -First 1).FullName
$cli = Join-Path $InstDir "n1\eden-cli.exe"
if (-not (Test-Path (Join-Path $InstDir "n1\user"))) { "FAIL: no portable instance n1 in $InstDir" | Add-Content $sum; exit 1 }
$t0 = Get-Date
if (-not (Get-ChildItem $Dump -Recurse -Directory -Filter Pack -ErrorAction SilentlyContinue | Select-Object -First 1)) {
    & $cli --unpack-nup $nup $Dump 2>&1 | ForEach-Object { "$_" } | Add-Content -Encoding utf8 $log
    "unpack exit $LASTEXITCODE in $([int]((Get-Date) - $t0).TotalSeconds) s" | Add-Content -Encoding utf8 $sum
}
$pack = Get-ChildItem $Dump -Recurse -Directory -Filter Pack -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.Parent.FullName "RSDB") } | Select-Object -First 1
if (-not $pack) { "FAIL: no romfs with Pack and RSDB under $Dump (see $log)" | Add-Content $sum; exit 1 }
$romfs = $pack.Parent.FullName
"romfs: $romfs ($([int]((Get-ChildItem $romfs -Recurse -File | Measure-Object Length -Sum).Sum / 1GB)) GB)" | Add-Content -Encoding utf8 $sum
Push-Location (Join-Path $Tree "tools")
& uv run --with zstandard python -m puppet_pack.build --romfs $romfs --user-dir $PuppetUser 2>&1 | ForEach-Object { "$_" } | Add-Content -Encoding utf8 $log
$rc = $LASTEXITCODE
Pop-Location
"build exit $rc" | Add-Content -Encoding utf8 $sum
Get-ChildItem $PuppetUser -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object { "  $($_.FullName.Substring($PuppetUser.Length)) $($_.Length)" } | Add-Content -Encoding utf8 $sum
Get-Content $log -Tail 8 | Add-Content -Encoding utf8 $sum
exit $rc

# Install build/winmm.dll and assets/nukepanel/* into the game, then hash-compare every file.
# Prints MATCH only when all match: do not launch the game until MATCH has been read.
# -SelfTest checks the ownership rule on a temp dir and touches nothing else.
# -GameDir is the game's System folder; the NUKEPANEL_GAME_DIR environment variable is the fallback.
param([switch]$SelfTest, [string]$GameDir = '')
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path (Join-Path $root '../..')).Path
$game = if ($GameDir) { $GameDir } else { $env:NUKEPANEL_GAME_DIR }

# A file is ours to overwrite when absent, or when its hash is the one we recorded writing.
function Test-Owned($dst, $recordedHash) {
  if (-not (Test-Path -LiteralPath $dst)) { return $true }
  if (-not $recordedHash) { return $false }
  return (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -eq $recordedHash
}

# A folder is DNF's System folder when it holds DukeForever.exe and Engine.dll: a slip of -GameDir must not plant a
# winmm.dll that every exe in some other folder would load.
function Test-GameFolder($dir) {
  return (Test-Path -LiteralPath (Join-Path $dir 'DukeForever.exe') -PathType Leaf) -and
         (Test-Path -LiteralPath (Join-Path $dir 'Engine.dll') -PathType Leaf)
}

if ($SelfTest) {
  $t = Join-Path ([IO.Path]::GetTempPath()) ("np-deploy-selftest-" + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $t | Out-Null
  try {
    $ours = Join-Path $t 'ours.bin'; [IO.File]::WriteAllText($ours, 'ours')
    $foreign = Join-Path $t 'foreign.bin'; [IO.File]::WriteAllText($foreign, 'foreign')
    $changed = Join-Path $t 'changed.bin'; [IO.File]::WriteAllText($changed, 'edited later')
    $oursHash = (Get-FileHash -LiteralPath $ours -Algorithm SHA256).Hash
    $cases = @(
      @{ n = 'absent';           ok = (Test-Owned (Join-Path $t 'nope.bin') $null) -eq $true },
      @{ n = 'ours';             ok = (Test-Owned $ours $oursHash) -eq $true },
      @{ n = 'foreign';          ok = (Test-Owned $foreign $null) -eq $false },
      @{ n = 'recorded-changed'; ok = (Test-Owned $changed $oursHash) -eq $false },
      @{ n = 'not-a-game-dir';   ok = (Test-GameFolder $t) -eq $false }
    )
    [IO.File]::WriteAllText((Join-Path $t 'DukeForever.exe'), 'x')
    $cases += @{ n = 'exe-without-engine'; ok = (Test-GameFolder $t) -eq $false }
    [IO.File]::WriteAllText((Join-Path $t 'Engine.dll'), 'x')
    $cases += @{ n = 'game-dir'; ok = (Test-GameFolder $t) -eq $true }
    $pass = 0; $fail = 0
    foreach ($c in $cases) { if ($c.ok) { $pass++ } else { $fail++; "FAIL $($c.n)" } }
    "deploy self-test: $pass passed, $fail failed"
    if ($fail -gt 0) { exit 1 }
    exit 0
  } finally { Remove-Item -LiteralPath $t -Recurse -Force -ErrorAction SilentlyContinue }
}

# The game folder must be named and exist; nothing is guessed, so nothing is ever written to a wrong place.
if (-not $game -or -not (Test-Path -LiteralPath $game -PathType Container)) {
  Write-Host "ERROR: the game's System folder was not given or does not exist: '$game'."
  Write-Host 'Pass -GameDir <folder> or set the NUKEPANEL_GAME_DIR environment variable.'
  exit 2
}
$game = (Resolve-Path -LiteralPath $game).Path   # a relative -GameDir works; Assert-Target compares full paths
if (-not (Test-GameFolder $game)) {
  Write-Host "REFUSED: '$game' is not Duke Nukem Forever's System folder (no DukeForever.exe and Engine.dll there)."
  exit 2
}

# Never deploy under a running game.
if (Get-Process -Name DukeForever -ErrorAction SilentlyContinue) {
  Write-Host 'REFUSED: DukeForever.exe is running'
  exit 2
}

# The only paths this script may write: GAME\winmm.dll and anything under GAME\NukePanel\.
$npDir = Join-Path $game 'NukePanel'
function Assert-Target($path) {
  $full = [IO.Path]::GetFullPath($path)
  if ($full -ieq (Join-Path $game 'winmm.dll')) { return }
  if ($full.StartsWith($npDir + '\', [StringComparison]::OrdinalIgnoreCase)) { return }
  throw "refusing to write outside winmm.dll and NukePanel\: $path"
}

# The game may hold the DLL open: retry while the file is locked.
function Copy-Retry($src, $dst) {
  Assert-Target $dst
  $deadline = (Get-Date).AddSeconds(30)
  while ($true) {
    try { Copy-Item -LiteralPath $src -Destination $dst -Force; return }
    catch [System.IO.IOException] {
      if ((Get-Date) -gt $deadline) { throw "still locked after 30 s: $dst" }
      Start-Sleep -Milliseconds 500
    }
  }
}

# pairs: src, dst, rel (path relative to GAME, the key in deployed.txt)
$pairs = @(@{ src = (Join-Path $repo 'build\winmm.dll'); dst = (Join-Path $game 'winmm.dll'); rel = 'winmm.dll' })
$assetRoot = Join-Path $repo 'assets\nukepanel'
if (Test-Path -LiteralPath $assetRoot) {
  $assetRoot = (Resolve-Path $assetRoot).Path
  Get-ChildItem -File -Recurse $assetRoot | ForEach-Object {
    $sub = $_.FullName.Substring($assetRoot.Length + 1)
    $pairs += @{ src = $_.FullName; dst = (Join-Path $npDir $sub); rel = "NukePanel\$sub" }
  }
}

# Record of what we last wrote: "sha256  relative-path" per line.
$record = Join-Path $npDir 'deployed.txt'
$recorded = @{}
if (Test-Path -LiteralPath $record) {
  foreach ($line in Get-Content -LiteralPath $record) {
    if ($line -match '^([0-9A-Fa-f]{64})\s\s(.+)$') { $recorded[$Matches[2]] = $Matches[1].ToUpper() }
  }
}

# Check every pair before the first copy: one foreign file stops the whole deploy.
foreach ($p in $pairs) {
  Assert-Target $p.dst
  if (-not (Test-Path -LiteralPath $p.src)) { throw "missing source $($p.src)" }
  if (-not (Test-Owned $p.dst $recorded[$p.rel])) {
    Write-Host "REFUSED: $($p.dst) is not ours"
    exit 3
  }
}

Assert-Target $record
New-Item -ItemType Directory -Force -Path $npDir | Out-Null
# Each file is recorded right after its own copy, and earlier entries are kept: a deploy that fails midway
# never leaves one of our files looking foreign to the next deploy.
function Save-Record {
  $lines = @($recorded.Keys | Sort-Object | ForEach-Object { "$($recorded[$_])  $_" })
  [IO.File]::WriteAllText($record, (($lines -join "`n") + "`n"))
}
foreach ($p in $pairs) {
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $p.dst) | Out-Null
  Copy-Retry $p.src $p.dst
  $recorded[$p.rel] = (Get-FileHash -LiteralPath $p.dst -Algorithm SHA256).Hash
  Save-Record
}

$bad = 0
foreach ($p in $pairs) {
  $a = (Get-FileHash -LiteralPath $p.src -Algorithm SHA256).Hash
  $b = (Get-FileHash -LiteralPath $p.dst -Algorithm SHA256).Hash
  if ($a -ne $b) { "MISMATCH $($p.dst)  src $a  dst $b"; $bad++ }
  else { "  ok $($p.dst)  $a" }
}

if ($bad -gt 0) { exit 1 }
"MATCH ($($pairs.Count) files)"

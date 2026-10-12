# Build the nukepanel proxy DLL (x86) and report its hash.
# The game is 32-bit, so this is x86 -- an x64 DLL will not load into it.
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $root '..\..')
$build = Join-Path $repo 'build'
$obj = Join-Path $build 'obj'
New-Item -ItemType Directory -Force -Path $obj | Out-Null

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
        -latest -products * -property installationPath
if (-not $vs) { throw 'Visual Studio not found' }
$msvc = Get-ChildItem "$vs\VC\Tools\MSVC" -Directory | Sort-Object Name | Select-Object -Last 1
$cl = Join-Path $msvc.FullName 'bin\Hostx64\x86\cl.exe'
if (-not (Test-Path $cl)) { throw "cl.exe not found at $cl" }

# The Windows SDK headers and libs the compiler needs. Highest complete version,
# not the last folder: a Driver Kit adds non-version folders such as Include\wdf,
# and CI images carry several SDKs.
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVer = Get-ChildItem "$sdkRoot\Include" -Directory -ErrorAction SilentlyContinue |
    Where-Object { $v = $null; [version]::TryParse($_.Name, [ref]$v) -and
                   (Test-Path "$($_.FullName)\um\windows.h") -and (Test-Path "$($_.FullName)\ucrt\stdio.h") } |
    Sort-Object { [version]$_.Name } | Select-Object -Last 1 -ExpandProperty Name
if (-not $sdkVer) { throw "No complete Windows SDK (um\windows.h and ucrt\stdio.h) under $sdkRoot\Include" }
Write-Host "Windows SDK $sdkVer"

$env:INCLUDE = @("$($msvc.FullName)\include", "$sdkRoot\Include\$sdkVer\ucrt",
                 "$sdkRoot\Include\$sdkVer\um", "$sdkRoot\Include\$sdkVer\shared") -join ';'
$env:LIB = @("$($msvc.FullName)\lib\x86", "$sdkRoot\Lib\$sdkVer\ucrt\x86",
             "$sdkRoot\Lib\$sdkVer\um\x86") -join ';'

# /MT so the DLL carries its own CRT: it loads into someone else's process and
# must not depend on a redistributable being present on a player's machine.
$common = @('/nologo', '/c', '/O2', '/EHsc', '/MT', '/Zc:preprocessor', '/DWIN32_LEAN_AND_MEAN', '/D_CRT_SECURE_NO_WARNINGS')

# THE SUITE RUNS BEFORE THE DLL IS BUILT, and fails the build on any red check.
# A red line here costs a second; a bad build found after a deploy costs a round trip.
#
# Forward slashes on purpose: cl.exe and PowerShell both accept them, and a
# backslash in a path here has already been turned into a tab once by the
# tooling that writes this file.
#
# THE TEST OBJECT GOES IN ITS OWN DIRECTORY, and that is not tidiness. The DLL
# link below globs *.obj out of $obj, so a test object written there was being
# linked into the shipped DLL -- the whole suite and its main(), inside the
# artifact players download. It linked without complaint, which is exactly why
# nothing said so.
$testObj = Join-Path $build 'obj-test'
New-Item -ItemType Directory -Force -Path $testObj | Out-Null
$testExe = Join-Path $build 'test_host.exe'
$testSrc = @(@('tests/test_host.cpp', 'config.cpp', 'paths.cpp', 'ui/ui.cpp', 'font_data.cpp') | ForEach-Object { Join-Path $root $_ })
if (Test-Path $testExe) { Remove-Item $testExe -Force }
# A failed build must leave no DLL behind for deploy.ps1 to ship.
$staleDll = Join-Path $build 'winmm.dll'
if (Test-Path $staleDll) { Remove-Item $staleDll -Force }
& $cl /nologo /O2 /W3 /WX /EHsc /MT /Zc:preprocessor /D_CRT_SECURE_NO_WARNINGS /I"$root" "/Fe:$testExe" "/Fo:$testObj/" @testSrc
if ($LASTEXITCODE -ne 0) { throw "test host failed to compile ($LASTEXITCODE)" }
# CHECK THE FILE, NOT THE EXIT CODE. cl can exit 0 without writing the exe
# under some security software, so a missing exe is reported rather than assumed built.
if (-not (Test-Path $testExe)) {
  throw "cl reported success but $testExe was never written -- THE SUITE DID NOT RUN. " +
        "Nothing here works around that."
}
# AND THE SUMMARY LINE IS THE PROOF, NOT THE EXIT CODE. Only the suite itself
# can print its `<N> passed, <M> failed` line, so requiring it makes this
# build incapable of reporting a pass it did not observe.
$suiteOut = & $testExe
$suiteCode = $LASTEXITCODE
$suiteOut | ForEach-Object { "  $_" }
$summary = @($suiteOut | Where-Object { $_ -match '^\s*\d+ passed, \d+ failed\s*$' })
if ($summary.Count -ne 1) {
  throw "the suite printed no '<N> passed, <M> failed' line -- THE SUITE DID NOT RUN. " +
        "Nothing here works around that."
}
if ($suiteCode -ne 0) { throw 'SUITE FAILED -- not building the DLL' }

# The link below takes every *.obj in $obj, so an object whose source was
# deleted would keep linking into the DLL: clear them before compiling.
Get-ChildItem $obj -Filter *.obj | Remove-Item -Force
Push-Location $obj
try {
  $ourSrc = @('proxy.cpp', 'proxy_stubs.cpp', 'log.cpp', 'paths.cpp', 'boot.cpp', 'hook.cpp',
              'stockgate.cpp', 'd3dcap.cpp', 'prim_d3d9.cpp', 'frame.cpp', 'font_data.cpp', 'canvas.cpp',
              'fontgen.cpp', 'clipboard.cpp', 'config.cpp', 'iat.cpp', 'input.cpp', 'panel.cpp', 'ui/menu.cpp',
              'ui/ui.cpp', 'fileio.cpp', 'stall.cpp', 'framecap.cpp', 'engine.cpp', 'bigpackage.cpp',
              'dimouse.cpp', 'crashlog.cpp', 'uname.cpp', 'uobj.cpp', 'uprop.cpp', 'world.cpp',
              'esp.cpp', 'dmgnum.cpp', 'scall.cpp', 'fov.cpp', 'gamespeed.cpp', 'accuracy.cpp', 'handling.cpp', 'turretwep.cpp', 'display.cpp',
              'cvars.cpp', 'postfx.cpp', 'focuskeep.cpp', 'pehook.cpp', 'rawmouse.cpp', 'noshake.cpp',
              'progress.cpp', 'levelstats.cpp', 'egoesp.cpp', 'autowin.cpp', 'autoexec.cpp', 'worldscan.cpp',
              'items.cpp', 'breakables.cpp', 'collect.cpp', 'modelbox.cpp', 'dukevision.cpp', 'aim.cpp', 'tbot.cpp', 'aimbot.cpp',
              'spawnbox.cpp', 'aircraft.cpp', 'overkill.cpp', 'freecam.cpp', 'fbprobe.cpp', 'fblift.cpp', 'teleport.cpp', 'photo.cpp', 'photofolder.cpp', 'achreset.cpp', 'achhelp.cpp', 'pbtrace.cpp', 'arctrace.cpp', 'pinbot.cpp', 'arcbot.cpp', 'lvnames.cpp') | ForEach-Object { Join-Path $root $_ }
  & $cl @common /W3 /WX /I"$root" @ourSrc
  if ($LASTEXITCODE -ne 0) { throw "nukepanel compile failed ($LASTEXITCODE)" }

  # /DEPENDENTLOADFLAG:0x800 (LOAD_LIBRARY_SEARCH_SYSTEM32): bcrypt.dll is not a KnownDLL, so without it a bcrypt.dll
  # dropped beside the game would load first. Checked in the PE below.
  $def = Join-Path $root 'winmm.def'
  & $cl /nologo /LD /MT (Get-ChildItem $obj -Filter *.obj | ForEach-Object { $_.FullName }) `
        /link /DLL /DEF:"$def" /OUT:"$build\winmm.dll" /DEPENDENTLOADFLAG:0x800 `
        kernel32.lib user32.lib gdi32.lib bcrypt.lib
  if ($LASTEXITCODE -ne 0) { throw "link failed ($LASTEXITCODE)" }
} finally { Pop-Location }

$out = Join-Path $build 'winmm.dll'
if (-not (Test-Path $out)) { throw "no output at $out" }
Get-ChildItem $build -File | Where-Object { $_.Extension -in '.exp', '.lib' } | Remove-Item -Force -EA SilentlyContinue

# EXPORT PARITY IS A BUILD GATE, NOT A POST-HOC CHECK. A build that cannot be
# loaded is a failed build, so it fails here. Parity reference is
# SysWOW64\winmm.dll: the game is 32-bit, so System32 holds the 64-bit copy.
$dumpbin = Join-Path $msvc.FullName 'bin\Hostx64\x86\dumpbin.exe'
function Get-Exports($path) {
  $r = & $dumpbin /nologo /exports $path |
       Select-String -Pattern '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)' |
       ForEach-Object { ($_.Line.Trim() -split '\s+')[3] }
  return @($r)   # force an array: a single result must not collapse to a scalar
}
$mine = Get-Exports $out
$real = Get-Exports "$env:SystemRoot\SysWOW64\winmm.dll"
if ($real.Count -eq 0) { throw 'could not read the real winmm.dll exports' }
$missing = @($real | Where-Object { $mine -notcontains $_ })
if ($missing.Count -gt 0) {
  throw "proxy is missing $($missing.Count) export(s) the real winmm.dll has: $($missing -join ', ')"
}

# Every static import resolves from System32 only: the load config's DependentLoadFlags must read 0x800.
$dlf = & $dumpbin /nologo /loadconfig $out | Select-String -Pattern '^\s*([0-9A-F]+) Dependent Load Flag'
if (-not $dlf -or ([Convert]::ToInt32($dlf.Matches[0].Groups[1].Value, 16) -band 0x800) -eq 0) {
  throw "the DLL's DependentLoadFlags lacks LOAD_LIBRARY_SEARCH_SYSTEM32 (0x800): imports would use the search order"
}

"built $out"
"  {0:N0} bytes" -f (Get-Item $out).Length
"  exports {0}/{1} (parity with SysWOW64\winmm.dll)" -f $mine.Count, $real.Count
"  sha256 $((Get-FileHash $out -Algorithm SHA256).Hash)"

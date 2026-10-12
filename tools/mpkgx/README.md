# mpkgx (MegaPackageExtractor)

- Source: https://github.com/DaZombieKiller/MegaPackageExtractor (the master branch at the time)
- Clone: a local checkout of MegaPackageExtractor outside this repo (built from source, no prebuilt binaries)
- Licence: MIT (c) 2022 Benjamin Moir
- Build (net7.0 project retargeted to the installed SDK 10.0.112; AOT/trim off):
  `dotnet restore -p:TargetFramework=net10.0 -p:PublishAot=false -p:PublishTrimmed=false`
  `dotnet build --no-restore -c Release -p:TargetFramework=net10.0 -p:RollForward=Major -p:PublishAot=false -p:PublishTrimmed=false`
- Run: `mpkgx.exe <MegaPackage.dat> <outdir>`; it sorts output into System\ (.u), Textures\, StaticMeshes\, etc.
  We keep only `System\*.u` in `research\packages\` (gitignored).

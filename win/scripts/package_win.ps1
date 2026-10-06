# powershell -ExecutionPolicy Bypass -File E:\Chronicle-project-Claude\package_win.ps1
# Builds a small zip a friend can unzip and run: darkcloud.exe + SDL3.dll + ChronicleLauncher.exe + a short README. No game data and no mods are included.
$root = "E:\Chronicle-project-Claude"
$build = "$root\Chronicle-win-build"
$dist = "$root\dist\DarkCloud-Windows"
$exe = "$build\darkcloud.exe"
$sdl = Get-ChildItem "$root\win-deps\SDL3-*\lib\x64\SDL3.dll" | Sort-Object FullName | Select-Object -Last 1
if (-not (Test-Path $exe)) { "darkcloud.exe not found - run build_win.ps1 first."; return }
if (-not $sdl) { "SDL3.dll not found under win-deps - run setup_win_deps.ps1."; return }
Remove-Item $dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item $exe $dist
$launcher = "$root\ChronicleLauncher.exe"
if (Test-Path $launcher) { Copy-Item $launcher $dist } else { "note: ChronicleLauncher.exe not found (run build_launcher.ps1); packaging without it." }
Copy-Item $sdl.FullName $dist          # the SDL3.dll the exe was linked against
$lic = Join-Path (Split-Path (Split-Path (Split-Path $sdl.FullName))) "LICENSE.txt"
if (Test-Path $lic) { Copy-Item $lic "$dist\SDL3-LICENSE.txt" }
@"
Dark Cloud (PAL) - Windows build
================================
You need: a Windows 10/11 PC, a graphics card with an up-to-date driver (Vulkan 1.3 or newer),
and your own disc image of the European (PAL) release of Dark Cloud.  No game data is included here.

1. Double-click ChronicleLauncher.exe (settings, co-op and mods in one window), or darkcloud.exe to start the game directly.
2. When asked, choose your disc image (.iso). The game extracts what it needs; this takes a few minutes, once.
3. A settings screen follows. Pick your screen mode, frame rate limit and so on, then press "Save and start".

Later
- Change settings again: run  darkcloud.exe --setup
- Advanced settings: config.json in the save folder (shown in the console / next to the game's save data).
- Texture mods: put PNG files in  mods\<your mod name>\textures\  inside the save folder; start the game with the
  environment variable DC_DUMP_TEXTURES=1 once to export every texture, then rename/replace the ones you want.

Mods (Lua scripts, PNG textures, 3D models): see win/docs/SCRIPTING.md, MODELS.md and TEXTURE_MODS.txt in the source repository below.

This is a native Windows build of the open-source Chronicle port.  Source and build instructions:
https://github.com/jeffiest/Chronicle-Windows   (branch: windows)
"@ | Set-Content "$dist\README.txt" -Encoding UTF8
$zip = "$root\dist\DarkCloud-Windows.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path "$dist\*" -DestinationPath $zip
"packaged: $zip"
Get-ChildItem $dist | Format-Table Name, Length

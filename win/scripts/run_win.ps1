# powershell -ExecutionPolicy Bypass -File E:\Chronicle-project-Claude\run_win.ps1 [-Width 1280] [-Height 720] [-Data <path>]
# Runs the Windows-native build on the GPU (Windows Vulkan). Data defaults to the PAL data already extracted in WSL.
param([int]$Width = 1280, [int]$Height = 720, [string]$Data = "", [string]$Extra = "", [switch]$DumpTextures, [switch]$DumpModels, [switch]$DumpData, [switch]$DumpText, [switch]$Guest)
$root = "E:\Chronicle-project-Claude"
$exe = "$root\Chronicle-win-build\darkcloud.exe"
if (-not $Data) { $Data = (wsl -- bash -lc 'wslpath -w ~/Chronicle/data').Trim() }
"data: $Data"
if (-not (Test-Path "$Data")) { "DATA FOLDER NOT FOUND: $Data  (pass -Data <folder with the extracted PAL data>)"; return }
$save = "$root\win-save"; New-Item -ItemType Directory -Force $save | Out-Null
if ($Guest) { # second instance for multiplayer testing: own save folder, same mods (junction), own config copy
    $save = "$root\win-save-guest"; New-Item -ItemType Directory -Force $save | Out-Null
    if (-not (Test-Path "$save\mods")) { New-Item -ItemType Junction -Path "$save\mods" -Target "$root\win-save\mods" | Out-Null }
    foreach ($f in "config.json", "setup_done.txt") { if (-not (Test-Path "$save\$f") -and (Test-Path "$root\win-save\$f")) { Copy-Item "$root\win-save\$f" "$save\$f" } }
    if (-not (Test-Path "$save\mc0") -and (Test-Path "$root\win-save\mc0")) { Copy-Item "$root\win-save\mc0" "$save\mc0" -Recurse }
    "GUEST instance, save folder: $save"
}
$env:DC_PRESENT_STATS = "1"
if ($DumpTextures) { $env:DC_DUMP_TEXTURES = "1"; "dumping every texture the game loads to $save\mods\_dump" }
if ($DumpText) { $env:DC_DUMP_TEXT = "1"; "dumping every message file the game loads to $save\mods\_dump\text (open the menus to load more)" }
if ($DumpData) { $env:DC_DUMP_DATA = "1"; "dumping the game data tables to $save\mods\_dump\data" }
if ($DumpModels) { $env:DC_DUMP_MODELS = "1"; "dumping every 3D model the game builds to $save\mods\_dump\models" }
& $exe --data "$Data" --save "$save" --width $Width --height $Height $Extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
"exit code: $LASTEXITCODE (hex 0x{0:X})" -f $LASTEXITCODE

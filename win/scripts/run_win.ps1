# powershell -ExecutionPolicy Bypass -File E:\Chronicle-project-Claude\run_win.ps1 [-Width 1280] [-Height 720] [-Data <path>]
# Runs the Windows-native build on the GPU (Windows Vulkan). Data defaults to the PAL data already extracted in WSL.
param([int]$Width = 1280, [int]$Height = 720, [string]$Data = "", [string]$Extra = "", [switch]$DumpTextures)
$root = "E:\Chronicle-project-Claude"
$exe = "$root\Chronicle-win-build\darkcloud.exe"
if (-not $Data) { $Data = (wsl -- bash -lc 'wslpath -w ~/Chronicle/data').Trim() }
"data: $Data"
if (-not (Test-Path "$Data")) { "DATA FOLDER NOT FOUND: $Data  (pass -Data <folder with the extracted PAL data>)"; return }
$save = "$root\win-save"; New-Item -ItemType Directory -Force $save | Out-Null
$env:DC_PRESENT_STATS = "1"
if ($DumpTextures) { $env:DC_DUMP_TEXTURES = "1"; "dumping every texture the game loads to $save\mods\_dump" }
& $exe --data "$Data" --save "$save" --width $Width --height $Height $Extra.Split(' ', [StringSplitOptions]::RemoveEmptyEntries)
"exit code: $LASTEXITCODE (hex 0x{0:X})" -f $LASTEXITCODE

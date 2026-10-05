# powershell -ExecutionPolicy Bypass -File E:\Chronicle-project-Claude\build_launcher.ps1
# Builds ChronicleLauncher.exe (one file) from Chronicle-src\win\launcher.py + mod_manager.py with PyInstaller. Needs: python -m pip install pyinstaller
$root = "E:\Chronicle-project-Claude"
$src = "$root\Chronicle-src\win"
$work = "$root\Chronicle-launcher-build"
python -m PyInstaller --noconfirm --clean --onefile --windowed --name ChronicleLauncher --paths $src --distpath $work\dist --workpath $work\build --specpath $work "$src\launcher.py"
if (Test-Path "$work\dist\ChronicleLauncher.exe") {
    Copy-Item "$work\dist\ChronicleLauncher.exe" "$root\ChronicleLauncher.exe" -Force
    "built: $root\ChronicleLauncher.exe"
} else { "launcher build failed" }

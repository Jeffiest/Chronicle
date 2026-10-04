# powershell -ExecutionPolicy Bypass -File E:\Chronicle-project-Claude\setup_win_deps.ps1
# 1) show what VS18's Llvm folder really contains  2) download SDL3 (VC dev zip) 3) download the Vulkan SDK installer (for glslang + headers + loader)
$ErrorActionPreference = 'Continue'
$llvm = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin"
"== Llvm\x64\bin =="; Get-ChildItem $llvm -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name
$cl = Get-ChildItem $llvm -Filter 'clang*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($cl) { & $cl.FullName --version }
$deps = "E:\Chronicle-project-Claude\win-deps"; New-Item -ItemType Directory -Force $deps | Out-Null
$sdlVer = "3.4.18"
$sdlZip = "$deps\SDL3-devel-$sdlVer-VC.zip"
if (-not (Test-Path "$deps\SDL3-$sdlVer")) {
  $u = "https://github.com/libsdl-org/SDL/releases/download/release-$sdlVer/SDL3-devel-$sdlVer-VC.zip"
  "Downloading $u"
  try { Invoke-WebRequest $u -OutFile $sdlZip -UseBasicParsing; Expand-Archive $sdlZip $deps -Force; "SDL3 ready: $deps\SDL3-$sdlVer" } catch { "SDL3 download FAILED: $_" }
} else { "SDL3 already at $deps\SDL3-$sdlVer" }
$vk = "$deps\vulkan-sdk-installer.exe"
if (-not (Test-Path $vk)) {
  $u = "https://sdk.lunarg.com/sdk/download/latest/windows/vulkan-sdk.exe"
  "Downloading $u (large)"
  try { Invoke-WebRequest $u -OutFile $vk -UseBasicParsing; "Downloaded: $vk ($([math]::Round((Get-Item $vk).Length/1MB)) MB)" } catch { "Vulkan SDK download FAILED: $_" }
}
"Next: run  $vk --accept-licenses --default-answer --confirm-command install  (elevated PowerShell), then open a NEW terminal."

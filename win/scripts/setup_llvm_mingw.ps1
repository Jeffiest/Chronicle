# powershell -ExecutionPolicy Bypass -File E:\Chronicle-project-Claude\setup_llvm_mingw.ps1
# Downloads the latest llvm-mingw (clang + lld + libc++ + mingw-w64 UCRT headers/libs, x86_64) into win-deps\llvm-mingw and smoke-tests C++26.
$ErrorActionPreference = 'Stop'
$deps = "E:\Chronicle-project-Claude\win-deps"; New-Item -ItemType Directory -Force $deps | Out-Null
$dest = "$deps\llvm-mingw"
if (-not (Test-Path "$dest\bin\clang++.exe")) {
  $rel = Invoke-RestMethod "https://api.github.com/repos/mstorsjo/llvm-mingw/releases/latest" -Headers @{ 'User-Agent' = 'chronicle-setup' }
  $asset = $rel.assets | Where-Object { $_.name -match 'ucrt-x86_64\.zip$' } | Select-Object -First 1
  "Release $($rel.tag_name): $($asset.name) ($([math]::Round($asset.size/1MB)) MB)"
  $zip = "$deps\$($asset.name)"
  Invoke-WebRequest $asset.browser_download_url -OutFile $zip -UseBasicParsing
  $tmp = "$deps\llvm-mingw-extract"; if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
  Expand-Archive $zip $tmp -Force
  $inner = Get-ChildItem $tmp -Directory | Select-Object -First 1
  Move-Item $inner.FullName $dest
  Remove-Item $tmp -Recurse -Force
}
& "$dest\bin\clang++.exe" --version
# C++26 smoke test: compile and run
$t = "$deps\hello26.cpp"
@'
#include <print>
#include <ranges>
#include <vector>
int main() { std::vector<int> v{1,2,3}; for (int x : v | std::views::reverse) std::print("{} ", x); std::println("long={} ptr={}", sizeof(long), sizeof(void*)); }
'@ | Set-Content $t
& "$dest\bin\clang++.exe" -std=c++2c -O1 $t -o "$deps\hello26.exe"
if ($LASTEXITCODE -eq 0) { & "$deps\hello26.exe" } else { "C++26 smoke test FAILED to compile" }

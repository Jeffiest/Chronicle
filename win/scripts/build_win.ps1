# powershell -ExecutionPolicy Bypass -File E:\Chronicle-project-Claude\build_win.ps1
# Generates the Windows source tree, configures and builds the Windows-native port. Logs: win-configure.log, win-build.log
$root = "E:\Chronicle-project-Claude"
$mingw = "$root\win-deps\llvm-mingw\bin"
$cm = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake"
$env:PATH = "$mingw;$cm\Ninja;$cm\CMake\bin;C:\Python313;$env:PATH"
$env:VULKAN_SDK = if ($env:VULKAN_SDK) { $env:VULKAN_SDK } else { "C:\VulkanSDK\1.4.363.0" }
python "$root\Chronicle-src\win\gen_win_src.py"
$src = "$root\Chronicle-win-src"; $bld = "$root\Chronicle-win-build"
cmake -S $src -B $bld -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER="$mingw\clang++.exe" -DCMAKE_C_COMPILER="$mingw\clang.exe" 2>&1 | Tee-Object "$root\win-configure.log"
if ($LASTEXITCODE -ne 0) { "CONFIGURE FAILED (see win-configure.log)"; return }
cmake --build $bld -- -k 0 2>&1 | Tee-Object "$root\win-build.log" | Select-Object -Last 3
"---- errors (first 40) ----"
Select-String -Path "$root\win-build.log" -Pattern 'error:|undefined symbol|duplicate symbol|FAILED:' | Select-Object -First 40 | ForEach-Object { $_.Line }
python "$root\Chronicle-src\win\link_report.py" "$root\win-build.log"
"error count: " + (Select-String -Path "$root\win-build.log" -Pattern 'error:' | Measure-Object).Count
# A call to an unresolved weak symbol links silently and crashes at run time (a patch's helper hidden in an anonymous namespace did this once):
# every call that lands on address 0x100000000 is such a call, and the pass line is 0.
$nulls = (& "$mingw\llvm-objdump.exe" -d --no-show-raw-insn "$bld\darkcloud.exe" 2>$null | Select-String 'callq.*0x100000000' | Measure-Object).Count
"unresolved weak calls: $nulls"

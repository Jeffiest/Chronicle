# Builds and runs the multiplayer transport self test (host + 2 guest processes on 127.0.0.1).
$root = "E:\Chronicle-project-Claude"
$clang = (Get-ChildItem "$root\win-deps\llvm-mingw*\bin\clang++.exe" | Select-Object -First 1).FullName
$out = "$env:TEMP\net_selftest.exe"
# net.cpp includes "platform/net.hpp": give it a shim include dir that maps that to win\net.hpp
$shim = "$env:TEMP\net_shim\platform"; New-Item -ItemType Directory -Force $shim | Out-Null
Copy-Item "$root\Chronicle-src\win\net.hpp" "$shim\net.hpp" -Force
& $clang -std=c++20 -O1 -static "-I$env:TEMP\net_shim" "$root\Chronicle-src\win\tests\net_selftest.cpp" "$root\Chronicle-src\win\net_win.cpp" -lws2_32 -o $out
if ($LASTEXITCODE -ne 0) { "compile failed"; exit 1 }
& $out
exit $LASTEXITCODE

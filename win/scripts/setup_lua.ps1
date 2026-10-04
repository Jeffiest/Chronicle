# powershell -ExecutionPolicy Bypass -File E:\Chronicle-project-Claude\setup_lua.ps1
# Downloads the Lua 5.4 source from lua.org into win-deps (compiled into the game by build_win.ps1).
$ErrorActionPreference = 'Stop'
$deps = Join-Path $PSScriptRoot 'win-deps'
New-Item -ItemType Directory -Force $deps | Out-Null
$ver = '5.4.7'
$dir = Join-Path $deps "lua-$ver"
if (Test-Path "$dir\src\lua.h") { "Lua $ver already at $dir"; exit 0 }
$tgz = Join-Path $deps "lua-$ver.tar.gz"
Invoke-WebRequest "https://www.lua.org/ftp/lua-$ver.tar.gz" -OutFile $tgz -UseBasicParsing
tar -xzf $tgz -C $deps
if (Test-Path "$dir\src\lua.h") { "Lua ready: $dir. Now run build_win.ps1" } else { "Lua extraction failed" }

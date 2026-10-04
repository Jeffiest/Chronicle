# powershell -ExecutionPolicy Bypass -File E:\Chronicle-project-Claude\copy_data_win.ps1
# Copies the WSL-extracted data to a normal Windows folder and logs every file Windows could not copy (invalid names etc.).
$src = (wsl -- bash -lc 'wslpath -w ~/Chronicle/data').Trim()
$dst = "E:\Chronicle-project-Claude\win-data"
robocopy "$src" "$dst" /E /R:0 /W:0 /NP /NFL /NDL /LOG:E:\Chronicle-project-Claude\robocopy.log | Out-Null
Get-Content E:\Chronicle-project-Claude\robocopy.log -Tail 14
"files in win-data: " + (Get-ChildItem $dst -Recurse -File | Measure-Object).Count

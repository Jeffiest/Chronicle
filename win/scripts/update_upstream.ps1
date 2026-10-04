# powershell -ExecutionPolicy Bypass -File E:\Chronicle-project-Claude\update_upstream.ps1
# 1) pulls the latest upstream port branch in WSL, 2) re-exports the source to Chronicle-src, 3) downloads SDL 3.4.18,
# 4) regenerates the Windows source (shows which patches still apply). Then run build_win.ps1.
$root = "E:\Chronicle-project-Claude"
"--- WSL repo remotes / branch ---"
wsl -- bash -lc 'cd ~/Chronicle && git remote -v && git branch --show-current && git status --short | head -5'
"--- pulling ---"
wsl -- bash -lc 'cd ~/Chronicle && git fetch origin && git merge --ff-only origin/port && git log --oneline -3'
if ($LASTEXITCODE -ne 0) { "PULL FAILED (see above). Tell Claude what it printed."; return }
"--- exporting source ---"
wsl -- bash -lc 'bash /mnt/e/Chronicle-project-Claude/export_src_for_win.sh'
"--- SDL 3.4.18 ---"
powershell -ExecutionPolicy Bypass -File "$root\setup_win_deps.ps1"
"--- regenerating Windows source ---"
python "$root\Chronicle-src\win\gen_win_src.py"

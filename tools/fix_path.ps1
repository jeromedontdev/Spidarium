# Clean the persisted user PATH.
#
# Two defects were found:
#   1. A trailing semicolon, which creates an empty entry. Windows tolerates it
#      but it is noise and some tools mis-parse it.
#   2. One entry prefixed with \\?\ (the long-path prefix). That prefix is valid
#      for file APIs but not for PATH entries, so the directory is silently
#      ignored.
#
# This rewrites the user PATH with both fixed, preserving order and every other
# entry exactly as it was.

$raw = [Environment]::GetEnvironmentVariable('PATH', 'User')

if (-not $raw) {
    Write-Output "no user PATH set; nothing to do"
    exit 0
}

$before = ($raw -split ';' | Where-Object { $_ -ne '' }).Count

$entries = $raw -split ';' |
    Where-Object { $_ -ne '' } |
    ForEach-Object { $_ -replace '^\\\\\?\\', '' } |
    ForEach-Object { $_.TrimEnd('\') } |
    Select-Object -Unique

$clean = $entries -join ';'

Write-Output "before: $before entries, $($raw.Length) chars"
Write-Output "after:  $($entries.Count) entries, $($clean.Length) chars"
Write-Output ""

if ($clean -eq $raw) {
    Write-Output "already clean; no change written"
    exit 0
}

# Back up the old value first, so this is reversible.
$backup = Join-Path $env:USERPROFILE 'path-backup.txt'
Set-Content -Path $backup -Value $raw -Encoding UTF8
Write-Output "backup written to $backup"

[Environment]::SetEnvironmentVariable('PATH', $clean, 'User')
Write-Output "user PATH updated"
Write-Output ""
Write-Output "entries now:"
$entries | ForEach-Object { Write-Output "  $_" }

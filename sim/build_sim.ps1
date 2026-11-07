# Build the headless PC simulator.
#
# The simulator compiles the REAL game logic modules for the host, with a small
# stub layer for the LVGL and hardware calls they make. It needs a host C
# compiler -- the ESP32 toolchain only targets Xtensa and cannot produce a
# Windows executable.
#
# If no compiler is found this script explains how to get one rather than
# failing with a wall of linker errors.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sim  = $PSScriptRoot

function Find-Compiler {
    foreach ($c in 'gcc', 'clang', 'cc') {
        $p = Get-Command $c -ErrorAction SilentlyContinue
        if ($p) { return $p.Source }
    }
    # Common install locations that may not be on PATH yet.
    $candidates = @(
        'C:\msys64\ucrt64\bin\gcc.exe',
        'C:\msys64\mingw64\bin\gcc.exe',
        'C:\MinGW\bin\gcc.exe',
        'C:\Program Files\LLVM\bin\clang.exe'
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    # winget WinLibs installs under LOCALAPPDATA.
    $winlibs = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" `
                             -Filter 'gcc.exe' -Recurse -ErrorAction SilentlyContinue |
               Select-Object -First 1
    if ($winlibs) { return $winlibs.FullName }

    return $null
}

$cc = Find-Compiler
if (-not $cc) {
    Write-Host ""
    Write-Host "No host C compiler found." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "The ESP32 toolchain only targets Xtensa, so it cannot build a"
    Write-Host "Windows executable. Install a host compiler with either:"
    Write-Host ""
    Write-Host "  winget install BrechtSanders.WinLibs.POSIX.UCRT" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "then re-run this script. You may need a new terminal so PATH"
    Write-Host "picks up the install."
    Write-Host ""
    exit 1
}

Write-Host "compiler: $cc"

# The logic modules. Deliberately excludes anything that draws: spider.c,
# terrarium.c, ui_*.c and assets.c are all presentation.
$sources = @(
    "$sim\sim_main.c",
    "$root\main\behaviour.c",
    "$root\main\prey.c",
    "$root\main\needs.c",
    "$root\main\mind.c",
    "$root\main\gift.c",
    "$root\main\species.c",
    "$root\main\theme.c"
)

$out = "$sim\spidarium-sim.exe"

$args = @(
    '-std=c11', '-O1', '-g',
    '-Wall', '-Wextra',
    '-Wno-unused-parameter',
    "-I$root\main",
    "-I$sim"
) + $sources + @('-o', $out, '-lm')

Write-Host "building..."
& $cc @args
if ($LASTEXITCODE -ne 0) {
    Write-Host "build failed" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "built: $out" -ForegroundColor Green
Write-Host ""
Write-Host "run:  $out --minutes 60"
Write-Host "      $out --minutes 10 --verbose"

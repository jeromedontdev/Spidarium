# Reusable ESP-IDF build script (manually-constructed env, bypasses idf_tools.py
# because this install uses a nested tool layout: tools\<name>\<ver>\<name>\bin)
$ErrorActionPreference = 'Continue'
$py   = 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$idf  = 'C:\Espressif\frameworks\esp-idf-v5.5.6'

$env:IDF_PATH            = $idf
$env:IDF_TOOLS_PATH      = 'C:\Espressif\tools'
$env:IDF_TARGET          = 'esp32s3'
$env:ESP_ROM_ELF_DIR     = 'C:\Espressif\tools\esp-rom-elfs\20260528'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\python_env\idf5.5_py3.11_env'
$env:IDF_CCACHE_ENABLE   = '0'

$bin = @(
  'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts'
  'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin'
  'C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin'
  'C:\Espressif\tools\riscv32-esp-elf-gdb\17.1_20260402\riscv32-esp-elf-gdb\bin'
  'C:\Espressif\tools\xtensa-esp-elf-gdb\17.1_20260402\xtensa-esp-elf-gdb\bin'
  'C:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin'
  'C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260831\openocd-esp32\bin'
  'C:\Espressif\tools\ninja\1.12.1'
  'C:\Espressif\tools\cmake\3.30.2\bin'
  'C:\Espressif\tools\idf-exe\1.0.3'
  'C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64'
  'C:\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64'
  'C:\Espressif\tools\idf-git\2.44.0\mingw64\bin'
)
$env:PATH = ($bin -join ';') + ';' + $env:PATH

Set-Location 'C:\Users\jerom\.cline\data\workspaces\chat\spidarium'

$scriptArgs = @((Join-Path $idf 'tools\idf.py')) + @('build')
$out = & $py $scriptArgs 2>&1
$code = $LASTEXITCODE
$out | Tee-Object -FilePath 'C:\Users\jerom\.cline\data\workspaces\chat\spidarium\build.log' | Select-Object -Last 40
Write-Host "=== idf.py build exited with code $code ==="

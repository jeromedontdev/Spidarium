# Reusable ESP-IDF flash script
$ErrorActionPreference = 'Continue'
$py   = 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$idf  = 'C:\Espressif\frameworks\esp-idf-v5.5.6'

# Fix: grab the port safely from arguments or default to COM7 (matching your hardware)
$port = if ($args.Count -ge 1 -and -not [string]::IsNullOrEmpty($args[0])) { $args[0] } else { 'COM7' }

$env:IDF_PATH              = $idf
$env:IDF_TOOLS_PATH        = 'C:\Espressif\tools'
$env:IDF_TARGET            = 'esp32s3'
$env:ESP_ROM_ELF_DIR       = 'C:\Espressif\tools\esp-rom-elfs\20260528'
$env:IDF_PYTHON_ENV_PATH   = 'C:\Espressif\python_env\idf5.5_py3.11_env'
$env:IDF_CCACHE_ENABLE     = '0'
$env:ESP_IDF_VERSION       = '5.5.6'
$env:PYTHONNOUSERSITE      = 'True'
$env:PYTHONPATH            = $null
$env:PYTHONHOME            = $null

$bin = @(
  'C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin'
  'C:\Espressif\tools\esp-clang\esp-19.1.2_20250312\esp-clang\bin'
  'C:\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin'
  'C:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin'
  'C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260831\openocd-esp32\bin'
  'C:\Espressif\tools\ninja\1.12.1'
  'C:\Espressif\tools\cmake\3.30.2\bin'
  'C:\Espressif\tools\idf-exe\1.0.3'
  'C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64'
  'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts'
  'C:\Espressif\frameworks\esp-idf-v5.5.6\tools'
)
$env:PATH = ($bin -join ';') + ';' + $env:PATH

Set-Location $PSScriptRoot

Write-Host "Flashing target 'esp32s3' on port $port..."
$out  = & $py (Join-Path $idf 'tools\idf.py') -p $port flash 2>&1
$code = $LASTEXITCODE
$out | Tee-Object -FilePath (Join-Path $PSScriptRoot 'build\_flash.log') |
    Select-Object -Last 45
Write-Host "=== idf.py flash exited with code $code (port $port) ==="
exit $code
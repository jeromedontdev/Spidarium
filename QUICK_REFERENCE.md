# Developer quick reference

```powershell
powershell -File build.ps1 *> build\build_check.txt 2>&1
powershell -File flash.ps1 COM7
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\asset_build.py --packed build\mmap_build\assets\storage\storage.bin
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' tools\test_console_client.py
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' run_tests.py COM7 build\test_results.txt --gameplay
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' stability_check.py COM7 40
```

Use [the console reference](docs/serial_console.md) for `scan`, `scanloop`,
`render`, `wifi`, `rtc`, `gifttest`, and menu commands.
Use [the handoff](docs/AGENT_HANDOFF.md) for actual gate results.
Use [release verification](docs/RELEASE_VERIFICATION.md) and
[installation notes](docs/RELEASE_v1.0.0.md) for the first official firmware.
Close the browser's serial connection before running console commands.

Rules: LVGL owns its object tree; slow jobs run on workers; CPU-only allocations
prefer PSRAM; networking pauses the adapter with acknowledgement and fully
tears down its radio before display resume.

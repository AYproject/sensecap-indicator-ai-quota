$ErrorActionPreference='Stop'
$pidFile=Join-Path $PSScriptRoot 'private\bridge.pid'
if (-not (Test-Path -LiteralPath $pidFile)) { Write-Output 'No recorded quota bridge.'; exit 0 }
$bridgePid=[int](Get-Content -LiteralPath $pidFile -Raw).Trim()
$process=Get-CimInstance Win32_Process -Filter "ProcessId=$bridgePid" -ErrorAction SilentlyContinue
if ($process -and $process.CommandLine -like ('*'+(Join-Path $PSScriptRoot 'bridge\monitor.py')+'*')) {
    Set-Content -LiteralPath (Join-Path $PSScriptRoot 'private\stop.request') -Value 'stop' -Encoding ascii
    for ($attempt=0; $attempt -lt 10; $attempt++) {
        if (-not (Get-Process -Id $bridgePid -ErrorAction SilentlyContinue)) { break }
        Start-Sleep -Milliseconds 250
    }
    if (Get-Process -Id $bridgePid -ErrorAction SilentlyContinue) {
        # Only the previously verified demo process tree, including an active quota reader.
        & taskkill.exe /PID $bridgePid /T /F | Out-Null
    }
    Write-Output 'D1 quota bridge stopped.'
} elseif ($process) { throw 'Recorded PID belongs to another process; not stopping it.' }

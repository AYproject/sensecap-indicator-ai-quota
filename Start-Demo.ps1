param(
    [string]$Port='auto',
    [ValidateSet('codex','volc','openrouter')][string]$Source='codex',
    [string]$PythonPath='',
    [ValidateRange(60,3600)][int]$Interval=300
)
$ErrorActionPreference='Stop'
$privateRoot=Join-Path $PSScriptRoot 'private'
New-Item -ItemType Directory -Force -Path $privateRoot | Out-Null
$pidFile=Join-Path $privateRoot 'bridge.pid'
if (Test-Path -LiteralPath $pidFile) {
    $previousPid=[int](Get-Content -LiteralPath $pidFile -Raw).Trim()
    $existing=Get-CimInstance Win32_Process -Filter "ProcessId=$previousPid" -ErrorAction SilentlyContinue
    $monitorPath=Join-Path $PSScriptRoot 'bridge\monitor.py'
    if ($existing -and $existing.CommandLine -like ('*'+$monitorPath+'*')) {
        Set-Content -LiteralPath (Join-Path $privateRoot 'refresh.request') -Value 'refresh' -Encoding ascii
        Write-Output 'Sync is already running; an online refresh was requested.'
        exit 0
    }
}
if (-not $PythonPath) {
    $localPython=Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $localPython) { $PythonPath=$localPython }
    else {
        $pythonCommand=Get-Command python -ErrorAction SilentlyContinue
        if ($pythonCommand) { $PythonPath=$pythonCommand.Source }
    }
}
if (-not $PythonPath -or -not (Test-Path -LiteralPath $PythonPath)) {
    throw 'Install Python 3.10+ and follow README.md, or pass -PythonPath.'
}
& $PythonPath -c 'import serial'
if ($LASTEXITCODE -ne 0) { throw 'Install dependencies first: python -m pip install -r requirements.txt' }
$stopFile=Join-Path $privateRoot 'stop.request'
if (Test-Path -LiteralPath $stopFile) { Remove-Item -LiteralPath $stopFile }
& $PythonPath (Join-Path $PSScriptRoot 'bridge\launch_monitor.py') --port $Port --source $Source --interval $Interval
if ($LASTEXITCODE -ne 0) { throw 'Could not launch sync.' }
Write-Output ('Sync started; online query interval='+$Interval+'s. Stop with Stop-Demo.ps1.')

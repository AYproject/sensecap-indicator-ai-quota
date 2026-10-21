param([string]$ArduinoCli='', [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if (-not $ArduinoCli) {
    $command=Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($command) { $ArduinoCli=$command.Source }
    else {
        $bundled=Join-Path $env:ProgramFiles 'Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
        if (Test-Path -LiteralPath $bundled) { $ArduinoCli=$bundled }
    }
}
if (-not $ArduinoCli) { throw 'Install Arduino CLI or pass -ArduinoCli with its executable path.' }
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $projectRoot 'prebuilt' }
$buildRoot=Join-Path ([System.IO.Path]::GetTempPath()) 'sensecap-d1-quota-build'
# Map __FILE__ and debug paths as well as source files: assertion strings can
# otherwise embed a builder's personal home directory in distributed firmware.
$sourceMap=@(
    ('"-ffile-prefix-map='+$env:USERPROFILE+'=/builder"'),
    ('"-ffile-prefix-map='+$env:USERPROFILE.Replace('\','/')+'=/builder"'),
    ('"-ffile-prefix-map='+$projectRoot+'=/source"'),
    ('"-ffile-prefix-map='+$projectRoot.Replace('\','/')+'=/source"')
) -join ' '
& $ArduinoCli compile --fqbn 'esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=default,USBMode=hwcdc' --build-property ('compiler.c.extra_flags='+$sourceMap) --build-property ('compiler.cpp.extra_flags='+$sourceMap) --library (Join-Path $projectRoot 'libraries/D1_GFX') --build-path $buildRoot --output-dir $OutputDirectory (Join-Path $projectRoot 'firmware/D1_AI_Quota')
if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }

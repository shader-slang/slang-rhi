param(
    [Parameter(Mandatory = $true)][string]$ProcDump,
    [string]$BuildDirectory = 'build/Release'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$executable = Join-Path (Resolve-Path $BuildDirectory) 'slang-rhi-tests.exe'
$diagnostics = New-Item -ItemType Directory -Force 'build/diagnostics'
$diagnostics = $diagnostics.FullName
$ProcDump = (Resolve-Path $ProcDump).Path

@{
    commit = (git rev-parse HEAD)
    image = $env:ImageOS
    imageVersion = $env:ImageVersion
    architecture = $env:PROCESSOR_ARCHITECTURE
    os = [System.Environment]::OSVersion.VersionString
} | ConvertTo-Json | Set-Content "$diagnostics/environment.json"
Get-Item "$env:windir/System32/d3d12.dll", "$env:windir/System32/d3d10warp.dll" |
    ForEach-Object { $_.VersionInfo | Select-Object FileName, FileVersion, ProductVersion } |
    ConvertTo-Json | Set-Content "$diagnostics/runtime-versions.json"

function Invoke-Workload([string]$Name, [string[]]$TestArguments, [int]$ExpectedVariants = 4) {
    Write-Host "Starting workload: $Name"
    $destination = New-Item -ItemType Directory -Force "$diagnostics/$Name"
    # Capture access violations before doctest's unhandled-exception filter exits.
    & $ProcDump -accepteula -ma -e 1 -f C0000005 -n 1 -k -x $destination.FullName `
        $executable --no-breaks=1 @TestArguments 2>&1 |
        ForEach-Object { "$_" -replace "`0", '' } |
        Tee-Object -FilePath "$destination/process.log"
    $monitorExit = $LASTEXITCODE
    $dumps = @(Get-ChildItem $destination -Filter '*.dmp')
    $completed = Select-String -Path "$destination/process.log" -SimpleMatch '[diagnostic] exit=0'
    $exited = Select-String -Path "$destination/process.log" -Pattern 'Process Exit:.*Exit Code 0x00000000'
    $variants = @(Select-String -Path "$destination/process.log" -Pattern '\[raygen\].*phase=destroyed').Count
    if ($dumps.Count -gt 0 -or !$completed -or !$exited -or $monitorExit -ne 0 -or $variants -ne $ExpectedVariants) {
        throw "Workload $Name failed: monitor exit=$monitorExit, dumps=$($dumps.Count), completed=$([bool]$completed), exited=$([bool]$exited), variants=$variants/$ExpectedVariants"
    }
}

Push-Location (Split-Path $executable)
try {
    Invoke-Workload 'full-suite' @('--check-devices', '--require-devices=d3d11,d3d12')
    Invoke-Workload 'raygen-same-process' @('--test-case=ray-tracing-raygen-entrypoint-resources.d3d12', '--require-devices=d3d12', '--diagnostic-repeat=100') 400
    Invoke-Workload 'raytracing-same-process' @('--test-case=ray-tracing*.d3d12', '--require-devices=d3d12', '--diagnostic-repeat=30') 120
    for ($iteration = 1; $iteration -le 30; ++$iteration) {
        Invoke-Workload "raygen-fresh-process-$iteration" @('--test-case=ray-tracing-raygen-entrypoint-resources.d3d12', '--require-devices=d3d12')
    }
    Invoke-Workload 'full-suite-repeat' @('--check-devices', '--require-devices=d3d11,d3d12', '--diagnostic-repeat=3') 12
}
finally {
    Pop-Location
}

param(
    [string]$ProcDump,
    [string]$BuildDirectory = 'build/Release',
    [int]$RaygenRepeats = 1000,
    [int]$GroupRepeats = 100,
    [int]$FreshProcesses = 30,
    [int]$FullProcesses = 1,
    [string]$DiagnosticDirectory = 'diagnostics',
    [switch]$Direct
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$executable = Join-Path (Resolve-Path $BuildDirectory) 'slang-rhi-tests.exe'
$diagnostics = New-Item -ItemType Directory -Force "build/$DiagnosticDirectory"
$diagnostics = $diagnostics.FullName
if ($Direct) {
    $dumpFolder = (New-Item -ItemType Directory -Force "$diagnostics/dumps").FullName
    $env:RHI_DIAGNOSTIC_DUMP_DIRECTORY = $dumpFolder
}
if (!$Direct) {
    $ProcDump = (Resolve-Path $ProcDump).Path
}

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

function Invoke-Workload([string]$Name, [string[]]$TestArguments, [int]$ExpectedCases = 1) {
    Write-Host "Starting workload: $Name"
    $destination = New-Item -ItemType Directory -Force "$diagnostics/$Name"
    if ($Direct) {
        # First-chance capture leaves execution unattached until an access violation.
        & $executable --no-breaks=1 @TestArguments 2>&1 |
            Tee-Object -FilePath "$destination/process.log"
    }
    else {
        # Capture access violations before doctest's unhandled-exception filter exits.
        & $ProcDump -accepteula -ma -e 1 -f C0000005 -n 1 -x $destination.FullName `
            $executable --no-breaks=1 @TestArguments 2>&1 |
            ForEach-Object { "$_" -replace "`0", '' } |
            Tee-Object -FilePath "$destination/process.log"
    }
    $monitorExit = $LASTEXITCODE
    $dumps = @(Get-ChildItem $diagnostics -Filter '*.dmp' -Recurse)
    $completed = Select-String -Path "$destination/process.log" -SimpleMatch '[diagnostic] exit=0'
    $exited = $Direct -or (Select-String -Path "$destination/process.log" -Pattern 'Process Exit:.*Exit Code 0x00000000')
    $cases = @(Select-String -Path "$destination/process.log" -Pattern '^ray-tracing-raygen-entrypoint-resources[.]d3d12\s+PASSED').Count
    if ($dumps.Count -gt 0 -or !$completed -or !$exited -or $monitorExit -ne 0 -or $cases -ne $ExpectedCases) {
        throw "Workload $Name failed: monitor exit=$monitorExit, dumps=$($dumps.Count), completed=$([bool]$completed), exited=$([bool]$exited), cases=$cases/$ExpectedCases"
    }
}

Push-Location (Split-Path $executable)
try {
    Invoke-Workload 'raygen-same-process' @('--test-case=ray-tracing-raygen-entrypoint-resources.d3d12', '--require-devices=d3d12', "--diagnostic-repeat=$RaygenRepeats") $RaygenRepeats
    Invoke-Workload 'raytracing-same-process' @('--test-case=ray-tracing*.d3d12', '--require-devices=d3d12', "--diagnostic-repeat=$GroupRepeats") $GroupRepeats
    for ($iteration = 1; $iteration -le $FreshProcesses; ++$iteration) {
        Invoke-Workload "raygen-fresh-process-$iteration" @('--test-case=ray-tracing-raygen-entrypoint-resources.d3d12', '--require-devices=d3d12')
    }
    for ($iteration = 1; $iteration -le $FullProcesses; ++$iteration) {
        Invoke-Workload "full-suite-$iteration" @('--check-devices', '--require-devices=d3d11,d3d12')
    }
}
finally {
    Pop-Location
}

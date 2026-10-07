# Manage the resident AVS bridge and its reloadable payload. Never force-eject the bridge.
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('list', 'inject', 'unload')]
    [string] $Action
)

$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$hotswap = Join-Path (Split-Path (Split-Path $repo -Parent) -Parent) 'win32-hotswap-dll\build\hotswap.exe'
$bridge = Join-Path $repo 'build\cmake\avs-bridge.dll'
$payload = Join-Path $repo 'build\cmake\avs-native-mod.dll'
$log = Join-Path $repo 'build\cmake\avs-bridge.log'
$control = Join-Path $repo 'build\cmake\avs-native-mod.unload'
$exe = 'AVS03Pro.exe'

function Assert-Process {
    $processes = @(Get-Process -Name 'AVS03Pro' -ErrorAction SilentlyContinue)
    if ($processes.Count -ne 1) {
        throw "Expected exactly one $exe process; found $($processes.Count)."
    }
    return $processes[0].Id
}

function Get-Modules {
    $lines = @(& $hotswap list $exe)
    if ($LASTEXITCODE -ne 0) { throw "hotswap list failed (exit $LASTEXITCODE)." }
    $modules = @()
    foreach ($line in $lines) {
        if ($line -match '^([0-9a-fA-F]+)\s+(.+)$') {
            $modules += [pscustomobject]@{ Base = $Matches[1]; Path = $Matches[2].Trim() }
        }
    }
    if (-not $modules -or -not @($modules | Where-Object { $_.Path -match '[\\/]AVS03Pro\.exe$' })) {
        throw 'The module snapshot did not contain the game executable.'
    }
    return $modules
}

function Find-ExactModule($modules, [string] $expected) {
    $name = [IO.Path]::GetFileName($expected)
    $sameName = @($modules | Where-Object { [IO.Path]::GetFileName($_.Path) -ieq $name })
    foreach ($module in $sameName) {
        if (-not [string]::Equals($module.Path, $expected, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unexpected $name path in game: $($module.Path)"
        }
    }
    return $sameName
}

function Read-NewLog([string] $path, [long] $offset) {
    # The bridge keeps its append handle open. ReadAllBytes denies its writes.
    $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        if ($stream.Length -le $offset) { return '' }
        [void]$stream.Seek($offset, [IO.SeekOrigin]::Begin)
        $output = [IO.MemoryStream]::new()
        try {
            $stream.CopyTo($output)
            return [Text.Encoding]::UTF8.GetString($output.ToArray())
        } finally { $output.Dispose() }
    } finally { $stream.Dispose() }
}

try {
    if (-not (Test-Path -LiteralPath $hotswap -PathType Leaf)) { throw "Missing hotswap: $hotswap" }
    $pidBefore = Assert-Process
    $modules = @(Get-Modules)
    $bridges = @(Find-ExactModule $modules $bridge)
    $payloads = @(Find-ExactModule $modules $payload)
    if ($bridges.Count -gt 1 -or $payloads.Count -gt 1) { throw 'Duplicate AVS module entries.' }

    if ($Action -eq 'list') {
        Write-Output "PID $pidBefore"
        Write-Output "Bridge: $(if ($bridges.Count) { $bridges[0].Path } else { 'absent' })"
        Write-Output "Payload: $(if ($payloads.Count) { $payloads[0].Path } else { 'absent' })"
        exit 0
    }

    if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) { throw "Missing bridge: $bridge" }
    if ($Action -eq 'inject') {
        if ($bridges.Count) {
            Write-Output "Already loaded in PID ${pidBefore}: $($bridges[0].Path). No injection."
            exit 0
        }
        if ($payloads.Count) { throw 'Payload is present without our bridge; refusing injection.' }
        foreach ($name in @('enable', 'unload', 'stop')) {
            if (Test-Path -LiteralPath (Join-Path $repo "build\cmake\avs-native-mod.$name")) {
                throw "Stale .$name control exists; refusing injection."
            }
        }
        $logBytes = if (Test-Path -LiteralPath $log -PathType Leaf) { (Get-Item -LiteralPath $log).Length } else { 0 }
        & $hotswap inject $exe $bridge
        if ($LASTEXITCODE -ne 0) { throw "hotswap inject failed (exit $LASTEXITCODE)." }
        $deadline = (Get-Date).AddSeconds(5)
        do {
            Start-Sleep -Milliseconds 200
            if ((Assert-Process) -ne $pidBefore) { throw 'Game PID changed after injection.' }
            $modules = @(Get-Modules)
            $bridges = @(Find-ExactModule $modules $bridge)
            $payloads = @(Find-ExactModule $modules $payload)
            if ($bridges.Count -eq 1 -and -not $payloads.Count -and (Test-Path -LiteralPath $log -PathType Leaf)) {
                $fresh = Read-NewLog $log $logBytes
                if ($fresh) {
                    if ($fresh -match 'prepared DISABLED target=[0-9a-fA-F]+; bridge pinned; payload absent; waiting for \.enable') {
                        Write-Output "Verified in PID ${pidBefore}: bridge present, prepared disabled, payload absent."
                        exit 0
                    }
                    if ($fresh -match 'initialization refused') { throw "Bridge initialization refused: $fresh" }
                }
            }
        } while ((Get-Date) -lt $deadline)
        throw 'Bridge readiness not verified; inspect the module list and bridge log. Do not eject the bridge.'
    }

    if (-not $bridges.Count) { throw 'Bridge absent; there is no payload to unload through it.' }
    if (-not $payloads.Count) {
        Write-Output "Payload already absent in PID $pidBefore. No control file created."
        exit 0
    }
    foreach ($name in @('enable', 'unload', 'stop')) {
        if (Test-Path -LiteralPath (Join-Path $repo "build\cmake\avs-native-mod.$name")) {
            throw "Existing .$name control; refusing a conflicting unload request."
        }
    }
    if (-not (Test-Path -LiteralPath $log -PathType Leaf)) { throw "Missing bridge log: $log" }
    $logBytes = (Get-Item -LiteralPath $log).Length
    New-Item -ItemType File -Path $control -ErrorAction Stop | Out-Null
    $deadline = (Get-Date).AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 200
        if ((Assert-Process) -ne $pidBefore) { throw 'Game PID changed while unloading.' }
        $modules = @(Get-Modules)
        $bridges = @(Find-ExactModule $modules $bridge)
        $payloads = @(Find-ExactModule $modules $payload)
        if (-not $bridges.Count) { throw 'Bridge disappeared unexpectedly. Do not assume cleanup succeeded.' }
        $fresh = Read-NewLog $log $logBytes
        if ($fresh -match 'payload stop: success=0' -or $fresh -match 'control/flush error') {
            throw "Payload cleanup failed: $fresh"
        }
        if (-not $payloads.Count -and -not (Test-Path -LiteralPath $control) -and
            $fresh -match 'payload stop: success=1 unmapped=1 reload_allowed=1') {
            Write-Output "Verified in PID ${pidBefore}: payload unmapped; bridge remains resident."
            exit 0
        }
    } while ((Get-Date) -lt $deadline)
    throw 'Payload unload not verified; inspect the control file, module list and bridge log. Do not force-eject.'
} catch {
    Write-Error $_
    exit 1
}

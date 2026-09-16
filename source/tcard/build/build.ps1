[CmdletBinding()]
param(
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64')]
    [string]$Platform = 'x64',
    [switch]$Test,
    [switch]$Deploy
)

$ErrorActionPreference = 'Stop'
$buildDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$tcardRoot = Resolve-Path (Join-Path $buildDir '..')
$solution = Join-Path $tcardRoot 'TCard.sln'
$buildRoot = Join-Path $tcardRoot ("out\x64\$Configuration")
$deployRoot = Resolve-Path (Join-Path $tcardRoot "..\x64\$Configuration") -ErrorAction SilentlyContinue
if (-not $deployRoot) {
    $deployRoot = Join-Path $tcardRoot "..\x64\$Configuration"
}

function Stop-TCardForBuild {
    $allowedPaths = @(
        [IO.Path]::GetFullPath((Join-Path $buildRoot 'plugins\TCard.exe'))
    )
    if ($Deploy) {
        $allowedPaths += [IO.Path]::GetFullPath((Join-Path $deployRoot 'plugins\TCard.exe'))
    }
    $processes = @(Get-Process -Name 'TCard' -ErrorAction SilentlyContinue)
    foreach ($process in $processes) {
        if ($process.Threads.Count -eq 0) {
            Write-Output "Ignoring an exited TCard process entry $($process.Id)"
            continue
        }
        $path = $null
        try { $path = $process.MainModule.FileName } catch { }
        if (-not $path) { throw "Cannot verify TCard executable path for PID $($process.Id)." }
        if ($allowedPaths -notcontains [IO.Path]::GetFullPath($path)) { continue }
        if ($path) {
            Write-Output "Stopping TCard process $($process.Id): $path"
        } else {
            Write-Output "Stopping TCard process $($process.Id)"
        }
        if (-not $process.HasExited -and $process.MainWindowHandle -ne 0) {
            $process.CloseMainWindow() | Out-Null
            $process.WaitForExit(3000) | Out-Null
        }
        if (-not $process.HasExited) {
            Write-Warning 'Forcing TCard shutdown; unsaved edits may be lost.'
            Stop-Process -Id $process.Id -Force -ErrorAction Stop
            if (-not $process.WaitForExit(5000)) { throw "TCard did not exit: $($process.Id)" }
        }
    }
    $remaining = @(Get-Process -Name 'TCard' -ErrorAction SilentlyContinue | Where-Object { $_.Threads.Count -gt 0 -and $allowedPaths -contains $_.Path })
    if ($remaining.Count -gt 0) {
        $ids = ($remaining | ForEach-Object Id) -join ', '
        throw "Unable to stop TCard process(es): $ids. Close TCard or run the build with sufficient rights."
    }
}

Stop-TCardForBuild

$msbuild = $env:MSBUILD
if ([string]::IsNullOrWhiteSpace($msbuild)) {
    $candidates = @(
        (Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'),
        'msbuild'
    )
    $msbuild = $candidates | Where-Object { $_ -eq 'msbuild' -or (Test-Path -LiteralPath $_) } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($msbuild)) { throw 'MSBuild.exe was not found. Set the MSBUILD environment variable.' }

& $msbuild $solution /m /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$languageSource = Join-Path $tcardRoot 'lang'
$languageBuild = Join-Path $buildRoot 'plugins\tcard\lang'
if (Test-Path -LiteralPath $languageSource) {
    New-Item -ItemType Directory -Path $languageBuild -Force | Out-Null
    Copy-Item -Path (Join-Path $languageSource '*') -Destination $languageBuild -Force
}

$renderer = Join-Path $buildRoot 'plugins\tcwui-card.dll'
$coreTests = Join-Path $tcardRoot "out\tests\core\tcard-native-core-tests.exe"
$rendererTests = Join-Path $tcardRoot "out\tests\renderer\tcwui-card-smoke.exe"
$icon = Join-Path $tcardRoot 'icon3.ico'
if (-not (Test-Path -LiteralPath $icon)) { throw "Icon asset not found: $icon" }
$buildIcon = Join-Path $buildRoot 'plugins\icon3.ico'
New-Item -ItemType Directory -Path (Split-Path -Parent $buildIcon) -Force | Out-Null
Copy-Item -LiteralPath $icon -Destination $buildIcon -Force
if ($Test) {
    foreach ($testExecutable in @($coreTests, $rendererTests)) {
        $testStart = @{ FilePath = $testExecutable; PassThru = $true; WindowStyle = 'Hidden' }
        if ($testExecutable -eq $rendererTests) { $testStart.ArgumentList = '"' + $renderer + '"' }
        $testProcess = Start-Process @testStart
        if (-not $testProcess.WaitForExit(60000)) {
            Stop-Process -Id $testProcess.Id -Force
            throw "Test timed out: $testExecutable"
        }
        $testProcess.WaitForExit()
        if ($testProcess.ExitCode -ne 0) { throw "Test failed ($($testProcess.ExitCode)): $testExecutable" }
        Write-Output "PASS: $testExecutable"
    }
}

if ($Deploy) {
    if (-not (Test-Path -LiteralPath $renderer)) { throw "Renderer artifact not found: $renderer" }
    $hostArtifact = Join-Path $buildRoot 'plugins\TCard.exe'
    if (-not (Test-Path -LiteralPath $hostArtifact)) { throw "Host artifact not found: $hostArtifact" }
    # Recheck after build/tests in case TCard was started again before deployment.
    Stop-TCardForBuild
    $pluginDir = Join-Path $deployRoot 'plugins'
    New-Item -ItemType Directory -Path $pluginDir -Force | Out-Null
    New-Item -ItemType Directory -Path $deployRoot -Force | Out-Null
    Copy-Item -LiteralPath $hostArtifact -Destination (Join-Path $pluginDir 'TCard.exe') -Force
    Copy-Item -LiteralPath $icon -Destination (Join-Path $pluginDir 'icon3.ico') -Force
    Copy-Item -LiteralPath $renderer -Destination (Join-Path $pluginDir 'tcwui-card.dll') -Force
    $markdownLicenseSource = Join-Path $buildRoot 'plugins\tcard\licenses\md4c'
    $markdownLicenseTarget = Join-Path $pluginDir 'tcard\licenses\md4c'
    New-Item -ItemType Directory -Path $markdownLicenseTarget -Force | Out-Null
    foreach ($licenseFile in @('LICENSE.md', 'NOTICE.txt')) {
        Copy-Item -LiteralPath (Join-Path $markdownLicenseSource $licenseFile) -Destination (Join-Path $markdownLicenseTarget $licenseFile) -Force
    }
    $ini = Join-Path $tcardRoot 'config\TCard.ini.example'
    $targetIni = Join-Path $pluginDir 'TCard.ini'
    $languageSource = Join-Path $buildRoot 'plugins\tcard\lang'
    $languageTarget = Join-Path $pluginDir 'tcard\lang'
    if (Test-Path -LiteralPath $languageSource) {
        New-Item -ItemType Directory -Path $languageTarget -Force | Out-Null
        Copy-Item -Path (Join-Path $languageSource '*') -Destination $languageTarget -Force
    }
    if ((Test-Path -LiteralPath $ini) -and -not (Test-Path -LiteralPath $targetIni)) {
        Copy-Item -LiteralPath $ini -Destination $targetIni
    }
    Write-Output "TCard native artifacts deployed: $pluginDir"
}

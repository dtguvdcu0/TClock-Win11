param(
    [string]$RuntimeDir = "source/x64/Release"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ini = Join-Path $RuntimeDir "TCycle.ini"
$log = Join-Path $RuntimeDir "tcycle.log"
$baseline = "source/tcyc/TCycle.ini"

if (!(Test-Path $ini)) { throw "Runtime INI not found: $ini" }
if (!(Test-Path $baseline)) { throw "Baseline INI not found: $baseline" }

Add-Type -Name IniApi -Namespace TcycVerify -MemberDefinition @'
[DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
public static extern bool WritePrivateProfileString(string section, string key, string value, string filePath);
'@

function W([string]$s, [string]$k, [string]$v) {
    [TcycVerify.IniApi]::WritePrivateProfileString($s, $k, $v, $ini) | Out-Null
}

function Setup-BaseTask {
    W "TCycle" "Enabled" "1"
    W "TCycle" "PollSec" "1"
    W "TCycle" "LogLevel" "2"
    W "Task.1" "Enabled" "1"
    W "Task.1" "Name" "ArgsMatchSmoke"
    W "Task.1" "TriggerType" "startup"
    W "Task.1" "ActionPath" "C:\Windows\System32\cmd.exe"
    W "Task.1" "ActionArgs" "/c timeout /t 12 >nul"
    W "Task.1" "SingleInstance" "1"
    W "Task.1" "WatchdogEnabled" "0"
    W "Task.1" "WatchdogRequireArgsMatch" "1"
    W "Debug" "ForceCmdlineReadFail" "0"
}

function Run-Case([string]$name, [string]$existingArgs) {
    Write-Host "Running case: $name"
    $pre = Start-Process -FilePath "C:\Windows\System32\cmd.exe" -ArgumentList $existingArgs -PassThru
    $tc = Start-Process -FilePath (Join-Path $RuntimeDir "TCycle.exe") -PassThru
    Start-Sleep -Seconds 3
    Stop-Process -Id $tc.Id -Force -ErrorAction SilentlyContinue
    Stop-Process -Id $pre.Id -Force -ErrorAction SilentlyContinue
}

try {
    if (Test-Path $log) { Remove-Item $log -Force }
    Setup-BaseTask

    Run-Case -name "args_mismatch_should_launch" -existingArgs "/c timeout /t 30 >nul"
    Run-Case -name "args_match_should_skip" -existingArgs "/c timeout /t 12 >nul"

    W "Debug" "ForceCmdlineReadFail" "1"
    Run-Case -name "forced_fallback_should_skip" -existingArgs "/c timeout /t 30 >nul"
    W "Debug" "ForceCmdlineReadFail" "0"

    Write-Host "Recent log lines:"
    Get-Content $log -Tail 80
}
finally {
    Copy-Item $baseline $ini -Force
}

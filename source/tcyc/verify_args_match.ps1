param(
    [string]$RuntimeDir = "source/x64/Release"
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$target = Join-Path $scriptDir "scripts\verify_args_match.ps1"
if (!(Test-Path $target)) {
    throw "Missing script: $target"
}

powershell -NoProfile -ExecutionPolicy Bypass -File $target -RuntimeDir $RuntimeDir
exit $LASTEXITCODE


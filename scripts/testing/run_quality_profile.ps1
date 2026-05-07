[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$profile,
    [string]$config = "scripts/testing/quality_profiles.json",
    [string]$build_dir = "",
    [string]$report_dir = "",
    [switch]$dry_run,
    [switch]$verbose,
    [switch]$fail_fast,
    [string[]]$set
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repo_root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Push-Location $repo_root
try {
    $args = @(
        "scripts/testing/vr_quality_runner.py",
        "--profile", $profile,
        "--config", $config
    )
    if (![string]::IsNullOrWhiteSpace($build_dir)) {
        $args += @("--build-dir", $build_dir)
    }
    if (![string]::IsNullOrWhiteSpace($report_dir)) {
        $args += @("--report-dir", $report_dir)
    }
    if ($dry_run.IsPresent) {
        $args += "--dry-run"
    }
    if ($verbose.IsPresent) {
        $args += "--verbose"
    }
    if ($fail_fast.IsPresent) {
        $args += "--fail-fast"
    }
    if ($set) {
        foreach ($entry in $set) {
            $args += @("--set", $entry)
        }
    }

    python @args
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}

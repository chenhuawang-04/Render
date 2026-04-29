[CmdletBinding()]
param(
    [string]$filter = "EcsGeometryRuntimeSystem",
    [string]$baseline_json = "bench/baselines/ecs_geometry_runtime_gold.json",
    [string]$report_json = "bench/baselines/ecs_geometry_runtime_gate_latest.json",
    [UInt32]$iterations = 512,
    [UInt32]$warmup_runs = 2,
    [UInt32]$measured_runs = 9,
    [double]$fail_on_regression_percent = 8.0,
    [switch]$require_baseline_match
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Get-RepoRoot {
    param(
        [string]$script_path_
    )
    return (Resolve-Path (Join-Path $script_path_ "..\..")).Path
}

function Resolve-BenchRunnerPath {
    param(
        [string]$repo_root_
    )
    $runner_path = Join-Path $repo_root_ "build\bench\vr_bench_runner.exe"
    if (!(Test-Path -LiteralPath $runner_path)) {
        throw "bench runner not found: $runner_path. Build first: cmake --build build -j 8 --target vr_bench_runner"
    }
    return $runner_path
}

function Resolve-PathMaybeRelative {
    param(
        [string]$repo_root_,
        [string]$path_
    )
    if ([System.IO.Path]::IsPathRooted($path_)) {
        return $path_
    }
    return (Join-Path $repo_root_ $path_)
}

function Ensure-ParentDirectory {
    param(
        [string]$file_path_
    )
    $parent_path = Split-Path -Parent $file_path_
    if (![string]::IsNullOrEmpty($parent_path) -and !(Test-Path -LiteralPath $parent_path)) {
        New-Item -ItemType Directory -Path $parent_path | Out-Null
    }
}

function To-RepoRelativePath {
    param(
        [string]$repo_root_,
        [string]$target_path_
    )
    $repo_root_full_path = [System.IO.Path]::GetFullPath($repo_root_)
    $target_full_path = [System.IO.Path]::GetFullPath($target_path_)
    $repo_uri = [System.Uri]($repo_root_full_path + [System.IO.Path]::DirectorySeparatorChar)
    $target_uri = [System.Uri]$target_full_path
    $relative_uri = $repo_uri.MakeRelativeUri($target_uri)
    return [System.Uri]::UnescapeDataString($relative_uri.ToString().Replace('/', '\'))
}

$script_dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo_root = Get-RepoRoot -script_path_ $script_dir
$bench_runner = Resolve-BenchRunnerPath -repo_root_ $repo_root

$baseline_full_path = Resolve-PathMaybeRelative -repo_root_ $repo_root -path_ $baseline_json
$report_full_path = Resolve-PathMaybeRelative -repo_root_ $repo_root -path_ $report_json

if (!(Test-Path -LiteralPath $baseline_full_path)) {
    throw "baseline file does not exist: $baseline_full_path. Run scripts/bench/new_golden_baseline.ps1 first."
}
Ensure-ParentDirectory -file_path_ $report_full_path

Push-Location $repo_root
try {
    $relative_baseline_path = To-RepoRelativePath -repo_root_ $repo_root -target_path_ $baseline_full_path
    $relative_report_path = $report_full_path
    if ([System.IO.Path]::IsPathRooted($report_full_path)) {
        $relative_report_path = To-RepoRelativePath -repo_root_ $repo_root -target_path_ $report_full_path
    }

    Write-Host "[bench-gate] running..." -ForegroundColor Cyan
    Write-Host "  filter       = $filter"
    Write-Host "  baseline     = $relative_baseline_path"
    Write-Host "  report       = $relative_report_path"
    Write-Host "  iterations   = $iterations"
    Write-Host "  warmup/runs  = $warmup_runs/$measured_runs"
    Write-Host "  metric       = mean_ns_per_iteration"
    Write-Host "  fail%        = $fail_on_regression_percent"
    Write-Host "  requireMatch = $($require_baseline_match.IsPresent)"

    $args = @(
        "--filter", $filter,
        "--iterations", "$iterations",
        "--warmup", "$warmup_runs",
        "--runs", "$measured_runs",
        "--baseline-json", $relative_baseline_path,
        "--baseline-metric", "mean_ns_per_iteration",
        "--fail-on-regression-percent", "$fail_on_regression_percent",
        "--report-json", $relative_report_path
    )

    if ($require_baseline_match.IsPresent) {
        $args += "--require-baseline-match"
    }

    & $bench_runner @args
    $exit_code = $LASTEXITCODE

    if ($exit_code -eq 0) {
        Write-Host "[bench-gate] passed." -ForegroundColor Green
        exit 0
    }

    Write-Host "[bench-gate] failed, exit_code=$exit_code" -ForegroundColor Red
    exit $exit_code
} finally {
    Pop-Location
}

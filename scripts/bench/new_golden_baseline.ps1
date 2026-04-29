[CmdletBinding()]
param(
    [string]$filter = "EcsGeometryRuntimeSystem",
    [string]$output_json = "bench/baselines/ecs_geometry_runtime_gold.json",
    [UInt32]$iterations = 512,
    [UInt32]$warmup_runs = 2,
    [UInt32]$measured_runs = 9,
    [switch]$force_overwrite
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
$output_full_path = Resolve-PathMaybeRelative -repo_root_ $repo_root -path_ $output_json

Ensure-ParentDirectory -file_path_ $output_full_path

if ((Test-Path -LiteralPath $output_full_path) -and (-not $force_overwrite.IsPresent)) {
    throw "output baseline already exists: $output_full_path. Use -force_overwrite to replace it."
}

Push-Location $repo_root
try {
    $relative_output_path = $output_full_path
    if ([System.IO.Path]::IsPathRooted($output_full_path)) {
        $relative_output_path = To-RepoRelativePath -repo_root_ $repo_root -target_path_ $output_full_path
    }

    Write-Host "[bench] generating golden baseline..." -ForegroundColor Cyan
    Write-Host "  filter      = $filter"
    Write-Host "  iterations  = $iterations"
    Write-Host "  warmup      = $warmup_runs"
    Write-Host "  runs        = $measured_runs"
    Write-Host "  output_json = $relative_output_path"

    & $bench_runner `
        --filter $filter `
        --iterations $iterations `
        --warmup $warmup_runs `
        --runs $measured_runs `
        --baseline-metric mean_ns_per_iteration `
        --report-json $relative_output_path
    $exit_code = $LASTEXITCODE
    if ($exit_code -ne 0) {
        throw "vr_bench_runner failed, exit_code=$exit_code"
    }

    Write-Host "[bench] golden baseline generated: $relative_output_path" -ForegroundColor Green
} finally {
    Pop-Location
}

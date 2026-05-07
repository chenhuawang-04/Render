#!/usr/bin/env python3
"""
Unified test + benchmark orchestrator for VulkanRender_New.

Goals:
1) Maintainable: profile-driven, no hardcoded command flow in script logic.
2) Configurable: command-line overrides + variable injection.
3) Extensible: each profile is a list of independent steps with exit policy.
"""

from __future__ import annotations

import argparse
import copy
import datetime as _dt
import json
import os
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any


_PLACEHOLDER_PATTERN = re.compile(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}")


class QualityConfigError(RuntimeError):
    """Raised when profile/config parsing fails."""


@dataclass
class StepResult:
    name: str
    command: list[str]
    cwd: str
    start_utc: str
    end_utc: str
    duration_ms: float
    exit_code: int
    passed: bool
    allow_exit_codes: list[int]
    continue_on_failure: bool
    timeout_sec: int | None
    stdout_path: str
    stderr_path: str
    error: str = ""


def _utc_now_iso() -> str:
    return _dt.datetime.utcnow().replace(microsecond=0).isoformat() + "Z"


def _load_json(path_: Path) -> dict[str, Any]:
    if not path_.exists():
        raise QualityConfigError(f"Config file does not exist: {path_}")
    try:
        return json.loads(path_.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise QualityConfigError(f"Invalid JSON in {path_}: {exc}") from exc


def _parse_set_values(entries_: list[str]) -> dict[str, str]:
    values: dict[str, str] = {}
    for entry in entries_:
        if "=" not in entry:
            raise QualityConfigError(f"--set expects KEY=VALUE, got: {entry}")
        key, value = entry.split("=", 1)
        key = key.strip()
        if not key:
            raise QualityConfigError(f"--set key is empty: {entry}")
        values[key] = value
    return values


def _expand_scalar(value_: str, variables_: dict[str, str]) -> str:
    def _replace(match_: re.Match[str]) -> str:
        key = match_.group(1)
        if key not in variables_:
            raise QualityConfigError(f"Unknown variable in placeholder: {key}")
        return variables_[key]

    return _PLACEHOLDER_PATTERN.sub(_replace, value_)


def _expand_value(value_: Any, variables_: dict[str, str]) -> Any:
    if isinstance(value_, str):
        return _expand_scalar(value_, variables_)
    if isinstance(value_, list):
        return [_expand_value(item, variables_) for item in value_]
    if isinstance(value_, dict):
        return {key: _expand_value(item, variables_) for key, item in value_.items()}
    return value_


def _validate_step(step_: dict[str, Any], index_: int) -> None:
    if "name" not in step_:
        raise QualityConfigError(f"Profile step[{index_}] missing field: name")
    if "command" not in step_:
        raise QualityConfigError(f"Profile step[{index_}] missing field: command")
    if not isinstance(step_["name"], str) or not step_["name"].strip():
        raise QualityConfigError(f"Profile step[{index_}] has invalid name")
    if not isinstance(step_["command"], list) or not step_["command"]:
        raise QualityConfigError(f"Profile step[{index_}] command must be non-empty list")
    for token in step_["command"]:
        if not isinstance(token, str):
            raise QualityConfigError(
                f"Profile step[{index_}] command entries must be string, got: {type(token)}"
            )
    if "allow_exit_codes" in step_:
        if not isinstance(step_["allow_exit_codes"], list) or not step_["allow_exit_codes"]:
            raise QualityConfigError(f"Profile step[{index_}] allow_exit_codes must be non-empty list")
        for code in step_["allow_exit_codes"]:
            if not isinstance(code, int):
                raise QualityConfigError(
                    f"Profile step[{index_}] allow_exit_codes entries must be integer"
                )
    if "continue_on_failure" in step_ and not isinstance(step_["continue_on_failure"], bool):
        raise QualityConfigError(f"Profile step[{index_}] continue_on_failure must be bool")
    if "timeout_sec" in step_:
        timeout = step_["timeout_sec"]
        if not isinstance(timeout, int) or timeout <= 0:
            raise QualityConfigError(f"Profile step[{index_}] timeout_sec must be positive integer")
    if "env" in step_:
        env = step_["env"]
        if not isinstance(env, dict):
            raise QualityConfigError(f"Profile step[{index_}] env must be object")
        for key, value in env.items():
            if not isinstance(key, str) or not isinstance(value, str):
                raise QualityConfigError(
                    f"Profile step[{index_}] env must be string->string map"
                )


def _resolve_profile(config_: dict[str, Any], profile_name_: str) -> dict[str, Any]:
    if config_.get("version") != 1:
        raise QualityConfigError("Config version must be 1")
    profiles = config_.get("profiles")
    if not isinstance(profiles, dict):
        raise QualityConfigError("Config field 'profiles' must be object")
    if profile_name_ not in profiles:
        known = ", ".join(sorted(profiles.keys()))
        raise QualityConfigError(f"Profile not found: {profile_name_}. Available: {known}")
    profile = profiles[profile_name_]
    if not isinstance(profile, dict):
        raise QualityConfigError(f"Profile '{profile_name_}' must be object")
    steps = profile.get("steps")
    if not isinstance(steps, list) or not steps:
        raise QualityConfigError(f"Profile '{profile_name_}' must contain non-empty steps[]")
    for index, step in enumerate(steps):
        if not isinstance(step, dict):
            raise QualityConfigError(f"Profile '{profile_name_}' step[{index}] must be object")
        _validate_step(step, index)
    return profile


def _collect_variables(
    config_: dict[str, Any],
    repo_root_: Path,
    args_: argparse.Namespace,
) -> dict[str, str]:
    ts = _dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    variables: dict[str, str] = {}

    config_variables = config_.get("variables", {})
    if config_variables is not None and not isinstance(config_variables, dict):
        raise QualityConfigError("Config field 'variables' must be object if present")
    for key, value in (config_variables or {}).items():
        if not isinstance(key, str) or not isinstance(value, str):
            raise QualityConfigError("Config variables must be string->string map")
        variables[key] = value

    variables.setdefault("repo_root", str(repo_root_))
    variables.setdefault("build_dir", str((repo_root_ / "build").resolve()))
    variables.setdefault("report_dir", str((repo_root_ / "build" / "reports").resolve()))
    variables.setdefault("tests_bin", str((Path(variables["build_dir"]) / "vr_tests.exe").resolve()))
    variables.setdefault("bench_bin", str((Path(variables["build_dir"]) / "vr_bench_runner.exe").resolve()))
    variables.setdefault("timestamp", ts)

    if args_.build_dir:
        build_dir = str(Path(args_.build_dir).resolve())
        variables["build_dir"] = build_dir
        variables["tests_bin"] = str((Path(build_dir) / "vr_tests.exe").resolve())
        variables["bench_bin"] = str((Path(build_dir) / "vr_bench_runner.exe").resolve())
    if args_.tests_bin:
        variables["tests_bin"] = str(Path(args_.tests_bin).resolve())
    if args_.bench_bin:
        variables["bench_bin"] = str(Path(args_.bench_bin).resolve())
    if args_.report_dir:
        variables["report_dir"] = str(Path(args_.report_dir).resolve())

    overrides = _parse_set_values(args_.set_values or [])
    variables.update(overrides)

    # second pass expansion for variables that reference other variables.
    expanded: dict[str, str] = {}
    for key, value in variables.items():
        expanded[key] = _expand_scalar(value, {**variables, **expanded})

    def _normalize_binary_path(primary_path_: str, fallback_paths_: list[str]) -> str:
        primary = Path(primary_path_)
        if primary.exists():
            return str(primary.resolve())
        for fallback in fallback_paths_:
            fallback_path = Path(fallback)
            if fallback_path.exists():
                return str(fallback_path.resolve())
        return str(primary.resolve())

    build_dir_path = Path(expanded["build_dir"])
    expanded["tests_bin"] = _normalize_binary_path(
        expanded["tests_bin"],
        [
            str(build_dir_path / "tests" / "vr_tests.exe"),
            str(build_dir_path / "vr_tests.exe"),
        ],
    )
    expanded["bench_bin"] = _normalize_binary_path(
        expanded["bench_bin"],
        [
            str(build_dir_path / "bench" / "vr_bench_runner.exe"),
            str(build_dir_path / "vr_bench_runner.exe"),
        ],
    )
    return expanded


def _ensure_directory(path_: Path) -> None:
    path_.mkdir(parents=True, exist_ok=True)


def _log_path_for_step(report_dir_: Path, profile_name_: str, step_name_: str, suffix_: str) -> Path:
    safe_step = re.sub(r"[^A-Za-z0-9_.-]+", "_", step_name_.strip())
    safe_profile = re.sub(r"[^A-Za-z0-9_.-]+", "_", profile_name_.strip())
    return report_dir_ / f"{safe_profile}_{safe_step}.{suffix_}.log"


def _run_step(
    step_: dict[str, Any],
    report_dir_: Path,
    profile_name_: str,
    dry_run_: bool,
    verbose_: bool,
) -> StepResult:
    command = [str(token) for token in step_["command"]]
    step_name = str(step_["name"])
    allow_exit_codes = [int(code) for code in step_.get("allow_exit_codes", [0])]
    continue_on_failure = bool(step_.get("continue_on_failure", False))
    timeout_sec = step_.get("timeout_sec")
    if timeout_sec is not None:
        timeout_sec = int(timeout_sec)

    cwd = str(Path(step_.get("cwd", ".")).resolve())
    step_env = os.environ.copy()
    env_override = step_.get("env", {})
    for key, value in env_override.items():
        step_env[str(key)] = str(value)

    stdout_path = _log_path_for_step(report_dir_, profile_name_, step_name, "stdout")
    stderr_path = _log_path_for_step(report_dir_, profile_name_, step_name, "stderr")

    start_utc = _utc_now_iso()
    start_monotonic = time.perf_counter()

    if dry_run_:
        end_utc = _utc_now_iso()
        return StepResult(
            name=step_name,
            command=command,
            cwd=cwd,
            start_utc=start_utc,
            end_utc=end_utc,
            duration_ms=0.0,
            exit_code=0,
            passed=True,
            allow_exit_codes=allow_exit_codes,
            continue_on_failure=continue_on_failure,
            timeout_sec=timeout_sec,
            stdout_path=str(stdout_path),
            stderr_path=str(stderr_path),
            error="",
        )

    with stdout_path.open("w", encoding="utf-8", newline="\n") as stdout_file, \
         stderr_path.open("w", encoding="utf-8", newline="\n") as stderr_file:
        try:
            if verbose_:
                print(f"[RUN ] {step_name}")
                print(f"       cwd: {cwd}")
                print(f"       cmd: {shlex.join(command)}")
            proc = subprocess.run(
                command,
                cwd=cwd,
                env=step_env,
                text=True,
                stdout=stdout_file,
                stderr=stderr_file,
                timeout=timeout_sec,
                check=False,
            )
            exit_code = int(proc.returncode)
            passed = exit_code in allow_exit_codes
            error = ""
        except subprocess.TimeoutExpired as exc:
            exit_code = -1
            passed = False
            error = f"timeout after {exc.timeout} sec"
        except OSError as exc:
            exit_code = -1
            passed = False
            error = f"spawn failed: {exc}"

    duration_ms = (time.perf_counter() - start_monotonic) * 1000.0
    end_utc = _utc_now_iso()
    return StepResult(
        name=step_name,
        command=command,
        cwd=cwd,
        start_utc=start_utc,
        end_utc=end_utc,
        duration_ms=duration_ms,
        exit_code=exit_code,
        passed=passed,
        allow_exit_codes=allow_exit_codes,
        continue_on_failure=continue_on_failure,
        timeout_sec=timeout_sec,
        stdout_path=str(stdout_path),
        stderr_path=str(stderr_path),
        error=error,
    )


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run test + benchmark quality profiles for VulkanRender_New"
    )
    parser.add_argument("--config", default="scripts/testing/quality_profiles.json")
    parser.add_argument("--profile", default="")
    parser.add_argument("--list-profiles", action="store_true")
    parser.add_argument("--build-dir", default="")
    parser.add_argument("--tests-bin", default="")
    parser.add_argument("--bench-bin", default="")
    parser.add_argument("--report-dir", default="")
    parser.add_argument("--set", dest="set_values", action="append")
    parser.add_argument("--summary-json", default="")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser


def _print_profiles(config_: dict[str, Any]) -> int:
    profiles = config_.get("profiles", {})
    if not isinstance(profiles, dict):
        raise QualityConfigError("Config field 'profiles' must be object")
    print("Available profiles:")
    for profile_name in sorted(profiles.keys()):
        profile = profiles[profile_name]
        description = ""
        if isinstance(profile, dict):
            description = str(profile.get("description", ""))
        print(f"  - {profile_name}: {description}")
    return 0


def main(argv_: list[str]) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv_)

    repo_root = Path(__file__).resolve().parents[2]
    config_path = Path(args.config)
    if not config_path.is_absolute():
        config_path = (repo_root / config_path).resolve()
    config = _load_json(config_path)

    if args.list_profiles:
        return _print_profiles(config)

    if not args.profile:
        raise QualityConfigError("--profile is required (or use --list-profiles)")

    profile = _resolve_profile(config, args.profile)
    raw_steps = profile["steps"]

    variables = _collect_variables(config, repo_root, args)
    variables["profile"] = args.profile

    expanded_steps = [copy.deepcopy(_expand_value(step, variables)) for step in raw_steps]
    report_dir = Path(variables["report_dir"])
    _ensure_directory(report_dir)

    print(f"[quality] profile={args.profile}")
    print(f"[quality] config={config_path}")
    print(f"[quality] report_dir={report_dir}")
    if args.dry_run:
        print("[quality] dry-run enabled (no command execution)")

    run_started_utc = _utc_now_iso()
    run_start = time.perf_counter()

    step_results: list[StepResult] = []
    has_failure = False

    for step in expanded_steps:
        result = _run_step(
            step_=step,
            report_dir_=report_dir,
            profile_name_=args.profile,
            dry_run_=args.dry_run,
            verbose_=args.verbose,
        )
        step_results.append(result)
        status = "PASS" if result.passed else "FAIL"
        print(
            f"[{status}] {result.name} exit={result.exit_code} "
            f"duration_ms={result.duration_ms:.2f}"
        )
        if not result.passed:
            has_failure = True
            if result.error:
                print(f"       error: {result.error}")
            print(f"       stdout: {result.stdout_path}")
            print(f"       stderr: {result.stderr_path}")
            fail_fast = args.fail_fast or (not result.continue_on_failure)
            if fail_fast:
                print("[quality] stopping due to failure policy")
                break

    total_duration_ms = (time.perf_counter() - run_start) * 1000.0
    run_finished_utc = _utc_now_iso()

    summary = {
        "version": 1,
        "profile": args.profile,
        "config_path": str(config_path),
        "started_utc": run_started_utc,
        "ended_utc": run_finished_utc,
        "duration_ms": total_duration_ms,
        "variables": variables,
        "step_count": len(expanded_steps),
        "executed_steps": len(step_results),
        "passed_steps": sum(1 for r in step_results if r.passed),
        "failed_steps": sum(1 for r in step_results if not r.passed),
        "dry_run": bool(args.dry_run),
        "results": [asdict(result) for result in step_results],
    }

    summary_json_path = args.summary_json
    if not summary_json_path:
        summary_json_path = str(report_dir / f"quality_{args.profile}_{variables['timestamp']}.json")
    summary_path = Path(summary_json_path)
    if not summary_path.is_absolute():
        summary_path = (repo_root / summary_path).resolve()
    _ensure_directory(summary_path.parent)
    summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"[quality] summary: {summary_path}")

    return 1 if has_failure else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except QualityConfigError as exc:
        print(f"[quality][error] {exc}", file=sys.stderr)
        raise SystemExit(2)

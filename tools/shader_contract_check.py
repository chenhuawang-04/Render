#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

import argparse
import json
import pathlib
import re
from typing import Any


_LAYOUT_PATTERN = re.compile(r"layout\s*\((?P<spec>[^)]*)\)", flags=re.IGNORECASE | re.MULTILINE)
_PUSH_CONSTANT_PATTERN = re.compile(
    r"layout\s*\(\s*push_constant(?:\s*,[^)]*)?\s*\)\s*uniform\b",
    flags=re.IGNORECASE | re.MULTILINE,
)
_STAGE_BY_EXTENSION: dict[str, str] = {
    ".vert": "vert",
    ".frag": "frag",
    ".geom": "geom",
    ".tesc": "tesc",
    ".tese": "tese",
    ".comp": "comp",
}


def _strip_glsl_comments(source_text_: str) -> str:
    without_block = re.sub(r"/\*.*?\*/", "", source_text_, flags=re.DOTALL)
    without_line = re.sub(r"//.*?$", "", without_block, flags=re.MULTILINE)
    return without_line


def _parse_source_descriptors(source_text_: str) -> list[dict[str, int]]:
    descriptors: dict[tuple[int, int], int] = {}
    for match in _LAYOUT_PATTERN.finditer(source_text_):
        spec = match.group("spec")
        set_match = re.search(r"\bset\s*=\s*(\d+)\b", spec)
        binding_match = re.search(r"\bbinding\s*=\s*(\d+)\b", spec)
        if not set_match or not binding_match:
            continue

        set_id = int(set_match.group(1))
        binding_id = int(binding_match.group(1))
        key = (set_id, binding_id)
        descriptors[key] = descriptors.get(key, 0) + 1

    return [
        {"set": set_id, "binding": binding_id, "count": count}
        for (set_id, binding_id), count in sorted(descriptors.items())
    ]


def _parse_source_push_constant_count(source_text_: str) -> int:
    return len(_PUSH_CONSTANT_PATTERN.findall(source_text_))


def _load_reflect_descriptors(reflect_json_: dict[str, Any]) -> list[dict[str, int]]:
    descriptors_src = reflect_json_.get("descriptors", [])
    if not isinstance(descriptors_src, list):
        raise RuntimeError("Reflection JSON does not contain array field: descriptors")

    descriptors: dict[tuple[int, int], int] = {}
    for item in descriptors_src:
        if not isinstance(item, dict):
            continue
        if "set" not in item or "binding" not in item:
            continue
        set_id = int(item["set"])
        binding_id = int(item["binding"])
        key = (set_id, binding_id)
        descriptors[key] = descriptors.get(key, 0) + 1

    return [
        {"set": set_id, "binding": binding_id, "count": count}
        for (set_id, binding_id), count in sorted(descriptors.items())
    ]


def _load_reflect_push_constant_count(reflect_json_: dict[str, Any]) -> int:
    push_constants = reflect_json_.get("push_constants", [])
    if not isinstance(push_constants, list):
        return 0
    return len(push_constants)


def _pair_key(item_: dict[str, int]) -> tuple[int, int]:
    return int(item_["set"]), int(item_["binding"])


def _descriptor_count_map(descriptors_: list[dict[str, int]]) -> dict[tuple[int, int], int]:
    return {_pair_key(item): int(item["count"]) for item in descriptors_}


def _expected_stage_from_source(source_path_: pathlib.Path) -> str | None:
    return _STAGE_BY_EXTENSION.get(source_path_.suffix.lower())


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate shader set/binding contract against reflection JSON.")
    parser.add_argument("--source", required=True, help="GLSL source shader path.")
    parser.add_argument("--reflect", required=True, help="Normalized reflection JSON path.")
    parser.add_argument("--output", required=True, help="Output validation report JSON path.")
    parser.add_argument("--strict", action="store_true", help="Return non-zero if contract check failed.")
    args = parser.parse_args()

    source_path = pathlib.Path(args.source)
    reflect_path = pathlib.Path(args.reflect)
    output_path = pathlib.Path(args.output)

    source_text = _strip_glsl_comments(source_path.read_text(encoding="utf-8"))
    reflect_json = json.loads(reflect_path.read_text(encoding="utf-8"))

    source_descriptors = _parse_source_descriptors(source_text)
    reflect_descriptors = _load_reflect_descriptors(reflect_json)
    source_counts = _descriptor_count_map(source_descriptors)
    reflect_counts = _descriptor_count_map(reflect_descriptors)
    source_pairs = set(source_counts.keys())
    reflect_pairs = set(reflect_counts.keys())

    missing_in_reflect = sorted(source_pairs - reflect_pairs)
    extra_in_reflect = sorted(reflect_pairs - source_pairs)
    count_mismatches: list[dict[str, int]] = []
    for pair in sorted(source_pairs & reflect_pairs):
        source_count = source_counts[pair]
        reflect_count = reflect_counts[pair]
        if source_count != reflect_count:
            count_mismatches.append(
                {
                    "set": pair[0],
                    "binding": pair[1],
                    "source_count": source_count,
                    "reflect_count": reflect_count,
                }
            )

    source_push_constant_count = _parse_source_push_constant_count(source_text)
    reflect_push_constant_count = _load_reflect_push_constant_count(reflect_json)
    push_constant_mismatch = source_push_constant_count != reflect_push_constant_count
    expected_stage = _expected_stage_from_source(source_path)
    reflect_stage = str(reflect_json.get("stage", ""))
    stage_mismatch = expected_stage is not None and expected_stage != reflect_stage

    issues: list[str] = []
    if missing_in_reflect:
        issues.append("missing_bindings_in_reflection")
    if extra_in_reflect:
        issues.append("unexpected_bindings_in_reflection")
    if count_mismatches:
        issues.append("binding_count_mismatch")
    if push_constant_mismatch:
        issues.append("push_constant_count_mismatch")
    if stage_mismatch:
        issues.append("stage_mismatch")

    report = {
        "schema_version": 1,
        "source_shader": str(source_path.as_posix()),
        "reflect_json": str(reflect_path.as_posix()),
        "expected_stage": expected_stage,
        "reflect_stage": reflect_stage,
        "source_descriptors": source_descriptors,
        "reflect_descriptors": reflect_descriptors,
        "missing_in_reflect": [{"set": s, "binding": b} for s, b in missing_in_reflect],
        "extra_in_reflect": [{"set": s, "binding": b} for s, b in extra_in_reflect],
        "binding_count_mismatches": count_mismatches,
        "source_push_constant_count": source_push_constant_count,
        "reflect_push_constant_count": reflect_push_constant_count,
        "passed": len(issues) == 0,
        "issues": issues,
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    if args.strict and not report["passed"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

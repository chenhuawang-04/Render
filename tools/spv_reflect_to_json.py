#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
from typing import Any


_RESOURCE_KIND_MAP: tuple[tuple[str, str], ...] = (
    ("textures", "texture"),
    ("separate_images", "separate_image"),
    ("separate_samplers", "separate_sampler"),
    ("images", "storage_image"),
    ("ssbos", "storage_buffer"),
    ("ubos", "uniform_buffer"),
    ("subpass_inputs", "subpass_input"),
    ("acceleration_structures", "acceleration_structure"),
)


def _resolve_spirv_cross(spirv_cross_path_: str | None) -> str:
    if spirv_cross_path_:
        return spirv_cross_path_

    for candidate in ("spirv-cross", "spirv-cross.exe"):
        resolved = shutil.which(candidate)
        if resolved:
            return resolved

    raise RuntimeError(
        "Failed to locate spirv-cross executable. Provide --spirv-cross or make it available in PATH."
    )


def _normalize_resource(kind_: str, item_: dict[str, Any]) -> dict[str, Any]:
    normalized: dict[str, Any] = {
        "kind": kind_,
        "name": str(item_.get("name", "")),
        "type": str(item_.get("type", "")),
        "set": int(item_["set"]),
        "binding": int(item_["binding"]),
    }

    if "readonly" in item_:
        normalized["readonly"] = bool(item_["readonly"])
    if "block_size" in item_:
        normalized["block_size"] = int(item_["block_size"])

    array_dims = item_.get("array")
    if isinstance(array_dims, list) and array_dims:
        normalized["array"] = [int(value) for value in array_dims]

    return normalized


def _extract_descriptors(reflect_json_: dict[str, Any]) -> list[dict[str, Any]]:
    descriptors: list[dict[str, Any]] = []
    for source_key, normalized_kind in _RESOURCE_KIND_MAP:
        entries = reflect_json_.get(source_key, [])
        if not isinstance(entries, list):
            continue
        for item in entries:
            if not isinstance(item, dict):
                continue
            if "set" not in item or "binding" not in item:
                continue
            descriptors.append(_normalize_resource(normalized_kind, item))

    descriptors.sort(key=lambda item: (item["set"], item["binding"], item["kind"], item["name"]))
    return descriptors


def _extract_push_constants(reflect_json_: dict[str, Any]) -> list[dict[str, Any]]:
    push_constants_src = reflect_json_.get("push_constants", [])
    push_constants: list[dict[str, Any]] = []
    if not isinstance(push_constants_src, list):
        return push_constants

    type_table = reflect_json_.get("types", {})
    if not isinstance(type_table, dict):
        type_table = {}

    for item in push_constants_src:
        if not isinstance(item, dict):
            continue
        type_name = str(item.get("type", ""))
        member_count = 0
        type_info = type_table.get(type_name)
        if isinstance(type_info, dict):
            members = type_info.get("members")
            if isinstance(members, list):
                member_count = len(members)

        push_constants.append(
            {
                "name": str(item.get("name", "")),
                "type": type_name,
                "member_count": member_count,
            }
        )

    return push_constants


def _run_spirv_cross(spirv_cross_path_: str, input_path_: pathlib.Path) -> dict[str, Any]:
    run = subprocess.run(
        [spirv_cross_path_, str(input_path_), "--reflect"],
        capture_output=True,
        text=True,
        check=False,
    )
    if run.returncode != 0:
        raise RuntimeError(
            f"spirv-cross failed ({run.returncode}) for {input_path_}\n"
            f"stdout:\n{run.stdout}\n"
            f"stderr:\n{run.stderr}"
        )

    try:
        return json.loads(run.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Failed to parse spirv-cross JSON reflection for {input_path_}: {exc}") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate normalized JSON reflection from SPIR-V.")
    parser.add_argument("--input", required=True, help="Input SPIR-V file path.")
    parser.add_argument("--output", required=True, help="Output normalized reflection JSON path.")
    parser.add_argument("--spirv-cross", help="Path to spirv-cross executable.")
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    output_path = pathlib.Path(args.output)
    spirv_cross_path = _resolve_spirv_cross(args.spirv_cross)

    input_blob = input_path.read_bytes()
    reflect_json = _run_spirv_cross(spirv_cross_path, input_path)
    descriptors = _extract_descriptors(reflect_json)
    push_constants = _extract_push_constants(reflect_json)
    entry_points = reflect_json.get("entryPoints", [])
    stage = ""
    if isinstance(entry_points, list) and entry_points:
        first_entry = entry_points[0]
        if isinstance(first_entry, dict):
            stage = str(first_entry.get("mode", ""))

    normalized = {
        "schema_version": 1,
        "source_spirv": str(input_path.as_posix()),
        "spirv_sha256": hashlib.sha256(input_blob).hexdigest(),
        "stage": stage,
        "entry_points": entry_points,
        "descriptors": descriptors,
        "push_constants": push_constants,
        "interface_summary": {
            "input_count": len(reflect_json.get("inputs", [])) if isinstance(reflect_json.get("inputs"), list) else 0,
            "output_count": len(reflect_json.get("outputs", []))
            if isinstance(reflect_json.get("outputs"), list)
            else 0,
            "descriptor_count": len(descriptors),
            "push_constant_count": len(push_constants),
        },
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(normalized, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


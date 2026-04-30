#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any


def _load_contract(path_: pathlib.Path) -> dict[str, Any]:
    return json.loads(path_.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description="Aggregate shader contract check JSON results.")
    parser.add_argument("--input", nargs="+", required=True, help="Input contract JSON paths.")
    parser.add_argument("--output", required=True, help="Output summary JSON path.")
    parser.add_argument("--strict", action="store_true", help="Return non-zero if any contract failed.")
    args = parser.parse_args()

    input_paths = [pathlib.Path(path) for path in args.input]
    contracts: list[dict[str, Any]] = []
    failed_contracts: list[dict[str, Any]] = []
    for path in input_paths:
        contract = _load_contract(path)
        contracts.append(contract)
        if not bool(contract.get("passed", False)):
            failed_contracts.append(
                {
                    "contract_file": str(path.as_posix()),
                    "source_shader": str(contract.get("source_shader", "")),
                    "issues": contract.get("issues", []),
                }
            )

    summary = {
        "schema_version": 1,
        "contract_count": len(contracts),
        "passed_count": len(contracts) - len(failed_contracts),
        "failed_count": len(failed_contracts),
        "passed": len(failed_contracts) == 0,
        "failed_contracts": failed_contracts,
    }

    output_path = pathlib.Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    if args.strict and failed_contracts:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


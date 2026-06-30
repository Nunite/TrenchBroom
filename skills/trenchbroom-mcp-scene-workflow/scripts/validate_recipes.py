#!/usr/bin/env python3
"""Validate TrenchBroom MCP skill recipes without calling TrenchBroom or MCP."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
SKILL_DIR = SCRIPT_DIR.parent
RECIPE_DIR = SCRIPT_DIR / "recipes"
EXAMPLES_DIR = SCRIPT_DIR / "examples"

import sys

sys.path.insert(0, str(SCRIPT_DIR / "lib"))

from ir_builder import merge_defaults, validate_ir, validate_params, write_json  # noqa: E402


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    if not isinstance(data, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return data


def canonical_digest(data: dict[str, Any]) -> str:
    encoded = json.dumps(data, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()[:16]


def load_recipe(path: Path) -> tuple[dict[str, Any], Any]:
    spec = importlib.util.spec_from_file_location(path.stem, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load recipe module: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    manifest = getattr(module, "MANIFEST")
    build = getattr(module, "build")
    return manifest, build


def example_paths(recipe_id: str, variants: list[str]) -> list[tuple[str, Path]]:
    result: list[tuple[str, Path]] = []
    for variant in variants:
        path = EXAMPLES_DIR / recipe_id / f"{variant}.json"
        if path.exists():
            result.append((variant, path))
    return result


def validate_example(
    recipe_path: Path,
    variant: str,
    params_path: Path,
    out_dir: Path | None,
) -> dict[str, Any]:
    manifest, build = load_recipe(recipe_path)
    recipe_id = str(manifest["id"])
    params = merge_defaults(manifest, load_json(params_path))
    param_warnings = validate_params(manifest, params)
    first_ir = build(params)
    second_ir = build(params)
    deterministic = canonical_digest(first_ir) == canonical_digest(second_ir)
    validation = validate_ir(first_ir, manifest)
    warnings = param_warnings + list(validation.get("warnings", []))

    output_path = None
    if out_dir:
        output_path = out_dir / recipe_id / f"{variant}.ir.json"
        write_json(str(output_path), first_ir)

    return {
        "recipe": recipe_id,
        "variant": variant,
        "paramsPath": str(params_path),
        "outputPath": str(output_path) if output_path else None,
        "valid": bool(validation["valid"]) and deterministic,
        "deterministic": deterministic,
        "digest": canonical_digest(first_ir),
        "errors": list(validation.get("errors", []))
        + ([] if deterministic else ["recipe output is not deterministic"]),
        "warnings": warnings,
        "summary": validation.get("summary", {}),
    }


def markdown_report(results: list[dict[str, Any]]) -> str:
    valid_count = sum(1 for result in results if result["valid"])
    lines = [
        "# TrenchBroom MCP Recipe Validation",
        "",
        f"Validated {len(results)} example(s); {valid_count} passed.",
        "",
        "| Recipe | Variant | Valid | Ops | Entities | Parts | Warnings |",
        "| --- | --- | --- | ---: | ---: | --- | --- |",
    ]
    for result in results:
        summary = result.get("summary", {})
        parts = ", ".join(summary.get("parts", {}).keys())
        warnings = "; ".join(result.get("warnings", []))
        lines.append(
            "| {recipe} | {variant} | {valid} | {ops} | {entities} | {parts} | {warnings} |".format(
                recipe=result["recipe"],
                variant=result["variant"],
                valid="yes" if result["valid"] else "no",
                ops=summary.get("operationCount", 0),
                entities=summary.get("entityCount", 0),
                parts=parts,
                warnings=warnings,
            )
        )
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description="Validate TrenchBroom MCP skill recipes")
    parser.add_argument("--recipe", action="append", help="Recipe id to validate; defaults to all")
    parser.add_argument(
        "--variant",
        action="append",
        choices=["minimal", "default", "stress"],
        help="Example variant to validate; defaults to all",
    )
    parser.add_argument("--out-dir", help="Optional directory for generated IR files")
    parser.add_argument("--report", help="Optional markdown report path")
    parser.add_argument("--json", action="store_true", help="Print JSON instead of markdown")
    args = parser.parse_args()

    requested_recipes = set(args.recipe or [])
    variants = args.variant or ["minimal", "default", "stress"]
    out_dir = Path(args.out_dir).resolve() if args.out_dir else None
    if out_dir:
        out_dir.mkdir(parents=True, exist_ok=True)

    results: list[dict[str, Any]] = []
    for recipe_path in sorted(RECIPE_DIR.glob("*.py")):
        if recipe_path.name.startswith("_"):
            continue
        manifest, _ = load_recipe(recipe_path)
        recipe_id = str(manifest["id"])
        if requested_recipes and recipe_id not in requested_recipes:
            continue
        paths = example_paths(recipe_id, variants)
        if not paths:
            results.append(
                {
                    "recipe": recipe_id,
                    "variant": ",".join(variants),
                    "paramsPath": None,
                    "outputPath": None,
                    "valid": False,
                    "deterministic": False,
                    "digest": None,
                    "errors": ["no example file found for requested variant(s)"],
                    "warnings": [],
                    "summary": {},
                }
            )
            continue
        for variant, params_path in paths:
            try:
                results.append(validate_example(recipe_path, variant, params_path, out_dir))
            except Exception as e:
                results.append(
                    {
                        "recipe": recipe_id,
                        "variant": variant,
                        "paramsPath": str(params_path),
                        "outputPath": None,
                        "valid": False,
                        "deterministic": False,
                        "digest": None,
                        "errors": [str(e)],
                        "warnings": [],
                        "summary": {},
                    }
                )

    if requested_recipes and not results:
        raise SystemExit(f"unknown recipe id(s): {', '.join(sorted(requested_recipes))}")

    report_text = json.dumps({"valid": all(result["valid"] for result in results), "results": results}, indent=2)
    if not args.json:
        report_text = markdown_report(results)
    if args.report:
        report_path = Path(args.report)
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(report_text, encoding="utf-8", newline="\n")
    print(report_text, end="")
    if not all(result["valid"] for result in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()

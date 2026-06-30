#!/usr/bin/env python3
"""List TrenchBroom MCP skill recipes without generating IR."""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
RECIPE_DIR = SCRIPT_DIR / "recipes"
EXAMPLES_DIR = SCRIPT_DIR / "examples"


def load_manifest(path: Path) -> dict[str, Any]:
    spec = importlib.util.spec_from_file_location(path.stem, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load recipe module: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    manifest = getattr(module, "MANIFEST")
    if not isinstance(manifest, dict):
        raise RuntimeError(f"{path} MANIFEST must be a dict")
    return manifest


def example_variants(recipe_id: str) -> list[str]:
    recipe_examples = EXAMPLES_DIR / recipe_id
    variants = []
    if recipe_examples.exists():
        variants.extend(path.stem for path in sorted(recipe_examples.glob("*.json")))
    legacy = EXAMPLES_DIR / f"{recipe_id}.json"
    if legacy.exists():
        variants.append("legacy")
    return variants


def recipe_summary(manifest: dict[str, Any]) -> dict[str, Any]:
    recipe_id = str(manifest.get("id", ""))
    output = manifest.get("output", {})
    return {
        "id": recipe_id,
        "name": manifest.get("name", recipe_id),
        "version": manifest.get("version", ""),
        "summary": manifest.get("summary", ""),
        "routeLike": bool(output.get("routeLike", False)),
        "moduleIdParam": output.get("moduleIdParam", "moduleId"),
        "routeIdParam": output.get("routeIdParam"),
        "parts": output.get("parts", []),
        "requiredParts": output.get("requiredParts", []),
        "examples": example_variants(recipe_id),
        "expectedWarnings": manifest.get("expectedWarnings", []),
        "recommendedValidation": manifest.get("recommendedValidation", []),
    }


def markdown_catalog(recipes: list[dict[str, Any]]) -> str:
    lines = [
        "# TrenchBroom MCP Recipe Catalog",
        "",
        "| Recipe | Route | Examples | Summary |",
        "| --- | --- | --- | --- |",
    ]
    for recipe in recipes:
        examples = ", ".join(recipe["examples"])
        route = "yes" if recipe["routeLike"] else "no"
        lines.append(
            f"| `{recipe['id']}` | {route} | {examples} | {recipe['summary']} |"
        )
    lines.extend(["", "## Validation"])
    for recipe in recipes:
        lines.extend([f"", f"### `{recipe['id']}`"])
        for step in recipe["recommendedValidation"]:
            lines.append(f"- `{step}`")
        if recipe["expectedWarnings"]:
            lines.append("")
            lines.append("Expected warnings:")
            for warning in recipe["expectedWarnings"]:
                lines.append(f"- {warning}")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description="List available TrenchBroom MCP recipes")
    parser.add_argument("--recipe", action="append", help="Recipe id to include; defaults to all")
    parser.add_argument("--json", action="store_true", help="Print JSON instead of markdown")
    args = parser.parse_args()

    requested = set(args.recipe or [])
    recipes = []
    for path in sorted(RECIPE_DIR.glob("*.py")):
        if path.name.startswith("_"):
            continue
        manifest = load_manifest(path)
        summary = recipe_summary(manifest)
        if requested and summary["id"] not in requested:
            continue
        recipes.append(summary)

    if requested and not recipes:
        raise SystemExit(f"unknown recipe id(s): {', '.join(sorted(requested))}")

    if args.json:
        print(json.dumps({"recipes": recipes}, indent=2))
    else:
        print(markdown_catalog(recipes), end="")


if __name__ == "__main__":
    main()

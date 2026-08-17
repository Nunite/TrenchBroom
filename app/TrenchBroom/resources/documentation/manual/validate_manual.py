#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import Counter
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


CHAPTERS = (
    "00-introduction.md",
    "01-getting-started.md",
    "02-interface-and-tools.md",
    "03-brush-editing.md",
    "04-vertex-and-csg.md",
    "05-materials-and-uv.md",
    "06-assets-and-prefabs.md",
    "07-entities-and-organization.md",
    "08-preferences-and-compilation.md",
    "09-python-plugins-guide.md",
    "10-mcp-automation.md",
    "11-game-config-and-expressions.md",
    "12-references.md",
    "python-api.md",
)
EXPECTED_HTML_LANG = {"en": "en", "zh_CN": "zh-CN"}
HEADING_RE = re.compile(r"^(#{1,6})\s+(.+?)\s+\{#([^}]+)\}\s*$")
ANY_HEADING_RE = re.compile(r"^#{1,6}\s+")
MACRO_RE = re.compile(r"#(?:key|action|menu)\([^\n)]*\)")
SPECIAL_TOKEN_RE = re.compile(r"__TB_[A-Z0-9_]+__|\$\{[^}\n]+\}")
LINK_TARGET_RE = re.compile(r"!?\[[^\]\n]*\]\(([^)\n]+)\)")
REFERENCE_TARGET_RE = re.compile(r"^\[[^\]\n]+\]:\s+(\S+)", re.MULTILINE)
INLINE_CODE_RE = re.compile(r"(?<!`)`([^`\n]+)`(?!`)")
FENCE_RE = re.compile(r"^\s*(`{3,}|~{3,})")
UNRESOLVED_RE = re.compile(r"__TB_[A-Z0-9_]+__|\$[A-Za-z][A-Za-z0-9_-]*\$")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract_code_blocks(text: str) -> tuple[str, ...]:
    lines = text.splitlines(keepends=True)
    blocks: list[str] = []
    index = 0
    while index < len(lines):
        fence = FENCE_RE.match(lines[index])
        if fence:
            marker = fence.group(1)[0]
            length = len(fence.group(1))
            block = [lines[index]]
            index += 1
            while index < len(lines):
                block.append(lines[index])
                if re.match(rf"^\s*{re.escape(marker)}{{{length},}}\s*$", lines[index]):
                    index += 1
                    break
                index += 1
            blocks.append("".join(block))
            continue

        if lines[index].startswith(("    ", "\t")):
            block = [lines[index]]
            index += 1
            while index < len(lines):
                if lines[index].startswith(("    ", "\t")):
                    block.append(lines[index])
                    index += 1
                elif lines[index].strip() == "" and index + 1 < len(lines) and lines[index + 1].startswith(("    ", "\t")):
                    block.append(lines[index])
                    index += 1
                else:
                    break
            blocks.append("".join(block))
            continue
        index += 1
    return tuple(blocks)


def headings(text: str, source: Path, errors: list[str]) -> list[tuple[int, str]]:
    result: list[tuple[int, str]] = []
    seen: set[str] = set()
    in_fence = False
    fence_marker = ""
    for line_number, line in enumerate(text.splitlines(), 1):
        fence = FENCE_RE.match(line)
        if fence:
            marker = fence.group(1)
            if not in_fence:
                in_fence = True
                fence_marker = marker[0]
            elif marker[0] == fence_marker:
                in_fence = False
            continue
        if in_fence or not ANY_HEADING_RE.match(line):
            continue
        match = HEADING_RE.match(line)
        if not match:
            errors.append(f"{source}:{line_number}: heading lacks an explicit anchor")
            continue
        anchor = match.group(3)
        if anchor in seen:
            errors.append(f"{source}:{line_number}: duplicate anchor '{anchor}'")
        seen.add(anchor)
        result.append((len(match.group(1)), anchor))
    return result


def markdown_targets(text: str) -> Counter[str]:
    targets = [match.group(1).strip().split()[0].strip("<>") for match in LINK_TARGET_RE.finditer(text)]
    targets.extend(match.group(1).strip("<>") for match in REFERENCE_TARGET_RE.finditer(text))
    return Counter(targets)


def shared_tokens(text: str) -> Counter[str]:
    values = MACRO_RE.findall(text)
    values.extend(SPECIAL_TOKEN_RE.findall(text))
    values.extend(match.group(0) for match in INLINE_CODE_RE.finditer(text))
    return Counter(values)


def prose_lines(text: str) -> set[str]:
    result: set[str] = set()
    in_fence = False
    fence_marker = ""
    for line in text.splitlines():
        fence = FENCE_RE.match(line)
        if fence:
            marker = fence.group(1)
            if not in_fence:
                in_fence = True
                fence_marker = marker[0]
            elif marker[0] == fence_marker:
                in_fence = False
            continue
        stripped = line.strip()
        if (
            in_fence
            or line.startswith(("    ", "\t"))
            or not stripped
            or REFERENCE_TARGET_RE.match(line)
            or re.fullmatch(r"[-:| ]+", stripped)
        ):
            continue
        if len(re.findall(r"\b[A-Za-z]{2,}\b", stripped)) >= 8:
            result.add(stripped)
    return result


def validate_local_markdown_targets(
    text: str, source: Path, manual_root: Path, anchors: set[str], errors: list[str]
) -> None:
    for target in markdown_targets(text):
        parsed = urlsplit(target)
        if parsed.scheme or target.startswith(("mailto:", "//")):
            continue
        if parsed.path:
            normalized = unquote(parsed.path).replace("\\", "/")
            candidate = (manual_root / normalized.lstrip("./")).resolve()
            if not candidate.exists():
                errors.append(f"{source}: missing local target '{target}'")
        if not parsed.path and parsed.fragment and parsed.fragment not in anchors:
            errors.append(f"{source}: missing anchor target '#{parsed.fragment}'")


class ManualHtmlParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.lang = ""
        self.ids: list[str] = []
        self.targets: list[tuple[str, str]] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        if tag == "html":
            self.lang = values.get("lang") or ""
        if values.get("id"):
            self.ids.append(values["id"] or "")
        for attribute in ("href", "src"):
            if values.get(attribute):
                self.targets.append((attribute, values[attribute] or ""))


def validate_generated_html(generated_root: Path, errors: list[str]) -> None:
    documents: dict[Path, ManualHtmlParser] = {}
    for language, relative in (("en", Path("index.html")), ("zh_CN", Path("zh_CN/index.html"))):
        path = generated_root / relative
        if not path.is_file():
            errors.append(f"missing generated manual '{path}'")
            continue
        text = path.read_text(encoding="utf-8")
        parser = ManualHtmlParser()
        parser.feed(text)
        documents[path.resolve()] = parser
        if parser.lang != EXPECTED_HTML_LANG[language]:
            errors.append(f"{path}: expected html lang '{EXPECTED_HTML_LANG[language]}', got '{parser.lang}'")
        duplicate_ids = [key for key, count in Counter(parser.ids).items() if count > 1]
        if duplicate_ids:
            errors.append(f"{path}: duplicate HTML ids: {', '.join(sorted(duplicate_ids))}")
        unresolved = sorted(set(UNRESOLVED_RE.findall(text)))
        if unresolved:
            errors.append(f"{path}: unresolved placeholders: {', '.join(unresolved)}")

    for path, parser in documents.items():
        for attribute, target in parser.targets:
            parsed = urlsplit(target)
            if parsed.scheme or target.startswith(("mailto:", "//")):
                continue
            target_path = path if not parsed.path else (path.parent / unquote(parsed.path)).resolve()
            if not target_path.exists():
                errors.append(f"{path}: broken {attribute} target '{target}'")
                continue
            if parsed.fragment and target_path.suffix.lower() == ".html":
                target_parser = documents.get(target_path)
                if target_parser and parsed.fragment not in target_parser.ids:
                    errors.append(f"{path}: missing generated anchor '{target}'")


def load_fingerprints(path: Path, errors: list[str]) -> dict[str, str]:
    if not path.is_file():
        errors.append(f"missing translation fingerprint file '{path}'")
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"could not read translation fingerprints: {exc}")
        return {}
    if data.get("schemaVersion") != 1 or data.get("language") != "zh_CN":
        errors.append(f"{path}: unsupported translation fingerprint schema")
        return {}
    chapters = data.get("chapters")
    if not isinstance(chapters, dict):
        errors.append(f"{path}: 'chapters' must be an object")
        return {}
    return {str(key): str(value) for key, value in chapters.items()}


def write_fingerprints(path: Path, manual_root: Path) -> None:
    data = {
        "schemaVersion": 1,
        "language": "zh_CN",
        "sourceLanguage": "en",
        "chapters": {chapter: sha256(manual_root / "en" / chapter) for chapter in CHAPTERS},
    }
    path.write_text(json.dumps(data, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")


def validate_sources(manual_root: Path, fingerprints_path: Path, errors: list[str]) -> None:
    fingerprints = load_fingerprints(fingerprints_path, errors)
    all_anchors: dict[str, set[str]] = {"en": set(), "zh_CN": set()}
    texts: dict[tuple[str, str], str] = {}
    heading_shapes: dict[tuple[str, str], list[tuple[int, str]]] = {}

    for language in ("en", "zh_CN"):
        for chapter in CHAPTERS:
            path = manual_root / language / chapter
            if not path.is_file():
                errors.append(f"missing chapter '{path}'")
                continue
            text = path.read_text(encoding="utf-8")
            texts[(language, chapter)] = text
            shape = headings(text, path, errors)
            heading_shapes[(language, chapter)] = shape
            all_anchors[language].update(anchor for _, anchor in shape)

    for chapter in CHAPTERS:
        en_path = manual_root / "en" / chapter
        en_text = texts.get(("en", chapter))
        zh_text = texts.get(("zh_CN", chapter))
        if en_text is None or zh_text is None:
            continue
        if heading_shapes[("en", chapter)] != heading_shapes[("zh_CN", chapter)]:
            errors.append(f"{chapter}: English and Chinese heading levels or anchors differ")
        if shared_tokens(en_text) != shared_tokens(zh_text):
            errors.append(f"{chapter}: macros, special tokens, or inline code differ")
        if markdown_targets(en_text) != markdown_targets(zh_text):
            errors.append(f"{chapter}: Markdown link or image targets differ")
        if extract_code_blocks(en_text) != extract_code_blocks(zh_text):
            errors.append(f"{chapter}: code blocks differ")
        untranslated = sorted(prose_lines(en_text) & prose_lines(zh_text))
        if untranslated:
            examples = " | ".join(untranslated[:3])
            errors.append(
                f"{chapter}: Chinese contains {len(untranslated)} unchanged English prose lines: {examples}"
            )
        validate_local_markdown_targets(
            en_text, en_path, manual_root, all_anchors["en"], errors
        )
        expected = fingerprints.get(chapter)
        actual = sha256(en_path)
        if expected != actual:
            errors.append(
                f"{chapter}: Chinese translation is stale (English SHA-256 is {actual})"
            )

    unexpected = sorted(set(fingerprints) - set(CHAPTERS))
    if unexpected:
        errors.append(f"unexpected fingerprint chapters: {', '.join(unexpected)}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the bilingual offline manual.")
    parser.add_argument("--manual-root", type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument("--fingerprints", type=Path)
    parser.add_argument("--generated-root", type=Path)
    parser.add_argument("--update-fingerprints", action="store_true")
    args = parser.parse_args()

    manual_root = args.manual_root.resolve()
    fingerprints = (args.fingerprints or manual_root / "translation-status.json").resolve()
    if args.update_fingerprints:
        write_fingerprints(fingerprints, manual_root)

    errors: list[str] = []
    validate_sources(manual_root, fingerprints, errors)
    if args.generated_root:
        validate_generated_html(args.generated_root.resolve(), errors)

    if errors:
        for error in errors:
            print(f"manual validation: {error}", file=sys.stderr)
        return 1
    print(f"Manual validation passed for {len(CHAPTERS)} English/Chinese chapter pairs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

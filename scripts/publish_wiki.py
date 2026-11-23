#!/usr/bin/env python3
"""Validate wiki sources and optionally render them into a wiki checkout."""

import argparse
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs" / "wiki"
LINK = re.compile(r"(?<!!)\[[^\]\n]+\]\(([^\s)]+)\)")
REPO = "https://github.com/mattie02/ai-desk-buddy/blob/main/"


def validate():
    pages = sorted(SOURCE.glob("*.md"))
    errors = []
    for page in [ROOT / "README.md", ROOT / "docs/README.md",
                 ROOT / "backend/README.md", ROOT / "backend/TIMERS.md", *pages]:
        content = page.read_text()
        if sum(line.startswith("```") for line in content.splitlines()) % 2:
            errors.append(f"{page.relative_to(ROOT)}: unclosed code fence")
        for target in LINK.findall(content):
            url = urlsplit(target)
            if url.scheme or url.netloc or not url.path:
                continue
            resolved = (page.parent / unquote(url.path)).resolve()
            if not resolved.exists():
                errors.append(f"{page.relative_to(ROOT)}: missing link {target}")
    if errors:
        raise SystemExit("\n".join(errors))
    print(f"Validated {len(pages)} wiki source files and repository entry points")
    return pages


def render(page):
    def replace(match):
        target = match.group(1)
        url = urlsplit(target)
        if url.scheme or url.netloc or not url.path:
            return match.group(0)
        resolved = (page.parent / unquote(url.path)).resolve()
        if resolved.parent == SOURCE and resolved.suffix == ".md":
            new_target = resolved.stem
        else:
            new_target = REPO + resolved.relative_to(ROOT).as_posix()
        if url.fragment:
            new_target += "#" + url.fragment
        return match.group(0).replace(f"({target})", f"({new_target})")
    return LINK.sub(replace, page.read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="validate source links")
    parser.add_argument("--output", type=Path, help="render into this directory")
    args = parser.parse_args()
    pages = validate()
    if args.output:
        output = args.output.resolve()
        if output == SOURCE or SOURCE in output.parents or output in SOURCE.parents:
            parser.error("output must be outside the canonical source directory")
        output.mkdir(parents=True, exist_ok=True)
        for page in pages:
            (output / page.name).write_text(render(page))
        print(f"Rendered {len(pages)} files into {output}")


if __name__ == "__main__":
    main()

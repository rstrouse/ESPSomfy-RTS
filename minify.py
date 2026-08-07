"""
PlatformIO pre-build script: minify & gzip web assets.

Copies files from  data-src/  →  data/
  • HTML  – whitespace & comment removal, then gzip
  • CSS   – whitespace & comment removal, then gzip
  • JS    – whitespace & comment removal, then gzip
  • PNG   – copied as-is (optipng if available)
  • Other – copied as-is

Usage in platformio.ini
-----------------------
    extra_scripts = pre:minify.py

Directory layout
----------------
    project/
    ├── data-src/          ← original, human-readable assets
    │   ├── index.html
    │   ├── css/
    │   ├── js/
    │   └── img/
    ├── data/              ← auto-generated (gitignore this!)
    ├── minify.py
    └── platformio.ini

The script runs automatically before LittleFS / SPIFFS image is built.
It also runs once at the start of every build so the data/ dir is always fresh.

Dependencies: none (pure Python ≥ 3.7).
Optional:     optipng (for PNG optimisation)
"""

Import("env")  # PlatformIO macro – gives us the build environment

import gzip
import os
import re
import shutil
import subprocess
import sys

# ──────────────────────────────────────────────
# Config
# ──────────────────────────────────────────────
SRC_DIR_NAME = "data-src"
DST_DIR_NAME = "data"

# Extensions that will be minified + gzipped
MINIFY_AND_GZIP = {".html", ".htm", ".css", ".js", ".json", ".svg", ".xml"}

# Extensions that optipng can optimise
PNG_EXTENSIONS = {".png"}

# Everything else is copied verbatim
# ──────────────────────────────────────────────


def _project_dir():
    return env.subst("$PROJECT_DIR")


def _src_dir():
    return os.path.join(_project_dir(), SRC_DIR_NAME)


def _dst_dir():
    return os.path.join(_project_dir(), DST_DIR_NAME)


# ──────────────────────────────────────────────
# Minifiers (pure Python, no npm needed)
# ──────────────────────────────────────────────
def minify_html(text: str) -> str:
    """Simple but effective HTML minifier."""
    # Remove HTML comments (but keep IE conditional comments)
    text = re.sub(r"<!--(?!\[if).*?-->", "", text, flags=re.DOTALL)
    # Collapse whitespace between tags
    text = re.sub(r">\s+<", "> <", text)
    # Collapse runs of whitespace into a single space
    text = re.sub(r"\s{2,}", " ", text)
    # Remove whitespace around = in attributes
    text = re.sub(r'\s*=\s*', '=', text)
    # Strip leading/trailing whitespace per line, rejoin
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return " ".join(lines)


def minify_css(text: str) -> str:
    """Remove comments, collapse whitespace in CSS."""
    # Remove comments
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    # Remove whitespace around : ; { } ,
    text = re.sub(r"\s*([:{};,])\s*", r"\1", text)
    # Collapse remaining whitespace
    text = re.sub(r"\s{2,}", " ", text)
    # Strip leading/trailing
    return text.strip()


# def minify_js(text: str) -> str:
#     """
#     Light JS minifier – removes comments and collapses whitespace.
#     For heavy minification install terser and the script will use it
#     automatically (see _try_terser below).
#     """
#     # Remove single-line comments (careful with URLs – :// )
#     text = re.sub(r"(?<!:)//(?!/)[^\n]*", "", text)
#     # Remove multi-line comments
#     text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
#     # Collapse whitespace
#     text = re.sub(r"\s{2,}", " ", text)
#     lines = [line.strip() for line in text.splitlines() if line.strip()]
#     return "\n".join(lines)


def minify_json(text: str) -> str:
    """Compact JSON by removing unnecessary whitespace."""
    import json
    try:
        data = json.loads(text)
        return json.dumps(data, separators=(",", ":"), ensure_ascii=False)
    except json.JSONDecodeError:
        return text


def minify_svg(text: str) -> str:
    """Minimal SVG minifier – comments + whitespace."""
    text = re.sub(r"<!--.*?-->", "", text, flags=re.DOTALL)
    text = re.sub(r">\s+<", "><", text)
    text = re.sub(r"\s{2,}", " ", text)
    return text.strip()


MINIFIERS = {
    # ".html": minify_html,
    # ".htm":  minify_html,
    # ".css":  minify_css,
    # ".js":   minify_js,
    # ".json": minify_json,
    # ".svg":  minify_svg,
    # ".xml":  minify_svg,  # same approach works for generic XML
}


# ──────────────────────────────────────────────
# Optional external tools (used when available)
# ──────────────────────────────────────────────
def _has_command(cmd: str) -> bool:
    return shutil.which(cmd) is not None


def _try_terser(src_path: str, dst_path: str) -> bool:
    """Use terser for JS if installed (npm i -g terser)."""
    if not _has_command("terser"):
        return False
    try:
        subprocess.run(
            ["terser", src_path, "-o", dst_path, "--compress", "--mangle"],
            check=True, capture_output=True,
        )
        return True
    except subprocess.CalledProcessError:
        return False


def _try_optipng(path: str) -> None:
    """Optimise PNG in-place if optipng is available."""
    if _has_command("optipng"):
        try:
            subprocess.run(
                ["optipng", "-quiet", "-o2", path],
                check=True, capture_output=True,
            )
        except subprocess.CalledProcessError:
            pass


# ──────────────────────────────────────────────
# Core logic
# ──────────────────────────────────────────────
def process_file(src_path: str, dst_path: str) -> dict:
    """
    Process a single file: minify, gzip or copy.
    Returns a small stats dict.
    """
    ext = os.path.splitext(src_path)[1].lower()
    original_size = os.path.getsize(src_path)
    stats = {"src": src_path, "original": original_size, "final": 0, "action": "copy"}

    os.makedirs(os.path.dirname(dst_path), exist_ok=True)

    # ── Text assets: minify + gzip ──────────
    if ext in MINIFY_AND_GZIP:
        # Try terser for JS first
        if ext == ".js" and _try_terser(src_path, dst_path + ".tmp"):
            with open(dst_path + ".tmp", "rb") as f:
                minified = f.read()
            os.remove(dst_path + ".tmp")
            stats["action"] = "terser+gzip"
        else:
            with open(src_path, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()

            minifier = MINIFIERS.get(ext)
            if minifier:
                content = minifier(content)
                stats["action"] = "minify+gzip"
            else:
                stats["action"] = "gzip"

            minified = content.encode("utf-8")

        # Write gzipped version
        gz_path = dst_path + ".gz"
        with gzip.open(gz_path, "wb", compresslevel=9) as gz:
            gz.write(minified)

        stats["final"] = os.path.getsize(gz_path)
        return stats

    # ── PNG: copy + optional optipng ────────
    if ext in PNG_EXTENSIONS:
        shutil.copy2(src_path, dst_path)
        _try_optipng(dst_path)
        stats["final"] = os.path.getsize(dst_path)
        stats["action"] = "optipng" if _has_command("optipng") else "copy"
        return stats

    # ── Everything else: plain copy ─────────
    shutil.copy2(src_path, dst_path)
    stats["final"] = os.path.getsize(dst_path)
    return stats


def minify_all():
    src_dir = _src_dir()
    dst_dir = _dst_dir()

    if not os.path.isdir(src_dir):
        print(f"[minify] WARNING: '{SRC_DIR_NAME}/' not found – skipping.")
        return

    print(f"[minify] {SRC_DIR_NAME}/ → {DST_DIR_NAME}/")

    # Clean destination
    if os.path.exists(dst_dir):
        shutil.rmtree(dst_dir)
    os.makedirs(dst_dir, exist_ok=True)

    total_original = 0
    total_final = 0
    file_count = 0

    for root, dirs, files in os.walk(src_dir):
        for fname in sorted(files):
            # Skip hidden files and editor temp files
            if fname.startswith(".") or fname.endswith("~"):
                continue

            src_path = os.path.join(root, fname)
            rel_path = os.path.relpath(src_path, src_dir)
            dst_path = os.path.join(dst_dir, rel_path)

            stats = process_file(src_path, dst_path)

            pct = (1 - stats["final"] / stats["original"]) * 100 if stats["original"] > 0 else 0
            print(
                f"  {rel_path:<40s} "
                f"{stats['original']:>8,d} → {stats['final']:>8,d} B "
                f"({pct:+.0f}%)  [{stats['action']}]"
            )

            total_original += stats["original"]
            total_final += stats["final"]
            file_count += 1

    print(f"[minify] {file_count} files processed")
    print(
        f"[minify] Total: {total_original:,d} → {total_final:,d} bytes "
        f"(saved {total_original - total_final:,d} bytes, "
        f"{(1 - total_final / total_original) * 100:.0f}%)"
    )


# ──────────────────────────────────────────────
# PlatformIO hooks
# ──────────────────────────────────────────────
# Run at the start of every build
minify_all()
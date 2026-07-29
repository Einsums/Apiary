#!/usr/bin/env python3
# ----------------------------------------------------------------------------------------------
# Copyright (c) The Einsums Developers. All rights reserved.
# Licensed under the MIT License. See LICENSE.txt in the project root for license information.
# ----------------------------------------------------------------------------------------------
"""Content-addressed writes for generated files.

Generation is deterministic: the same headers produce the same page. Writing
that page anyway gives it a fresh mtime, and every build system downstream
reads an mtime, not a hash. So an unconditional write turns "nothing changed"
into "rebuild everything that consumes this" -- a regenerated binding TU
recompiles and relinks, and a regenerated .rst tree makes Sphinx re-read the
whole API reference instead of the handful of pages that actually moved.

Writing only on a content change costs one read of a file we were about to
overwrite, and buys back the incremental build.
"""

from __future__ import annotations

from pathlib import Path

__all__ = ["write_if_changed", "sync_tree"]


def write_if_changed(path: Path | str, text: str, *, encoding: str = "utf-8", newline: str = "\n") -> bool:
    """Write *text* to *path* only when it differs from what is already there.

    Returns True when the file was written. Creates parent directories. The
    comparison is on the DECODED text, so it is unaffected by how the existing
    file happens to be line-ended on disk.
    """
    p = Path(path)
    try:
        if p.read_text(encoding=encoding) == text:
            return False
    except (OSError, UnicodeDecodeError):
        # Missing, unreadable, or not valid text in this encoding: write it.
        pass
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding=encoding, newline=newline)
    return True


def sync_tree(src: Path | str, dst: Path | str, *, suffixes: tuple[str, ...] | None = None) -> tuple[int, int]:
    """Make *dst* match *src*, touching only what actually differs.

    Returns ``(written, removed)``.

    This exists because the two obvious options are each wrong on their own.
    ``rm -rf`` + copy prunes correctly but resets every mtime, so a consumer
    that keys off mtime redoes all its work. ``copy_if_different`` keeps the
    mtimes but leaves orphans behind, so a renamed or deleted entity keeps a
    stale page that still builds and still resolves references to a thing that
    no longer exists.

    Doing both is what makes the result correct AND incremental: files are
    written only when their content changed, and anything in *dst* with no
    counterpart in *src* is deleted.

    *suffixes* restricts which files are considered, in both directions, so a
    caller can own only the file kinds it generates and leave anything else in
    the destination alone.
    """
    src_dir, dst_dir = Path(src), Path(dst)

    def wanted(p: Path) -> bool:
        return suffixes is None or p.suffix in suffixes

    src_files = {p.relative_to(src_dir) for p in src_dir.rglob("*") if p.is_file() and wanted(p)}

    written = 0
    for rel in sorted(src_files):
        # Read as bytes and decode leniently: the tree may legitimately carry a
        # non-text asset, which should still be compared rather than rewritten.
        data = (src_dir / rel).read_bytes()
        target = dst_dir / rel
        try:
            if target.read_bytes() == data:
                continue
        except OSError:
            pass
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        written += 1

    removed = 0
    if dst_dir.is_dir():
        for p in sorted(dst_dir.rglob("*"), reverse=True):
            if p.is_file() and wanted(p) and p.relative_to(dst_dir) not in src_files:
                p.unlink()
                removed += 1
            elif p.is_dir() and not any(p.iterdir()):
                p.rmdir()

    return written, removed


def main(argv: list[str] | None = None) -> int:
    """``apiary_io.py <src> <dst> [--suffix .rst ...]`` - sync a generated tree.

    Exposed as a CLI so a build system can invoke the sync as a normal command
    instead of open-coding ``rm -rf`` + copy.
    """
    import argparse

    ap = argparse.ArgumentParser(description="Sync a generated tree, preserving mtimes of unchanged files.")
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--suffix", action="append", default=None, help="Only consider files with this suffix (repeatable).")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    written, removed = sync_tree(args.src, args.dst, suffixes=tuple(args.suffix) if args.suffix else None)
    if not args.quiet:
        print(f"sync_tree: {written} written, {removed} removed -> {args.dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

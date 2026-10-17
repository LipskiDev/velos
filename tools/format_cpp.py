#!/usr/bin/env python3
"""Check first-party C++ formatting; pass --fix to apply it."""
import argparse
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--fix", action="store_true")
parser.add_argument("--clang-format", default="clang-format")
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
try:
    version = subprocess.check_output([args.clang_format, "--version"], text=True)
    print(version.strip(), flush=True)
    paths = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=root,
    ).decode("utf-8").split("\0")
    excluded = {"external", "build", "bin", "bin-int", "cache", ".git"}
    extensions = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}
    files = sorted({p for p in paths if p and Path(p).suffix.lower() in extensions
                    and not any(part in excluded for part in Path(p).parts)
                    and (root / p).is_file()})
    failed = False
    for path in files:
        options = ["-i"] if args.fix else ["--dry-run", "--Werror"]
        result = subprocess.run(
            [args.clang_format, "--style=file", *options, path], cwd=root)
        failed |= result.returncode != 0
    print(f"{'Formatted' if args.fix else 'Checked'} {len(files)} C/C++ files.")
    sys.exit(1 if failed else 0)
except (OSError, subprocess.CalledProcessError) as error:
    print(f"Formatting tool failed: {error}", file=sys.stderr)
    sys.exit(2)

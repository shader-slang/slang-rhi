#!/usr/bin/env python

from __future__ import annotations

import sys
import unicodedata
from pathlib import Path

REPLACEMENTS = {
    # Invisible / zero-width
    "\ufeff": "",  # BOM (byte order mark)
    "\u00ad": "",  # soft hyphen
    "\u200b": "",  # zero-width space
    "\u200c": "",  # zero-width non-joiner
    "\u200d": "",  # zero-width joiner
    # Spaces
    "\u00a0": " ",  # non-breaking space
    "\u2000": " ",  # en quad
    "\u2001": " ",  # em quad
    "\u2002": " ",  # en space
    "\u2003": " ",  # em space
    "\u2004": " ",  # three-per-em space
    "\u2005": " ",  # four-per-em space
    "\u2006": " ",  # six-per-em space
    "\u2007": " ",  # figure space
    "\u2008": " ",  # punctuation space
    "\u2009": " ",  # thin space
    "\u200a": " ",  # hair space
    "\u202f": " ",  # narrow no-break space
    # Dashes
    "\u2010": "-",  # hyphen
    "\u2011": "-",  # non-breaking hyphen
    "\u2012": "-",  # figure dash
    "\u2013": "-",  # en dash
    "\u2014": "-",  # em dash
    "\u2015": "-",  # horizontal bar
    # Quotes
    "\u2018": "'",  # left single quotation mark
    "\u2019": "'",  # right single quotation mark
    "\u201a": "'",  # single low-9 quotation mark
    "\u201c": '"',  # left double quotation mark
    "\u201d": '"',  # right double quotation mark
    "\u201e": '"',  # double low-9 quotation mark
    "\u2032": "'",  # prime
    "\u2033": '"',  # double prime
    "\u00ab": "<<",  # left guillemet
    "\u00bb": ">>",  # right guillemet
    "\u2039": "<",  # single left angle quotation mark
    "\u203a": ">",  # single right angle quotation mark
    # Symbols
    "\u2026": "...",  # ellipsis
    "\u2022": "*",  # bullet
    "\u00b0": "deg",  # degree sign
    "\u00b7": ".",  # middle dot
    "\u00d7": "x",  # multiplication sign
    "\u00f7": "/",  # division sign
    "\u00ac": "!",  # not sign
    "\u00b1": "+/-",  # plus-minus sign
    "\u00b5": "u",  # micro sign
    "\u03bc": "u",  # greek mu
    "\u00a9": "(c)",  # copyright
    "\u00ae": "(R)",  # registered
    "\u2122": "(TM)",  # trademark
    "\u2713": "Y",  # check mark
    "\u2714": "Y",  # heavy check mark
    "\u2717": "X",  # ballot X
    "\u2718": "X",  # heavy ballot X
    # Math / arrows
    "\u03c0": "pi",  # pi
    "\u00b9": "1",  # superscript one
    "\u221a": "sqrt",  # square root
    "\u221e": "inf",  # infinity
    "\u2248": "~=",  # approximately equal
    "\u2260": "!=",  # not equal
    "\u2264": "<=",  # less-than or equal
    "\u2265": ">=",  # greater-than or equal
    "\u2190": "<-",  # left arrow
    "\u2192": "->",  # right arrow
}


def write_text_preserve_newlines(path: Path, text: str) -> None:
    with path.open("w", encoding="utf-8", newline="") as f:
        f.write(text)


def main() -> int:
    had_error = False
    for raw_path in sys.argv[1:]:

        # Read the file
        path = Path(raw_path)
        try:
            data = path.read_bytes()
        except UnicodeDecodeError as exc:
            print(f"{path}: not valid UTF-8 ({exc})", file=sys.stderr)
            had_error = True
            continue
        except OSError as exc:
            print(f"{path}: failed to read file ({exc})", file=sys.stderr)
            had_error = True
            continue

        # If ASCII-only, no decoding or detailed scan.
        if data.isascii():
            continue

        # Contains unicode, so decode it
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError as exc:
            print(f"{path}: not valid UTF-8 ({exc})", file=sys.stderr)
            had_error = True
            continue

        # Fix each line with explicit replacements first, then Unicode normalization.
        changed = False
        unresolved_lines: list[int] = []
        lines = text.splitlines(keepends=True)
        fixed_lines: list[str] = []
        for line_no, line in enumerate(lines, start=1):
            replaced = line
            for src, dst in REPLACEMENTS.items():
                replaced = replaced.replace(src, dst)
            normalized = unicodedata.normalize("NFKD", replaced)
            fixed_line = normalized.encode("ascii", "ignore").decode("ascii")
            if fixed_line != line:
                changed = True
                print(
                    f"{path}:{line_no}: replaced non-ASCII characters with ASCII equivalents",
                    file=sys.stderr,
                )
            if any(ord(ch) > 127 for ch in normalized):
                unresolved_lines.append(line_no)

            fixed_lines.append(fixed_line)

        # If any lines still contain non-ASCII after fixing, report them as errors.
        if unresolved_lines:
            for line_no in unresolved_lines:
                print(
                    f"{path}:{line_no}: line still contains non-ASCII characters after automatic fixing",
                    file=sys.stderr,
                )
            had_error = True
        elif changed:
            # Only write the fixed file when all characters were resolved.
            write_text_preserve_newlines(path, "".join(fixed_lines))

    return 1 if had_error else 0


if __name__ == "__main__":
    sys.exit(main())

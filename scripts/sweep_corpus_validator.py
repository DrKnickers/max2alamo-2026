"""sweep_corpus_validator.py - Tier 1 stress test against vanilla corpus.

Loads every .alo under tests/corpus/{eaw,foc}/ and runs
_alo.validate(). Reports the distinct failure categories with hit
counts so we can tell:
  - whether our universal-invariant model matches vanilla content
  - which validator rules are too strict (false positives on PG-shipped
    files; ground truth)
  - whether any vanilla file is exceptionally weird

Usage: python scripts/sweep_corpus_validator.py [--detail KEYWORD]
"""
from __future__ import annotations

import argparse
import collections
import glob
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tests', 'maxscript', 'verify'))
import _alo  # noqa: E402


_CAT_PAT = re.compile(
    r"\b\d+\b|"            # any decimal int
    r"'[^']*'|"            # any single-quoted name
    r"\([^)]*\)|"          # any parenthesised aside
    r"\b0x[0-9a-fA-F]+\b"  # any hex literal
)


def categorize(error: str) -> str:
    return _CAT_PAT.sub('*', error)


def sweep(pattern: str):
    counts = collections.Counter()
    samples: dict[str, list[tuple[str, str]]] = collections.defaultdict(list)
    total = passed = 0
    for f in sorted(glob.glob(pattern)):
        total += 1
        try:
            a = _alo.load(f)
            errors = _alo.validate(a, strict=False)
            if not errors:
                passed += 1
            for e in errors:
                cat = categorize(e)
                counts[cat] += 1
                if len(samples[cat]) < 3:
                    samples[cat].append((os.path.basename(f), e))
        except Exception as ex:
            cat = f'EXCEPTION: {type(ex).__name__}'
            counts[cat] += 1
            if len(samples[cat]) < 3:
                samples[cat].append((os.path.basename(f), str(ex)))
    return total, passed, counts, samples


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--detail', default=None,
                    help="substring filter; show every file whose error category matches")
    args = ap.parse_args()

    for label, sub in (('EaW', 'eaw'), ('FoC', 'foc')):
        pat = os.path.join(REPO, 'tests', 'corpus', sub, '*.ALO')
        if not glob.glob(pat):
            continue
        total, passed, counts, samples = sweep(pat)
        print(f'{label} corpus: {passed}/{total} pass validator '
              f'({len(counts)} distinct failure categories)')
        for cat, n in counts.most_common():
            print(f'  {n:5d}  {cat[:120]}')
            for name, msg in samples[cat]:
                print(f'         e.g. {name}: {msg[:100]}')
        print()

    return 0


if __name__ == '__main__':
    sys.exit(main())

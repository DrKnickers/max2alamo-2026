"""survey_bone_visibility.py

Phase 8e/8f planning survey: scan every .alo in tests/corpus and look for
parent-hidden-child-visible (or vice-versa) patterns. The question this
survey answers is: does vanilla EaW/FoC content rely on ANCESTOR
visibility inheritance, or does each bone's `visible` flag mean exactly
itself?

Output classes:
  - "no hidden bones" — model has no bone.visible == false anywhere
  - "all hidden bones are leaves" — hidden bones have no children
  - "hidden bone has visible children" — at least one descendant of a
    hidden bone has visible == true (the key pattern: if the engine
    inherited, these descendants would also be invisible at runtime,
    and the user would have authored them visible only if the engine
    does NOT inherit)
  - "hidden bone has hidden children only" — every descendant of every
    hidden bone is also hidden (consistent with either-or convention)
  - "visible bone with hidden parent chain" — same as "hidden bone has
    visible children" but framed from the descendant's side
"""
import sys
import os
from pathlib import Path

# Run from repo root.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tests" / "maxscript" / "verify"))
import _alo


def descendants(bones, idx):
    """All bone indices that are descendants of `idx` (not including idx itself)."""
    children_of = {}
    for i, b in enumerate(bones):
        children_of.setdefault(b.parent_index, []).append(i)
    out = []
    stack = list(children_of.get(idx, []))
    while stack:
        i = stack.pop()
        out.append(i)
        stack.extend(children_of.get(i, []))
    return out


def classify(alo_path):
    try:
        alo = _alo.load(alo_path)
        bones = alo.bones
    except Exception as e:
        return ("parse_error", str(e), 0, 0, [])

    if not bones:
        return ("no_bones", "", 0, 0, [])

    n_bones = len(bones)
    n_hidden = sum(1 for b in bones if not b.visible)

    if n_hidden == 0:
        return ("no_hidden", "", n_bones, 0, [])

    # For each hidden bone, classify its descendants.
    findings = []
    for i, b in enumerate(bones):
        if b.visible:
            continue
        desc = descendants(bones, i)
        if not desc:
            findings.append((i, b.name, "leaf_hidden", []))
            continue
        visible_desc = [d for d in desc if bones[d].visible]
        if visible_desc:
            findings.append(
                (i, b.name, "hidden_with_visible_descendants",
                 [(d, bones[d].name) for d in visible_desc[:5]])
            )
        else:
            findings.append((i, b.name, "hidden_with_all_hidden_descendants", []))

    any_visible_desc = any(f[2] == "hidden_with_visible_descendants" for f in findings)
    if any_visible_desc:
        return ("hidden_has_visible_children", "", n_bones, n_hidden, findings)
    return ("hidden_chain_only", "", n_bones, n_hidden, findings)


def main(corpus_root):
    counts = {
        "no_bones": 0,
        "no_hidden": 0,
        "hidden_chain_only": 0,
        "hidden_has_visible_children": 0,
        "parse_error": 0,
    }
    examples = {k: [] for k in counts}
    inheritance_evidence = []  # files that prove the local-only design

    for root, _, files in os.walk(corpus_root):
        for name in files:
            if not name.lower().endswith(".alo"):
                continue
            path = os.path.join(root, name)
            klass, err, n_bones, n_hidden, findings = classify(path)
            counts[klass] += 1
            if len(examples[klass]) < 3:
                examples[klass].append((path, n_bones, n_hidden, findings))
            if klass == "hidden_has_visible_children":
                inheritance_evidence.append((path, n_bones, n_hidden, findings))

    print("=== Bone visibility classification across corpus ===")
    total = sum(counts.values())
    for k, v in counts.items():
        pct = 100.0 * v / total if total else 0.0
        print(f"  {k:36s} {v:5d}  ({pct:5.1f}%)")
    print(f"  {'TOTAL':36s} {total:5d}")
    print()

    print("=== Examples by class ===")
    for k, exs in examples.items():
        if not exs:
            continue
        print(f"--- {k} ---")
        for path, nb, nh, findings in exs:
            rel = os.path.relpath(path, corpus_root)
            print(f"  {rel}  bones={nb} hidden={nh}")
            for fi, fn, ftype, vd in findings[:3]:
                print(f"      bone[{fi}] {fn!r:50s} {ftype}")
                for di, dn in vd[:3]:
                    print(f"          visible descendant: bone[{di}] {dn!r}")
        print()

    if inheritance_evidence:
        print("=== Files proving LOCAL-ONLY semantics ===")
        print(f"  {len(inheritance_evidence)} files have a hidden bone with at least one VISIBLE descendant.")
        print("  If the engine inherited ancestor visibility, those descendants would be invisible at")
        print("  runtime, and authors wouldn't have set bone.visible = true on them. So the engine must")
        print("  NOT inherit (or these files would be broken in-game).")
        print()
        print("  Sample (first 5):")
        for path, nb, nh, _ in inheritance_evidence[:5]:
            print(f"    {os.path.relpath(path, corpus_root)}  bones={nb} hidden={nh}")
    else:
        print("=== No inheritance evidence ===")
        print("  Every hidden bone has either no descendants or only hidden descendants.")
        print("  Consistent with EITHER local-only OR inherit-from-parent semantics.")


if __name__ == "__main__":
    corpus = sys.argv[1] if len(sys.argv) > 1 else "tests/corpus"
    main(corpus)

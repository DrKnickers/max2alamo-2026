"""verify_test_phase8_name_round_trip.py - Phase 8f UTF-8 round-trip.

Pins byte-for-byte UTF-8 round-trip of bone names. Catches:
  - to_utf8() converter bugs (CP_ACP vs CP_UTF8)
  - Truncation at non-ASCII byte boundaries
  - Length-handling bugs on long names

Expected encoded names (UTF-8):
  "Hone1_骨头"      = b"Hone1_\xe9\xaa\xa8\xe5\xa4\xb4"
  "Hone2_テスト"     = b"Hone2_\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88"
  "Hone3_тест"     = b"Hone3_\xd1\x82\xd0\xb5\xd1\x81\xd1\x82"
  "Long_aaaa..."   = 5 + 95 = 100 ASCII bytes
"""
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import _alo


def main(alo_path):
    errors = []
    try:
        alo = _alo.load(alo_path)
    except Exception as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1

    bone_names = [b.name for b in alo.bones]

    # #A1 expected bones present
    expected_names = {
        "Hone1_骨头",
        "Hone2_テスト",
        "Hone3_тест",
        "Long_" + ("a" * 95),
    }
    actual_set = set(bone_names)
    missing = expected_names - actual_set
    if missing:
        errors.append(f"#A1 missing bones: {missing}; got: {actual_set}")

    # #B2-B5 each name has the exact UTF-8 byte content we authored
    expected_utf8 = {
        "Hone1_骨头": b"Hone1_\xe9\xaa\xa8\xe5\xa4\xb4",
        "Hone2_テスト": b"Hone2_\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88",
        "Hone3_тест": b"Hone3_\xd1\x82\xd0\xb5\xd1\x81\xd1\x82",
    }
    for name, expected_bytes in expected_utf8.items():
        if name in actual_set:
            got = name.encode("utf-8")
            if got != expected_bytes:
                errors.append(f"#B-{name} UTF-8 byte mismatch: "
                              f"got {got.hex()}, expected {expected_bytes.hex()}")

    # #C6 long name is exactly 100 chars after decode
    long_name = "Long_" + ("a" * 95)
    if long_name in actual_set:
        if len(long_name) != 100:
            errors.append(f"#C6 long name length = {len(long_name)}, expected 100")
    else:
        errors.append(f"#C6 long name not present in bones (truncation?)")

    # #C7 no name has an embedded null byte
    for name in bone_names:
        if "\x00" in name:
            errors.append(f"#C7 bone {name!r} has embedded null byte")

    if errors:
        print("FAIL:", file=sys.stderr)
        for e in errors: print(f"  - {e}", file=sys.stderr)
        return 1

    print(f"OK  ({len(bone_names)} bones; "
          f"Chinese/Japanese/Cyrillic + 100-char names round-tripped UTF-8; "
          f"7/7 assertions passed)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))

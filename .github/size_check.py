"""Fail the build when the firmware leaves too little room in its app slot.

PlatformIO only fails once the firmware no longer fits.  Releases are built at
CORE_DEBUG_LEVEL=2 because the ESP32 image barely fits its 1.5 MB OTA slot, so
this warns while there is still room to act.

Usage: python .github/size_check.py <build dir> <name> [min free KB]
Writes a line to $GITHUB_STEP_SUMMARY when it is set.
"""
import os
import struct
import sys

ENTRY = struct.Struct("<HBBII16sI")  # magic, type, subtype, offset, size, label, flags
ENTRY_MAGIC = 0x50AA
APP_TYPE = 0x00


def app_slot_size(partitions_bin):
    """The largest app partition in a built partition table."""
    with open(partitions_bin, "rb") as f:
        table = f.read()
    largest = 0
    for pos in range(0, len(table) - ENTRY.size + 1, ENTRY.size):
        magic, ptype, _, _, size, _, _ = ENTRY.unpack_from(table, pos)
        if magic != ENTRY_MAGIC:
            break
        if ptype == APP_TYPE:
            largest = max(largest, size)
    return largest


def main():
    build_dir, name = sys.argv[1], sys.argv[2]
    min_free = int(sys.argv[3]) * 1024 if len(sys.argv) > 3 else 8 * 1024
    used = os.path.getsize(os.path.join(build_dir, "firmware.bin"))
    slot = app_slot_size(os.path.join(build_dir, "partitions.bin"))
    free = slot - used
    ok = free >= min_free
    line = (f"| {name} | {used:,} | {slot:,} | {free / 1024:.1f} KB | "
            f"{'OK' if ok else f'FAIL: under {min_free // 1024} KB free'} |")
    print(line)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("| Env | Firmware | App slot | Free | |\n|---|---|---|---|---|\n")
            f.write(line + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

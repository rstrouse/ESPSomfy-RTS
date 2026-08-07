"""
PlatformIO post-build script: archive firmware.elf files.

Copies firmware.elf to elf_archive/ with a timestamp after each build.
Keeps only the last 10 files to avoid filling up disk space.

Usage in platformio.ini
-----------------------
    extra_scripts = post:archive_elf.py
"""

Import("env")

import os
import shutil
from datetime import datetime

MAX_ARCHIVES = 10
ARCHIVE_DIR = os.path.join(env.subst("$PROJECT_DIR"), "elf_archive")


def archive_elf(source, target, env):
    elf_path = os.path.join(env.subst("$BUILD_DIR"), "firmware.elf")
    if not os.path.isfile(elf_path):
        print("[archive_elf] firmware.elf not found, skipping.")
        return

    os.makedirs(ARCHIVE_DIR, exist_ok=True)

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    dest = os.path.join(ARCHIVE_DIR, f"firmware_{timestamp}.elf")
    shutil.copy2(elf_path, dest)
    print(f"[archive_elf] Saved {dest}")

    # Keep only the last MAX_ARCHIVES files
    files = sorted(
        [f for f in os.listdir(ARCHIVE_DIR) if f.endswith(".elf")],
    )
    while len(files) > MAX_ARCHIVES:
        old = os.path.join(ARCHIVE_DIR, files.pop(0))
        os.remove(old)
        print(f"[archive_elf] Removed old archive {old}")


env.AddPostAction("$BUILD_DIR/firmware.elf", archive_elf)

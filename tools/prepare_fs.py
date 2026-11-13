"""
PlatformIO pre-script: stages the files listed in `custom_fs_files` from env/ into
.pio/fsdata/<env>/ and points `uploadfs` at that folder.

  - JSON files are validated, so a typo fails here and not silently on the bus.
  - Each target names its own secrets file, renamed to secrets.env on the device
    (node_a/.env:secrets.env, node_b/.env:secrets.env).
  - A missing secrets file only warns: the firmware then boots without Wi-Fi credentials.
"""
import json
import os
import shutil

Import("env")  # noqa: F821  (injected by PlatformIO)


def stage():
    spec = env.GetProjectOption("custom_fs_files", "").split()  # noqa: F821
    if not spec:
        return

    project = env.subst("$PROJECT_DIR")  # noqa: F821
    src_dir = os.path.join(project, "env")
    dst_dir = os.path.join(project, ".pio", "fsdata", env["PIOENV"])  # noqa: F821
    shutil.rmtree(dst_dir, ignore_errors=True)
    os.makedirs(dst_dir)

    for item in spec:
        src_name, _, dst_name = item.partition(":")
        dst_name = dst_name or src_name
        src = os.path.join(src_dir, src_name)
        if not os.path.isfile(src):
            hint = f" (copy env/.env.example to env/{src_name})" if os.path.basename(src_name) == ".env" else ""
            print(f"[prepare_fs] WARNING: env/{src_name} not found, skipped{hint}")
            continue
        if src_name.endswith(".json"):
            with open(src, encoding="utf-8") as f:
                try:
                    json.load(f)
                except json.JSONDecodeError as e:
                    print(f"[prepare_fs] ERROR: env/{src_name} is not valid JSON: {e}")
                    env.Exit(1)  # noqa: F821
        shutil.copyfile(src, os.path.join(dst_dir, dst_name))

    env.Replace(PROJECT_DATA_DIR=dst_dir)  # noqa: F821


stage()

#!/usr/bin/env python3
"""Generate vk-default.txt (the CTS caselist) from a local VK-GL-CTS checkout.

The generated file is gitignored (the full mustpass is ~380 MB, beyond GitHub's
100 MB file limit) and regenerated on demand:

    python scripts/generate_caselist.py [--profile vk10|full] [--cts-repo PATH] [--tag TAG] [--output PATH]

The tag must match the deqp-vk binary version pulled by xmake (see xmake/cts.lua),
otherwise deqp silently drops every caselist entry whose name does not exist in
its test tree (renamed groups like pipeline.* -> pipeline.monolithic.*). For the
same reason the historical 1.0.x mustpass files cannot be used with a modern
binary: their test names predate those renames. The vk10 profile is the modern
equivalent - the 1.4.4.1 names, restricted to the group files a Vulkan 1.0
driver can exercise (extension/1.1+ groups would all report NotSupported),
minus the individual cases that call a core 1.1 entry point without guarding
on the API version (see Vk10ExcludedCasePrefixes).
"""

import argparse
import os
import subprocess
import sys
import zipfile
from pathlib import Path

DefaultTag = "vulkan-cts-1.4.4.1"
MustpassRoot = "external/vulkancts/mustpass/main"


def default_cts_repo() -> str:
    env = os.environ.get("VK_GL_CTS_REPO")
    if env:
        return env
    return str(Path(__file__).resolve().parents[2] / "VK-GL-CTS")

# Mustpass group files (paths relative to vk-default/) that only test extensions or
# core versions beyond 1.0 - skipped by the vk10 profile.
Vk10ExcludedPrefixes = (
	"conditional-rendering",
	"cooperative-vector",
	"descriptor-indexing",
	"device-group",
	"dgc",
	"drm-format-modifiers",
	"fragment-shader-interlock",
	"fragment-shading-barycentric",
	"fragment-shading-rate",
	"image/host-image-copy",
	"image/swapchain-mutable",
	"imageless-framebuffer",
	"memory-model",
	"mesh-shader",
	"multiview",
	"pipeline/fast-linked-library",
	"pipeline/pipeline-library",
	"pipeline/shader-object",
	"protected-memory",
	"ray-query",
	"ray-tracing-pipeline",
	"reconvergence",
	"robustness",
	"shader-object/",
	"sparse-resources",
	"subgroups",
	"synchronization2",
	"transform-feedback",
	"video",
	"wsi",
	"ycbcr",
)

# Individual cases living in a group file the vk10 profile keeps, but whose checkSupport
# calls a core 1.1 entry point unconditionally. On a 1.0 instance the loader returns a null
# pointer for those names and deqp jumps to address 0 instead of reporting NotSupported,
# taking the whole run down. Drop them until the driver advertises 1.1.
Vk10ExcludedCasePrefixes = (
	# multiplane::checkFormatSupport -> vkGetPhysicalDeviceImageFormatProperties2
	"dEQP-VK.api.copy_and_blit.multiplanar_xfer.",
)


def git_show(repo: str, tag: str, path: str) -> str:
    result = subprocess.run(
        ["git", "-C", repo, "show", f"{tag}:{path}"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        sys.exit(f"git show {tag}:{path} failed: {result.stderr.strip()}")
    return result.stdout


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=["vk10", "full"], default="vk10", help="vk10: only the group files a Vulkan 1.0 driver can exercise (default). full: the whole mustpass.")
    parser.add_argument("--cts-repo", default=default_cts_repo(), help="Path to the VK-GL-CTS git checkout (default: $VK_GL_CTS_REPO, else a VK-GL-CTS checkout next to this repo)")
    parser.add_argument("--tag", default=DefaultTag, help="CTS release tag matching the deqp-vk binary")
    parser.add_argument("--output", default=str(Path(__file__).resolve().parent.parent / "vk-default.txt"))
    args = parser.parse_args()

    if not Path(args.cts_repo, ".git").exists():
        sys.exit(f"VK-GL-CTS checkout not found at {args.cts_repo} - pass --cts-repo or set VK_GL_CTS_REPO")

    index = git_show(args.cts_repo, args.tag, f"{MustpassRoot}/vk-default.txt")

    total = 0
    skipped = 0
    skippedCases = 0
    with open(args.output, "w", newline="\n") as out:
        for entry in index.splitlines():
            entry = entry.strip()
            if not entry:
                continue
            group = entry.removeprefix("vk-default/")
            if args.profile == "vk10" and group.startswith(Vk10ExcludedPrefixes):
                skipped += 1
                print(f"{entry}: skipped ({args.profile} profile)")
                continue
            content = git_show(args.cts_repo, args.tag, f"{MustpassRoot}/{entry}")
            cases = [case for case in content.splitlines() if case.strip()]
            dropped = 0
            if args.profile == "vk10":
                kept = [case for case in cases if not case.startswith(Vk10ExcludedCasePrefixes)]
                dropped = len(cases) - len(kept)
                skippedCases += dropped
                cases = kept
            out.write("".join(case + "\n" for case in cases))
            total += len(cases)
            print(f"{entry}: done" + (f" ({dropped} cases dropped)" if dropped else ""))

    # The committed artifact: the raw file is gitignored (over GitHub's size limit), the
    # vk-cts-* xmake target extracts this zip when vk-default.txt is absent (fresh checkout, CI).
    zip_path = args.output + ".zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.write(args.output, Path(args.output).name)

    print(f"\n{total} tests -> {args.output} ({skipped} group files skipped, {skippedCases} cases dropped)")
    print(f"compressed -> {zip_path} ({os.path.getsize(zip_path)} bytes)")


if __name__ == "__main__":
    main()

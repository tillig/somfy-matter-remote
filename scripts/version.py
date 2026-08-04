"""Inject the firmware version into the build as FIRMWARE_VERSION.

The version comes from git rather than from a constant in the source, so there
is one source of truth (the release tag) and no chance of the two drifting. A
tagged commit yields exactly the tag, for example "v1.0.0"; anything else gets
a descriptive form such as "v1.0.0-3-gabc1234" or "v1.0.0-3-gabc1234-dirty" so
a hand-built image is never mistaken for a release.

Falls back to "unknown" when git is unavailable or the tags were not fetched,
which is honest rather than misleading: the dashboard reports it verbatim and
its update check declines to compare when the version is not a release.
"""

import subprocess

Import("env")


def firmware_version():
    try:
        described = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return "unknown"

    version = described.stdout.strip()
    if described.returncode != 0 or not version:
        # No tags reachable (a shallow clone, for example) or not a repository.
        return "unknown"
    return version


version = firmware_version()
print("Firmware version: %s" % version)
env.Append(CPPDEFINES=[("FIRMWARE_VERSION", env.StringifyMacro(version))])

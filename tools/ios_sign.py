#!/usr/bin/env python3
"""Obtain a device development profile and sign a prepared TOTK iOS app.

The helper project requests only the increased-memory capability and, when
specified, one caller-owned CloudKit container in the Development environment.
The supplied app is not changed until the resulting profile and a matching
local signing identity have passed every validation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import plistlib
import re
import shutil
import subprocess
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Callable, Mapping, Sequence


MEMORY_ENTITLEMENT = "com.apple.developer.kernel.increased-memory-limit"
CLOUD_CONTAINERS = "com.apple.developer.icloud-container-identifiers"
CLOUD_SERVICES = "com.apple.developer.icloud-services"
CLOUD_ENVIRONMENT = "com.apple.developer.icloud-container-environment"
_BUNDLE_RE = re.compile(r"[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+\Z")
_TEAM_RE = re.compile(r"[A-Z0-9]{10}\Z")
_DEVICE_RE = re.compile(r"[A-Za-z0-9-]+\Z")
_IDENTITY_RE = re.compile(r"\b([0-9A-Fa-f]{40})\b")


class SigningError(RuntimeError):
    """A profile, identity, bundle, command, or verification was rejected."""


@dataclass(frozen=True)
class SigningResult:
    app: Path
    bundle_id: str
    profile_uuid: str
    expires: datetime
    cloud_container: str | None


Runner = Callable[[Sequence[str], Path | None], subprocess.CompletedProcess]


def _default_runner(command: Sequence[str], cwd: Path | None) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(item) for item in command], cwd=cwd, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, check=False,
    )


def _output_bytes(result: subprocess.CompletedProcess) -> bytes:
    output = result.stdout or b""
    return output.encode() if isinstance(output, str) else output


def _checked(runner: Runner, command: Sequence[str], cwd: Path | None = None) -> bytes:
    result = runner(tuple(str(item) for item in command), cwd)
    output = _output_bytes(result)
    if result.returncode:
        tail = output.decode(errors="replace").splitlines()[-20:]
        executable = Path(str(command[0])).name
        raise SigningError(f"{executable} failed" + (":\n" + "\n".join(tail) if tail else ""))
    return output


def _tool(name: str, finder: Callable[[str], str | None]) -> str:
    path = finder(name)
    if not path:
        hint = " (install with: brew install xcodegen)" if name == "xcodegen" else ""
        raise SigningError(f"required tool is unavailable: {name}{hint}")
    return path


def _validate_bundle_id(bundle_id: object) -> str:
    if not isinstance(bundle_id, str) or not _BUNDLE_RE.fullmatch(bundle_id):
        raise SigningError("the app has an invalid CFBundleIdentifier")
    return bundle_id


def _validate_team(team: str) -> str:
    if not _TEAM_RE.fullmatch(team):
        raise SigningError("team must be a 10-character Apple Developer Team ID")
    return team


def _validate_device(device: str) -> str:
    if not _DEVICE_RE.fullmatch(device):
        raise SigningError("device must be a device identifier, not a name or destination expression")
    return device


def _validate_cloud_container(container: str | None) -> str | None:
    if container in (None, ""):
        return None
    if not isinstance(container, str) or not container.startswith("iCloud."):
        raise SigningError("CloudKit container must begin with iCloud.")
    suffix = container.removeprefix("iCloud.")
    if not _BUNDLE_RE.fullmatch(suffix) or ".." in container:
        raise SigningError("CloudKit container has an invalid identifier")
    return container


def requested_capabilities(cloud_container: str | None) -> dict[str, object]:
    """Return the exact capabilities requested from automatic provisioning."""
    result: dict[str, object] = {MEMORY_ENTITLEMENT: True}
    if cloud_container:
        result.update({
            CLOUD_CONTAINERS: [cloud_container],
            CLOUD_SERVICES: ["CloudKit"],
            CLOUD_ENVIRONMENT: "Development",
        })
    return result


def _profile_allows(allowed: object, requested: object) -> bool:
    if isinstance(requested, list):
        return allowed == "*" or (isinstance(allowed, list) and all(item in allowed for item in requested))
    if isinstance(allowed, list):
        return requested in allowed
    return allowed == requested


def _utc(value: object, label: str) -> datetime:
    if not isinstance(value, datetime):
        raise SigningError(f"profile has no valid {label}")
    return value.replace(tzinfo=timezone.utc) if value.tzinfo is None else value.astimezone(timezone.utc)


def validate_profile(
    profile: Mapping[str, object], bundle_id: str, team: str, device: str,
    cloud_container: str | None, *, now: datetime | None = None,
) -> dict[str, object]:
    """Validate a development profile and return the exact app entitlements."""
    expires = _utc(profile.get("ExpirationDate"), "expiration date")
    current = now or datetime.now(timezone.utc)
    if current.tzinfo is None:
        current = current.replace(tzinfo=timezone.utc)
    if expires <= current.astimezone(timezone.utc):
        raise SigningError("provisioning profile is expired")

    teams = profile.get("TeamIdentifier")
    if not isinstance(teams, list) or team not in teams:
        raise SigningError("provisioning profile belongs to a different team")
    devices = profile.get("ProvisionedDevices")
    if not isinstance(devices, list) or device not in devices:
        raise SigningError("provisioning profile does not include the requested device")
    platforms = profile.get("Platform")
    if not isinstance(platforms, list) or "iOS" not in platforms:
        raise SigningError("provisioning profile is not for iOS")

    permitted = profile.get("Entitlements")
    if not isinstance(permitted, dict):
        raise SigningError("provisioning profile has no entitlements dictionary")
    application_id = permitted.get("application-identifier")
    if application_id != f"{team}.{bundle_id}":
        raise SigningError("provisioning profile does not match the app's exact bundle identifier")
    if permitted.get("com.apple.developer.team-identifier") != team:
        raise SigningError("profile entitlement belongs to a different team")
    if permitted.get("get-task-allow") is not True:
        raise SigningError("profile is not an Apple development profile")

    capabilities = requested_capabilities(cloud_container)
    for key, value in capabilities.items():
        if not _profile_allows(permitted.get(key), value):
            raise SigningError(f"profile does not permit requested entitlement: {key}")
    certificates = profile.get("DeveloperCertificates")
    if not isinstance(certificates, list) or not certificates or not all(isinstance(item, bytes) for item in certificates):
        raise SigningError("profile has no usable developer certificates")

    # Keep the signed entitlement set minimal. Values that bind the signature
    # to an identity come from the validated profile, never caller input.
    entitlements = dict(capabilities)
    entitlements.update({
        "application-identifier": application_id,
        "com.apple.developer.team-identifier": team,
        "get-task-allow": True,
    })
    return entitlements


def _plist_from_output(output: bytes, label: str) -> dict[str, object]:
    try:
        value = plistlib.loads(output)
    except Exception:
        start = output.find(b"<?xml")
        end = output.rfind(b"</plist>")
        if start < 0 or end < start:
            raise SigningError(f"{label} did not return a property list") from None
        try:
            value = plistlib.loads(output[start:end + len(b"</plist>")])
        except Exception as error:
            raise SigningError(f"{label} returned an invalid property list") from error
    if not isinstance(value, dict):
        raise SigningError(f"{label} property list is not a dictionary")
    return value


def _decode_profile(profile_path: Path, security: str, runner: Runner) -> dict[str, object]:
    return _plist_from_output(
        _checked(runner, [security, "cms", "-D", "-i", str(profile_path)]),
        "security cms",
    )


def _matching_identity(profile: Mapping[str, object], security: str, runner: Runner) -> str:
    output = _checked(runner, [security, "find-identity", "-v", "-p", "codesigning"]).decode(errors="replace")
    installed = {match.group(1).upper() for match in _IDENTITY_RE.finditer(output)}
    for certificate in profile["DeveloperCertificates"]:
        fingerprint = hashlib.sha1(certificate).hexdigest().upper()
        if fingerprint in installed:
            return fingerprint
    raise SigningError("no installed Apple Development identity matches the provisioning profile")


def _write_helper(helper: Path, bundle_id: str, team: str, cloud_container: str | None) -> None:
    if helper.exists():
        shutil.rmtree(helper)
    sources = helper / "Sources"
    sources.mkdir(parents=True)
    (sources / "main.m").write_text(
        "#import <UIKit/UIKit.h>\n"
        "int main(int argc, char **argv) { @autoreleasepool {\n"
        "    return UIApplicationMain(argc, argv, nil, nil);\n"
        "} }\n"
    )
    (helper / "Signing.entitlements").write_bytes(
        plistlib.dumps(requested_capabilities(cloud_container), sort_keys=True)
    )
    quote = json.dumps
    (helper / "project.yml").write_text(
        "name: TOTKSigningHelper\n"
        "options:\n  deploymentTarget:\n    iOS: \"26.0\"\n"
        "targets:\n  TOTKSigningHelper:\n"
        "    type: application\n    platform: iOS\n    sources:\n      - Sources\n"
        "    settings:\n"
        f"      PRODUCT_BUNDLE_IDENTIFIER: {quote(bundle_id)}\n"
        f"      DEVELOPMENT_TEAM: {quote(team)}\n"
        "      PRODUCT_NAME: TOTKSigningHelper\n"
        "      CODE_SIGN_STYLE: Automatic\n"
        "      CODE_SIGN_ENTITLEMENTS: Signing.entitlements\n"
        "      GENERATE_INFOPLIST_FILE: YES\n"
        "      TARGETED_DEVICE_FAMILY: \"1,2\"\n"
        "      OTHER_LDFLAGS: \"$(inherited) -framework UIKit\"\n"
    )


def _nested_code(app: Path) -> list[Path]:
    # GameData contains the player's large extracted RomFS and is never code.
    # Restrict traversal to Apple's code-bearing bundle locations.
    code_roots = [app / name for name in ("Frameworks", "PlugIns", "XPCServices", "Watch")]
    paths = [path for root in code_roots if root.exists() for path in root.rglob("*")]
    unsupported = [path for path in paths if path.is_dir() and path.suffix in (".appex", ".app", ".xpc")]
    if unsupported:
        raise SigningError("nested app extensions require their own provisioning profiles")
    values = {
        path for path in paths
        if (path.is_dir() and path.suffix == ".framework") or (path.is_file() and path.suffix == ".dylib")
    }
    return sorted(values, key=lambda path: len(path.parts), reverse=True)


def sign_app(
    app: Path | str, team: str, device: str, cloud_container: str | None,
    work: Path | str, *, runner: Runner | None = None,
    now: datetime | None = None,
    tool_finder: Callable[[str], str | None] = shutil.which,
) -> SigningResult:
    """Provision and sign ``app`` for ``device`` after fail-closed validation.

    This function intentionally performs automatic provisioning when invoked.
    Tests should pass a fake ``runner`` and ``tool_finder``.
    """
    run = runner or _default_runner
    app_path = Path(app).expanduser().resolve()
    work_path = Path(work).expanduser().resolve()
    if not app_path.is_dir() or app_path.suffix != ".app":
        raise SigningError("app must be an existing .app directory")
    info_path = app_path / "Info.plist"
    try:
        info = plistlib.loads(info_path.read_bytes())
    except (OSError, plistlib.InvalidFileException) as error:
        raise SigningError("app has no readable Info.plist") from error
    bundle_id = _validate_bundle_id(info.get("CFBundleIdentifier") if isinstance(info, dict) else None)
    executable = info.get("CFBundleExecutable")
    if not isinstance(executable, str) or Path(executable).name != executable or not (app_path / executable).is_file():
        raise SigningError("app has no valid CFBundleExecutable")
    team = _validate_team(team)
    device = _validate_device(device)
    cloud_container = _validate_cloud_container(cloud_container)
    built_container = _validate_cloud_container(info.get("TOTKCloudContainer"))
    if built_container != cloud_container:
        raise SigningError("--cloud-container must exactly match TOTKCloudContainer in the built app")
    if work_path == app_path or app_path in work_path.parents:
        raise SigningError("work directory must be outside the app bundle")
    work_path.mkdir(parents=True, exist_ok=True)

    xcodegen = _tool("xcodegen", tool_finder)
    xcodebuild = _tool("xcodebuild", tool_finder)
    security = _tool("security", tool_finder)
    codesign = _tool("codesign", tool_finder)
    helper = work_path / "signing-helper"
    _write_helper(helper, bundle_id, team, cloud_container)
    _checked(run, [xcodegen, "generate", "--spec", "project.yml"], helper)
    _checked(run, [
        xcodebuild, "-project", "TOTKSigningHelper.xcodeproj", "-scheme", "TOTKSigningHelper",
        "-configuration", "Debug", "-sdk", "iphoneos", "-destination", f"id={device}",
        "-derivedDataPath", str(helper / "DerivedData"), "-allowProvisioningUpdates",
        "-allowProvisioningDeviceRegistration", "build",
    ], helper)
    profile_path = helper / "DerivedData/Build/Products/Debug-iphoneos/TOTKSigningHelper.app/embedded.mobileprovision"
    if not profile_path.is_file():
        raise SigningError("Xcode did not produce an embedded development profile")
    profile = _decode_profile(profile_path, security, run)
    entitlements = validate_profile(profile, bundle_id, team, device, cloud_container, now=now)
    identity = _matching_identity(profile, security, run)
    nested_code = _nested_code(app_path)
    expires = _utc(profile.get("ExpirationDate"), "expiration date")
    profile_uuid = profile.get("UUID")
    if not isinstance(profile_uuid, str) or not profile_uuid:
        raise SigningError("provisioning profile has no UUID")

    # Mutation starts only after profile, capabilities, device, expiry, and
    # signing identity have all been accepted.
    embedded = app_path / "embedded.mobileprovision"
    shutil.copy2(profile_path, embedded)
    entitlement_path = work_path / "app-entitlements.plist"
    entitlement_path.write_bytes(plistlib.dumps(entitlements, sort_keys=True))
    for child in nested_code:
        _checked(run, [codesign, "--force", "--sign", identity, "--timestamp=none", str(child)])
    _checked(run, [
        codesign, "--force", "--sign", identity, "--timestamp=none",
        "--generate-entitlement-der", "--entitlements", str(entitlement_path), str(app_path),
    ])
    _checked(run, [codesign, "--verify", "--deep", "--strict", "--verbose=2", str(app_path)])
    signed = _plist_from_output(
        _checked(run, [codesign, "-d", "--entitlements", ":-", str(app_path)]),
        "codesign entitlement inspection",
    )
    if signed != entitlements:
        raise SigningError("signed app entitlements differ from the validated entitlement set")
    if embedded.read_bytes() != profile_path.read_bytes():
        raise SigningError("embedded provisioning profile changed during signing")
    return SigningResult(app_path, bundle_id, profile_uuid, expires, cloud_container)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="prepared unsigned .app directory")
    parser.add_argument("--team", required=True, help="10-character Apple Developer Team ID")
    parser.add_argument("--device", required=True, help="connected device identifier (UDID)")
    parser.add_argument("--cloud-container", default=None, help="optional caller-owned iCloud.* CloudKit container")
    parser.add_argument("--work", type=Path, required=True, help="directory for the generated helper and entitlements")
    args = parser.parse_args(argv)
    try:
        result = sign_app(args.app, args.team, args.device, args.cloud_container, args.work)
    except (OSError, SigningError) as error:
        parser.exit(1, f"ERROR: {error}\n")
    print(json.dumps({
        "signed_app": str(result.app), "bundle_id": result.bundle_id,
        "profile_uuid": result.profile_uuid,
        "expires": result.expires.isoformat(), "cloud_container": result.cloud_container,
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Signing and optional CloudKit contracts, using synthetic profiles only."""

from datetime import datetime, timedelta, timezone
import hashlib
from pathlib import Path
import plistlib
import subprocess
import tempfile
import unittest

from tools import ios_sign


TEAM = "ABCDEFGHIJ"
DEVICE = "SYNTHETIC-DEVICE-1"
BUNDLE = "org.example.totk"
CLOUD = "iCloud.org.example.totk"
CERTIFICATE = b"synthetic development certificate"
NOW = datetime(2026, 10, 21, tzinfo=timezone.utc)


def profile(*, cloud=None, device=DEVICE, expires=None, bundle=BUNDLE, memory=True):
    entitlements = {
        "application-identifier": f"{TEAM}.{bundle}",
        "com.apple.developer.team-identifier": TEAM,
        "get-task-allow": True,
    }
    if memory:
        entitlements[ios_sign.MEMORY_ENTITLEMENT] = True
    if cloud:
        entitlements.update({
            ios_sign.CLOUD_CONTAINERS: [cloud],
            ios_sign.CLOUD_SERVICES: ["CloudKit"],
            ios_sign.CLOUD_ENVIRONMENT: ["Development", "Production"],
        })
    return {
        "UUID": "00000000-0000-0000-0000-000000000001",
        "TeamIdentifier": [TEAM],
        "ProvisionedDevices": [device],
        "Platform": ["iOS"],
        "ExpirationDate": expires or NOW + timedelta(days=7),
        "DeveloperCertificates": [CERTIFICATE],
        "Entitlements": entitlements,
    }


class FakeRunner:
    def __init__(self, supplied_profile):
        self.profile = supplied_profile
        self.commands = []
        self.signed_entitlements = None

    def __call__(self, command, cwd):
        command = tuple(command)
        self.commands.append((command, cwd))
        executable = Path(command[0]).name
        output = b""
        if executable == "xcodebuild":
            destination = Path(command[command.index("-derivedDataPath") + 1])
            built = destination / "Build/Products/Debug-iphoneos/TOTKSigningHelper.app"
            built.mkdir(parents=True)
            (built / "embedded.mobileprovision").write_bytes(b"synthetic cms envelope")
        elif executable == "security" and command[1] == "cms":
            output = plistlib.dumps(self.profile)
        elif executable == "security" and command[1] == "find-identity":
            digest = hashlib.sha1(CERTIFICATE).hexdigest().upper()
            output = f'  1) {digest} "Synthetic Apple Development"\n'.encode()
        elif executable == "codesign" and "--entitlements" in command and "--force" in command:
            entitlement_path = Path(command[command.index("--entitlements") + 1])
            self.signed_entitlements = plistlib.loads(entitlement_path.read_bytes())
        elif executable == "codesign" and "-d" in command:
            output = plistlib.dumps(self.signed_entitlements)
        return subprocess.CompletedProcess(command, 0, stdout=output)


def tools(name):
    return "/synthetic/bin/" + name


class IOSSigningTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.app = self.root / "TOTK.app"
        self.app.mkdir()
        (self.app / "Info.plist").write_bytes(plistlib.dumps({
            "CFBundleIdentifier": BUNDLE,
            "CFBundleExecutable": "TOTK",
        }))
        (self.app / "TOTK").write_bytes(b"synthetic executable")
        self.work = self.root / "work"

    def sign(self, supplied_profile, cloud=None):
        info = plistlib.loads((self.app / "Info.plist").read_bytes())
        if cloud:
            info["TOTKCloudContainer"] = cloud
        else:
            info.pop("TOTKCloudContainer", None)
        (self.app / "Info.plist").write_bytes(plistlib.dumps(info))
        runner = FakeRunner(supplied_profile)
        result = ios_sign.sign_app(
            self.app, TEAM, DEVICE, cloud, self.work,
            runner=runner, now=NOW, tool_finder=tools,
        )
        return result, runner

    def test_default_capabilities_are_memory_only(self):
        self.assertEqual(ios_sign.requested_capabilities(None), {
            ios_sign.MEMORY_ENTITLEMENT: True,
        })
        requested = ios_sign.requested_capabilities(CLOUD)
        self.assertEqual(requested[ios_sign.CLOUD_CONTAINERS], [CLOUD])
        self.assertEqual(requested[ios_sign.CLOUD_SERVICES], ["CloudKit"])
        self.assertEqual(requested[ios_sign.CLOUD_ENVIRONMENT], "Development")
        self.assertNotIn("com.apple.developer.ubiquity-container-identifiers", requested)

    def test_memory_only_profile_signs_frameworks_then_complete_app(self):
        framework = self.app / "Frameworks/Example.framework"
        framework.mkdir(parents=True)
        (framework / "Example").write_bytes(b"synthetic framework")
        dylib = self.app / "Frameworks/helper.dylib"
        dylib.write_bytes(b"synthetic dylib")
        result, runner = self.sign(profile())

        self.assertEqual(result.bundle_id, BUNDLE)
        self.assertIsNone(result.cloud_container)
        self.assertEqual((self.app / "embedded.mobileprovision").read_bytes(), b"synthetic cms envelope")
        self.assertEqual(runner.signed_entitlements, {
            ios_sign.MEMORY_ENTITLEMENT: True,
            "application-identifier": f"{TEAM}.{BUNDLE}",
            "com.apple.developer.team-identifier": TEAM,
            "get-task-allow": True,
        })
        xcodebuild = next(command for command, _ in runner.commands if Path(command[0]).name == "xcodebuild")
        self.assertIn(f"id={DEVICE}", xcodebuild)
        self.assertIn("-allowProvisioningUpdates", xcodebuild)
        self.assertIn("-allowProvisioningDeviceRegistration", xcodebuild)
        signed_paths = [command[-1] for command, _ in runner.commands
                        if Path(command[0]).name == "codesign" and "--force" in command]
        app = str(self.app.resolve())
        self.assertEqual(signed_paths[-1], app)
        self.assertLess(signed_paths.index(str(dylib.resolve())), signed_paths.index(app))
        self.assertLess(signed_paths.index(str(framework.resolve())), signed_paths.index(app))

    def test_cloud_profile_uses_only_callers_exact_development_container(self):
        result, runner = self.sign(profile(cloud=CLOUD), CLOUD)
        self.assertEqual(result.cloud_container, CLOUD)
        self.assertEqual(runner.signed_entitlements[ios_sign.CLOUD_CONTAINERS], [CLOUD])
        self.assertEqual(runner.signed_entitlements[ios_sign.CLOUD_SERVICES], ["CloudKit"])
        self.assertEqual(runner.signed_entitlements[ios_sign.CLOUD_ENVIRONMENT], "Development")
        helper = plistlib.loads((self.work / "signing-helper/Signing.entitlements").read_bytes())
        self.assertEqual(helper, ios_sign.requested_capabilities(CLOUD))
        project = (self.work / "signing-helper/project.yml").read_text()
        self.assertIn(BUNDLE, project)
        self.assertIn(TEAM, project)

    def test_rejects_profile_mismatches_before_mutating_app(self):
        bad_profiles = {
            "wrong device": profile(device="different-device"),
            "expired": profile(expires=NOW - timedelta(seconds=1)),
            "wrong app": profile(bundle="org.example.other"),
            "missing memory capability": profile(memory=False),
            "wrong cloud namespace": profile(cloud="iCloud.org.example.other"),
            "wrong platform": dict(profile(), Platform=["tvOS"]),
        }
        for label, supplied in bad_profiles.items():
            with self.subTest(label=label):
                info = plistlib.loads((self.app / "Info.plist").read_bytes())
                if label == "wrong cloud namespace":
                    info["TOTKCloudContainer"] = CLOUD
                else:
                    info.pop("TOTKCloudContainer", None)
                (self.app / "Info.plist").write_bytes(plistlib.dumps(info))
                (self.app / "embedded.mobileprovision").unlink(missing_ok=True)
                runner = FakeRunner(supplied)
                with self.assertRaises(ios_sign.SigningError):
                    ios_sign.sign_app(
                        self.app, TEAM, DEVICE,
                        CLOUD if label == "wrong cloud namespace" else None,
                        self.work, runner=runner, now=NOW, tool_finder=tools,
                    )
                self.assertFalse((self.app / "embedded.mobileprovision").exists())
                self.assertFalse(any(Path(command[0]).name == "codesign" for command, _ in runner.commands))

    def test_rejects_unmatched_identity_and_nested_extension_before_mutation(self):
        runner = FakeRunner(profile())
        runner.profile["DeveloperCertificates"] = [b"a different certificate"]
        with self.assertRaisesRegex(ios_sign.SigningError, "identity"):
            ios_sign.sign_app(
                self.app, TEAM, DEVICE, None, self.work,
                runner=runner, now=NOW, tool_finder=tools,
            )
        self.assertFalse((self.app / "embedded.mobileprovision").exists())

        extension = self.app / "PlugIns/Unsafe.appex"
        extension.mkdir(parents=True)
        runner = FakeRunner(profile())
        with self.assertRaisesRegex(ios_sign.SigningError, "own provisioning profiles"):
            ios_sign.sign_app(
                self.app, TEAM, DEVICE, None, self.work,
                runner=runner, now=NOW, tool_finder=tools,
            )
        self.assertFalse((self.app / "embedded.mobileprovision").exists())

    def test_rejects_invalid_caller_identifiers(self):
        cases = [
            ("bad team", DEVICE, None),
            (TEAM, "device with spaces", None),
            (TEAM, DEVICE, "iCloud.other..container"),
            (TEAM, DEVICE, "group.org.example"),
        ]
        for team, device, cloud in cases:
            with self.subTest(team=team, device=device, cloud=cloud):
                with self.assertRaises(ios_sign.SigningError):
                    ios_sign.sign_app(
                        self.app, team, device, cloud, self.work,
                        runner=FakeRunner(profile()), now=NOW, tool_finder=tools,
                    )

    def test_rejects_cloud_container_that_does_not_match_built_app(self):
        runner = FakeRunner(profile(cloud=CLOUD))
        with self.assertRaisesRegex(ios_sign.SigningError, "exactly match"):
            ios_sign.sign_app(
                self.app, TEAM, DEVICE, CLOUD, self.work,
                runner=runner, now=NOW, tool_finder=tools,
            )
        self.assertEqual(runner.commands, [])

    def test_rejects_scalar_wildcards_and_work_inside_app(self):
        wildcard_memory = profile()
        wildcard_memory["Entitlements"][ios_sign.MEMORY_ENTITLEMENT] = "*"
        with self.assertRaisesRegex(ios_sign.SigningError, "increased-memory"):
            ios_sign.validate_profile(wildcard_memory, BUNDLE, TEAM, DEVICE, None, now=NOW)
        with self.assertRaisesRegex(ios_sign.SigningError, "outside the app"):
            ios_sign.sign_app(
                self.app, TEAM, DEVICE, None, self.app / "signing-work",
                runner=FakeRunner(profile()), now=NOW, tool_finder=tools,
            )


class CloudSaveSourceTests(unittest.TestCase):
    def test_cloudkit_is_optional_and_no_personal_namespace_remains(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / "games/totk/ios/Sources/TOTKCloudSave.m").read_text()
        header = (root / "games/totk/ios/Sources/TOTKCloudSave.h").read_text()
        entitlements = plistlib.loads((root / "games/totk/ios/Sources/TOTK.entitlements").read_bytes())
        self.assertIn('infoDictionary[@"TOTKCloudContainer"]', source)
        self.assertNotIn('containerWithIdentifier:@"', source)
        self.assertIn("if(!self.database)", source)
        self.assertNotIn("Checking iCloud saves", source)
        self.assertIn("Without TOTKCloudContainer, saves stay local", header)
        self.assertEqual(entitlements, {ios_sign.MEMORY_ENTITLEMENT: True})


if __name__ == "__main__":
    unittest.main()

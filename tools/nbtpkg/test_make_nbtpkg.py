#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Tests of make_nbtpkg.py. Every program, zip and key here is invented: the programs are ELF headers with a few
strings after them, and the key is made for the run.

  NBTAPPS_HEADUNIT=../nbtapps/headunit python3 -m unittest discover -s tools/nbtpkg -p 'test_*.py'

NBTAPPS_HEADUNIT is the headunit/ folder of a checkout of https://github.com/tom-pryor/nbtapps (for nbtpkg.py).
Without it the tests look for ../nbtapps/headunit beside this repo, and are skipped when that is not there. When
NBTAPPS_HEADUNIT is set and wrong, they fail. If NBTAPPS_PACKAGES names a checkout of tom-pryor/nbtapps-packages
(or one is beside this repo), the records are also checked against its schema/version.schema.json.
"""
import contextlib
import hashlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest
import zipfile
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_nbtpkg as mk  # noqa: E402

NBTAPPS = os.environ.get("NBTAPPS_HEADUNIT") or mk.DEFAULT_NBTAPPS
PACKAGES = os.environ.get("NBTAPPS_PACKAGES") or os.path.join(os.path.dirname(mk.REPO_ROOT), "nbtapps-packages")
nbt = None

REPO = "example/nbt-aad-test"
COMMIT = "0123456789abcdef0123456789abcdef01234567"
TAG = "test-release"
VERSION = "2.3.4"
PUBLISHED = "2030-01-02T23:59:58Z"


def setUpModule():
    global nbt
    if not os.path.isfile(os.path.join(NBTAPPS, "tools", "nbtpkg.py")):
        if os.environ.get("NBTAPPS_HEADUNIT"):
            raise RuntimeError("NBTAPPS_HEADUNIT=%s has no tools/nbtpkg.py" % NBTAPPS)
        raise unittest.SkipTest("nbtpkg.py is not at %s: set NBTAPPS_HEADUNIT to the headunit/ folder of a "
                                "checkout of tom-pryor/nbtapps" % NBTAPPS)
    try:
        import cryptography  # noqa: F401
    except ImportError:
        raise unittest.SkipTest("the python module `cryptography` is not installed")
    nbt = mk.load_nbtpkg(NBTAPPS)


def fake_elf(machine=mk.EM_ARM, elf_class=1, strings=mk.FOLDER_STRINGS, extra=b""):
    header = bytearray(52)
    header[:4] = mk.ELF_MAGIC
    header[4], header[5], header[6] = elf_class, 1, 1
    struct.pack_into("<HH", header, 16, 3, machine)
    return bytes(header) + b"\0" + b"\0".join(strings) + b"\0" + extra


def fake_daemon(version=b"v2.3.4-test", **kwargs):
    return fake_elf(extra=b"NBT-AAD-VERSION=" + version + b"\0more text\0", **kwargs)


def fake_plugin(library=None):
    files = {name: ("invented %s\n" % name).encode("ascii") * 3 for name in mk.PLUGIN_FILES}
    files["libNbtAad.so"] = library if library is not None else fake_elf(extra=b"invented plugin\0")
    return files


def make_zip(files, folder=mk.ZIP_FOLDER, extra=(), dir_entry=False):
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        if dir_entry:
            z.writestr(folder, b"")
        for name, data in files.items():
            z.writestr(folder + name, data)
        for name, data in extra:
            z.writestr(name, data)
    return out.getvalue()


class FakeGitHub:
    """The GitHub class's calls, over releases held in memory."""

    def __init__(self, releases):
        self.repo = REPO
        self.all = releases
        self.uploads = []
        self.downloads = []
        self.next_id = 1000

    def repo_url(self):
        return "https://github.com/" + REPO

    def release_by_tag(self, tag):
        return next((r for r in self.all if r["tag_name"] == tag), None)

    def releases(self):
        return iter(self.all)

    def download(self, asset, limit):
        self.downloads.append(asset["name"])
        if len(asset["_data"]) > limit:
            raise mk.Refused("%s is too large" % asset["name"])
        return asset["_data"]

    def upload(self, release, name, data, content_type):
        if mk.asset_named(release, name):
            raise AssertionError("an asset was overwritten: %s" % name)
        self.next_id += 1
        asset = {"id": self.next_id, "name": name, "size": len(data), "_data": data,
                 "url": "https://api.example/assets/%d" % self.next_id,
                 "browser_download_url": "https://github.com/%s/releases/download/%s/%s"
                                         % (REPO, release["tag_name"], name)}
        release["assets"].append(asset)
        self.uploads.append((release["tag_name"], name, content_type))
        return asset

    def tag_commit(self, tag):
        return COMMIT


def asset(name, data):
    return {"id": hash(name) & 0xFFFF, "name": name, "size": len(data), "_data": data, "url": "https://x/" + name}


def release(tag=TAG, rid=1, files=None, name="Test build: invented notes", prerelease=False):
    if files is None:
        files = {mk.DAEMON: fake_daemon(), mk.PLUGIN_ZIP: make_zip(fake_plugin())}
    return {"id": rid, "tag_name": tag, "name": name, "prerelease": prerelease, "published_at": PUBLISHED,
            "upload_url": "https://uploads.example/releases/%d/assets{?name,label}" % rid,
            "assets": [asset(n, d) for n, d in files.items()]}


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.seed = bytes(range(32))  # invented, for this test only
        self.public = nbt.public_key(self.seed)
        quiet = contextlib.redirect_stdout(io.StringIO())
        self.out = quiet.__enter__()
        self.addCleanup(quiet.__exit__, None, None, None)

    def path(self, *parts):
        return os.path.join(self.tmp.name, *parts)

    def refuses(self, text, function, *args, **kwargs):
        with self.assertRaises(mk.Refused) as caught:
            function(*args, **kwargs)
        self.assertIn(text, str(caught.exception))
        self.assertNotIn("\n", str(caught.exception))
        return str(caught.exception)

    def fetch(self, gh, work="work", tag=TAG):
        return mk.run_fetch(gh, nbt, tag, self.path(work), environ={"GITHUB_STEP_SUMMARY": self.path("summary.md")})

    def sign(self, gh, work="work", seed=None, public=None):
        return mk.run_sign(gh, nbt, self.path(work), seed or self.seed, public or self.public,
                           environ={"GITHUB_STEP_SUMMARY": self.path("summary.md")})

    def check_schema(self, record):
        schema_path = os.path.join(PACKAGES, "schema", "version.schema.json")
        if not os.path.isfile(schema_path):
            return
        with open(schema_path, "rb") as f:
            schema = json.load(f)
        nbt.validate(record, schema, schema, "record")


class HappyPath(Base):
    def test_fetch_and_sign(self):
        gh = FakeGitHub([release(), release("older", rid=2)])
        state = self.fetch(gh)
        self.assertEqual(state["version"], VERSION)
        self.assertEqual(state["released"], "2030-01-02")
        staged = sorted(os.path.relpath(os.path.join(d, n), self.path("work", "stage"))
                        for d, _, names in os.walk(self.path("work", "stage")) for n in names)
        self.assertEqual(staged, sorted(["manifest.json", "LICENSE.md", "payload/nbt-aad"]
                                        + ["payload/plugin/" + n for n in mk.PLUGIN_FILES]))

        document = self.sign(gh)
        archive = mk.archive_name(VERSION)
        self.assertEqual(gh.uploads, [(TAG, archive, "application/octet-stream"),
                                      (TAG, mk.RECORD_ASSET, "application/json")])
        data = mk.asset_named(gh.all[0], archive)["_data"]
        info = nbt.verify_package(data, keys=[self.public])
        self.assertEqual((info["id"], info["version"], info["name"]), (mk.PACKAGE_ID, VERSION, "Android Auto"))
        self.assertEqual(info["flags"], ["needs-reboot"])

        manifest = json.loads(nbt.read_zip(data)[0]["data"])
        self.assertEqual(manifest["install"], {
            "files": [{"src": "payload/nbt-aad", "dest": "${app}/nbt-aad", "mode": "0755"},
                      {"src": "payload/plugin/", "dest": "${plugins}/NbtAad/", "mode": "0644"}],
            "serialServices": [{"name": "AndroidAuto", "program": "nbt-aad"}],
            "reboot": "hmi-restart"})
        self.assertEqual(sorted(c["path"] for c in manifest["contents"]),
                         sorted(["LICENSE.md", "payload/nbt-aad"] + ["payload/plugin/" + n for n in mk.PLUGIN_FILES]))

        self.assertEqual(document, {
            "id": mk.PACKAGE_ID, "version": VERSION,
            "record": {"url": "https://github.com/%s/releases/download/%s/%s" % (REPO, TAG, archive),
                       "sha256": hashlib.sha256(data).hexdigest(), "released": "2030-01-02",
                       "notes": ["Test build: invented notes"],
                       "source": {"repo": "https://github.com/" + REPO, "commit": COMMIT}}})
        attached = mk.asset_named(gh.all[0], mk.RECORD_ASSET)["_data"]
        self.assertEqual(json.loads(attached.decode("utf-8")), document)
        self.assertEqual(list(json.loads(attached)["record"]), ["url", "sha256", "released", "notes", "source"])
        self.check_schema(document["record"])
        with open(self.path("summary.md"), encoding="utf-8") as f:
            summary = f.read()
        self.assertIn(hashlib.sha256(data).hexdigest(), summary)
        self.assertIn('"commit": "%s"' % COMMIT, summary)

    def test_zip_with_folder_entry(self):
        gh = FakeGitHub([release(files={mk.DAEMON: fake_daemon(),
                                        mk.PLUGIN_ZIP: make_zip(fake_plugin(), dir_entry=True)})])
        self.assertEqual(self.fetch(gh)["version"], VERSION)

    def test_again_is_idempotent(self):
        gh = FakeGitHub([release()])
        self.fetch(gh, "one")
        first = self.sign(gh, "one")
        uploads = list(gh.uploads)
        self.fetch(gh, "two")
        self.assertEqual(self.sign(gh, "two"), first)
        self.assertEqual(gh.uploads, uploads)

    def test_pre_release_not_packaged(self):
        gh = FakeGitHub([release(prerelease=True)])
        self.assertEqual(set(self.fetch(gh)), {"skip"})
        self.assertEqual(gh.downloads, [])
        self.assertIsNone(mk.sign_step(nbt, self.path("work"), author_pub=self.path("none.pub"), environ={}))
        self.assertEqual(gh.uploads, [])
        with open(self.path("summary.md"), encoding="utf-8") as f:
            self.assertIn("Not packaged: the release %s is a pre-release" % TAG, f.read())

    def test_pre_release_does_not_use_up_the_version(self):
        gh = FakeGitHub([release("test-build", rid=5, prerelease=True), release()])
        self.assertEqual(set(self.fetch(gh, "pre", tag="test-build")), {"skip"})   # same version, skipped
        self.fetch(gh)
        self.assertEqual(self.sign(gh)["version"], VERSION)
        self.assertEqual({u[0] for u in gh.uploads}, {TAG})

    def test_became_pre_release_before_sign(self):
        gh = FakeGitHub([release()])
        self.fetch(gh)
        gh.all[0]["prerelease"] = True
        self.assertIsNone(self.sign(gh))
        self.assertEqual(gh.uploads, [])

    def test_pre_release_switch(self):
        gh = FakeGitHub([release(prerelease=True)])
        with mock.patch.object(mk, "PACKAGE_PRERELEASES", True):
            self.fetch(gh)
            self.sign(gh)
        self.assertEqual([u[1] for u in gh.uploads], [mk.archive_name(VERSION), mk.RECORD_ASSET])
        with open(self.path("summary.md"), encoding="utf-8") as f:
            self.assertIn("is a pre-release: the record is not proposed", f.read())

    def test_no_title_no_notes(self):
        gh = FakeGitHub([release(name="  \t ")])
        self.fetch(gh)
        self.assertNotIn("notes", self.sign(gh)["record"])


class FetchRefusals(Base):
    def files(self, daemon=None, plugin_zip=None):
        return {mk.DAEMON: daemon if daemon is not None else fake_daemon(),
                mk.PLUGIN_ZIP: plugin_zip if plugin_zip is not None else make_zip(fake_plugin())}

    def refuses_fetch(self, text, files=None, releases=None):
        gh = FakeGitHub(releases or [release(files=files)])
        message = self.refuses(text, self.fetch, gh)
        self.assertEqual(gh.uploads, [])
        return message

    def test_no_release(self):
        self.refuses_fetch("has no published release with the tag", releases=[release("other")])

    def test_missing_daemon(self):
        self.refuses_fetch("has no nbt-aad", files={mk.PLUGIN_ZIP: make_zip(fake_plugin())})

    def test_missing_zip(self):
        self.refuses_fetch("has no Releases.zip", files={mk.DAEMON: fake_daemon()})

    def test_not_a_zip(self):
        self.refuses_fetch("is not a zip archive", self.files(plugin_zip=b"PK\x03\x04 invented, not a zip"))

    def test_zip_missing_file(self):
        plugin = fake_plugin()
        del plugin["NbtAad.xml"]
        self.refuses_fetch("has no Releases/NbtAad.xml", self.files(plugin_zip=make_zip(plugin)))

    def test_zip_extra_file(self):
        self.refuses_fetch("which is not one of the five plugin files",
                           self.files(plugin_zip=make_zip(fake_plugin(), extra=[("Releases/extra.txt", b"x")])))

    def test_zip_path_outside(self):
        self.refuses_fetch("which is not one of the five plugin files",
                           self.files(plugin_zip=make_zip(fake_plugin(), extra=[("../libNbtAad.so", b"x")])))

    def test_zip_wrong_folder(self):
        self.refuses_fetch("which is not one of the five plugin files",
                           self.files(plugin_zip=make_zip(fake_plugin(), folder="Other/")))

    def test_zip_twice(self):
        with mock.patch("warnings.warn"):
            twice = make_zip(fake_plugin(), extra=[("Releases/NbtAad.xml", b"again")])
        self.refuses_fetch("holds Releases/NbtAad.xml twice", self.files(plugin_zip=twice))

    def test_zip_empty_file(self):
        plugin = fake_plugin()
        plugin["NbtAad.manifest"] = b""
        self.refuses_fetch("is empty", self.files(plugin_zip=make_zip(plugin)))

    def test_zip_file_too_large(self):
        with mock.patch.object(mk, "MAX_PLUGIN_FILE", 100):
            self.refuses_fetch("more than the 100 allowed", self.files())

    def test_zip_total_too_large(self):
        with mock.patch.object(mk, "MAX_PLUGIN_TOTAL", 200):
            self.refuses_fetch("add up to more than", self.files())

    def test_zip_too_large(self):
        with mock.patch.object(mk, "MAX_ZIP", 100):
            self.refuses_fetch("is too large", self.files())

    def test_daemon_too_large(self):
        with mock.patch.object(mk, "MAX_DAEMON", 100):
            self.refuses_fetch("is too large", self.files())

    def test_daemon_not_elf(self):
        self.refuses_fetch("nbt-aad is not an ELF program", self.files(daemon=b"#!/bin/sh\n" + b"x" * 100))

    def test_daemon_not_arm(self):
        self.refuses_fetch("nbt-aad is not a 32-bit little-endian ARM program",
                           self.files(daemon=fake_daemon(machine=3)))

    def test_daemon_64_bit(self):
        self.refuses_fetch("nbt-aad is not a 32-bit", self.files(daemon=fake_daemon(elf_class=2)))

    def test_plugin_not_arm(self):
        plugin = fake_plugin(library=fake_elf(machine=62))
        self.refuses_fetch("libNbtAad.so is not a 32-bit", self.files(plugin_zip=make_zip(plugin)))

    def test_daemon_built_before_run_time_folders(self):
        message = self.refuses_fetch("built before the programs found their folders at run time",
                                     self.files(daemon=fake_daemon(strings=(b"/opt/aa",))))
        self.assertTrue(message.startswith("nbt-aad does not contain /var/opt/nbtapp/apps/" + mk.PACKAGE_ID))

    def test_daemon_without_data_folder(self):
        self.refuses_fetch("nbt-aad does not contain /var/opt/nbtapp/data/" + mk.PACKAGE_ID,
                           self.files(daemon=fake_daemon(strings=mk.FOLDER_STRINGS[:1])))

    def test_plugin_built_before_run_time_folders(self):
        plugin = fake_plugin(library=fake_elf(strings=(b"/opt/aa/nbt-aad",)))
        self.refuses_fetch("libNbtAad.so does not contain", self.files(plugin_zip=make_zip(plugin)))

    def test_folder_strings_split(self):
        split = (b"/var/opt/nbtapp/apps/", mk.PACKAGE_ID.encode("ascii"), mk.FOLDER_STRINGS[1])
        self.refuses_fetch("does not contain", self.files(daemon=fake_daemon(strings=split)))

    def test_no_version(self):
        self.refuses_fetch("has no NBT-AAD-VERSION", self.files(daemon=fake_elf(extra=b"NBT-AAD-VERSION=dev\0")))

    def test_two_versions(self):
        daemon = fake_elf(extra=b"NBT-AAD-VERSION=v2.3.4\0NBT-AAD-VERSION=v2.3.5\0")
        self.refuses_fetch("2 different NBT-AAD-VERSION strings", self.files(daemon=daemon))

    def test_leading_zero_version(self):
        self.refuses_fetch("is not a package version", self.files(daemon=fake_daemon(version=b"v2.03.4")))

    def test_version_on_another_release(self):
        archive = mk.archive_name(VERSION)
        other = release("earlier", rid=2, files={archive: b"invented archive"})
        message = self.refuses_fetch("is already on the release earlier", releases=[release(), other])
        self.assertIn("raise the daemon's version", message)

    def test_work_not_empty(self):
        os.makedirs(self.path("work"))
        with open(self.path("work", "left"), "w") as f:
            f.write("x")
        self.refuses_fetch("is not empty", self.files())


class SignRefusals(Base):
    def prepared(self, **kwargs):
        gh = FakeGitHub([release(**kwargs)])
        self.fetch(gh)
        return gh

    def test_no_state(self):
        self.refuses("has no state.json", self.sign, FakeGitHub([release()]))

    def test_wrong_key(self):
        gh = self.prepared()
        other = bytes(range(1, 33))
        self.refuses("not signed by the key in the author public key file", self.sign, gh, seed=other)
        self.assertEqual(gh.uploads, [])

    def test_same_version_other_bytes(self):
        gh = self.prepared()
        gh.all[0]["assets"].append(asset(mk.archive_name(VERSION), b"invented other archive"))
        message = self.refuses("on this release already with other bytes", self.sign, gh)
        self.assertIn("raise the daemon's version", message)
        self.assertEqual(gh.uploads, [])

    def test_version_on_another_release_meanwhile(self):
        gh = self.prepared()
        gh.all.append(release("meanwhile", rid=3, files={mk.archive_name(VERSION): b"invented"}))
        self.refuses("is already on the release meanwhile", self.sign, gh)
        self.assertEqual(gh.uploads, [])

    def test_record_differs(self):
        gh = self.prepared()
        gh.all[0]["assets"].append(asset(mk.RECORD_ASSET, b"{}\n"))
        self.refuses("never overwritten", self.sign, gh)
        self.assertEqual([u[1] for u in gh.uploads], [mk.archive_name(VERSION)])

    def test_release_replaced(self):
        gh = self.prepared()
        gh.all[0]["id"] = 99
        self.refuses("not the one the fetch step read", self.sign, gh)


class CommandLine(Base):
    """main(): the key arrives in NBTPKG_AUTHOR_KEY and is never printed. Without author.pub the set-up is not
    finished and both steps end well with nothing done; with it, a missing or wrong key fails."""

    def run_main(self, argv, environ, author_pub, gh):
        stdout, stderr = io.StringIO(), io.StringIO()
        code = 0
        environ = dict({"GITHUB_STEP_SUMMARY": self.path("summary.md")}, **environ)
        with mock.patch.dict(os.environ, environ), mock.patch.object(mk, "AUTHOR_PUB", author_pub), \
                mock.patch.object(mk.GitHub, "from_env", return_value=gh), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            if "NBTPKG_AUTHOR_KEY" not in environ:
                os.environ.pop("NBTPKG_AUTHOR_KEY", None)
            try:
                mk.main(["--nbtapps", NBTAPPS] + argv)
            except SystemExit as e:
                code = e.code
        return code, stdout.getvalue() + stderr.getvalue()

    def write_pub(self, public, text=None):
        path = self.path("author.pub")
        with open(path, "w") as f:
            f.write(text if text is not None else "# invented test key\n%s\n" % public.hex())
        return path

    def fetched(self, gh=None):
        gh = gh or FakeGitHub([release()])
        code, output = self.run_main(["fetch", "--tag", TAG, "--work", self.path("work")], {},
                                     self.write_pub(self.public), gh)
        self.assertEqual(code, 0, output)
        return gh

    def summary(self):
        with open(self.path("summary.md"), encoding="utf-8") as f:
            return f.read()

    def test_sign_by_main(self):
        gh = self.fetched()
        env = {"NBTPKG_AUTHOR_KEY": self.seed.hex() + "\n"}
        code, output = self.run_main(["sign", "--work", self.path("work")], env, self.path("author.pub"), gh)
        self.assertEqual(code, 0, output)
        self.assertNotIn(self.seed.hex(), output)
        self.assertIn('"id": "%s"' % mk.PACKAGE_ID, output)
        self.assertEqual([u[1] for u in gh.uploads], [mk.archive_name(VERSION), mk.RECORD_ASSET])
        for name in os.listdir(self.path("work")):
            if os.path.isfile(self.path("work", name)):
                with open(self.path("work", name), "rb") as f:
                    self.assertNotIn(self.seed.hex().encode(), f.read())

    def test_author_pub_missing_skips_both_steps(self):
        env = {"GITHUB_ACTIONS": "true"}
        no_network = mock.Mock(side_effect=AssertionError("a network call"))
        with mock.patch.object(mk.GitHub, "_call", no_network):
            code, output = self.run_main(["fetch", "--tag", TAG, "--work", self.path("work")], env,
                                         self.path("none.pub"), mk.GitHub(REPO, "invented-token"))
            self.assertEqual(code, 0, output)
            self.assertIn("::notice::the set-up is not finished", output)
            self.assertIn("tools/nbtpkg/README.md", output)
            self.assertEqual(os.listdir(self.path("work")), ["state.json"])
            # the sign step: no key, no author.pub, and still no failure
            code, output = self.run_main(["sign", "--work", self.path("work")], env, self.path("none.pub"),
                                         mk.GitHub(REPO, "invented-token"))
        self.assertEqual(code, 0, output)
        self.assertIn("nothing to sign: the set-up is not finished", output)
        no_network.assert_not_called()
        self.assertIn("Not packaged: the set-up is not finished", self.summary())
        self.assertIn("tools/nbtpkg/README.md", self.summary())

    def test_author_pub_broken_fails(self):
        gh = FakeGitHub([release()])
        code, _ = self.run_main(["fetch", "--tag", TAG, "--work", self.path("work")], {},
                                self.write_pub(None, "not a key\n"), gh)
        self.assertIn("author.pub is not a key", str(code))
        self.assertEqual(gh.downloads, [])

    def test_key_missing_fails(self):
        gh = self.fetched()
        code, _ = self.run_main(["sign", "--work", self.path("work")], {}, self.path("author.pub"), gh)
        self.assertIn("NBTPKG_AUTHOR_KEY is not set", str(code))
        self.assertEqual(gh.uploads, [])

    def test_key_empty_fails(self):
        gh = self.fetched()
        code, _ = self.run_main(["sign", "--work", self.path("work")], {"NBTPKG_AUTHOR_KEY": ""},
                                self.path("author.pub"), gh)
        self.assertIn("NBTPKG_AUTHOR_KEY is not set", str(code))
        self.assertEqual(gh.uploads, [])

    def test_key_malformed_fails_not_echoed(self):
        gh = self.fetched()
        bad = self.seed.hex()[:-2] + "zz"
        code, output = self.run_main(["sign", "--work", self.path("work")], {"NBTPKG_AUTHOR_KEY": bad},
                                     self.path("author.pub"), gh)
        self.assertIn("NBTPKG_AUTHOR_KEY is not a key", str(code))
        self.assertNotIn(bad, str(code) + output)
        self.assertEqual(gh.uploads, [])

    def test_key_not_author_pub_fails(self):
        gh = self.fetched()
        other = bytes(range(1, 33))   # invented
        code, output = self.run_main(["sign", "--work", self.path("work")], {"NBTPKG_AUTHOR_KEY": other.hex()},
                                     self.path("author.pub"), gh)
        self.assertIn("not signed by the key in the author public key file", str(code))
        self.assertNotIn(other.hex(), str(code) + output)
        self.assertEqual(gh.uploads, [])

    def test_old_build_by_main(self):
        gh = FakeGitHub([release(files={mk.DAEMON: fake_daemon(strings=(b"/opt/aa",)),
                                        mk.PLUGIN_ZIP: make_zip(fake_plugin())})])
        code, output = self.run_main(["fetch", "--tag", TAG, "--work", self.path("work")], {},
                                     self.write_pub(self.public), gh)
        self.assertIn("built before the programs found their folders at run time", str(code))
        self.assertIn("v2.3.4-test", output)

    def test_pre_release_skips_both_steps(self):
        gh = FakeGitHub([release(prerelease=True)])
        env = {"GITHUB_ACTIONS": "true"}
        code, output = self.run_main(["fetch", "--tag", TAG, "--work", self.path("work")], env,
                                     self.write_pub(self.public), gh)
        self.assertEqual(code, 0, output)
        self.assertIn("::notice::the release %s is a pre-release" % TAG, output)
        self.assertEqual(gh.downloads, [])
        self.assertEqual(os.listdir(self.path("work")), ["state.json"])
        code, output = self.run_main(["sign", "--work", self.path("work")],
                                     dict(env, NBTPKG_AUTHOR_KEY=self.seed.hex()), self.path("author.pub"), gh)
        self.assertEqual(code, 0, output)
        self.assertIn("nothing to sign: the release %s is a pre-release" % TAG, output)
        self.assertEqual(gh.uploads, [])
        self.assertIn("Not packaged: the release %s is a pre-release" % TAG, self.summary())


class Local(Base):
    def write_inputs(self, daemon=None, plugin=None):
        os.makedirs(self.path("in", "Releases"))
        with open(self.path("in", "nbt-aad"), "wb") as f:
            f.write(daemon or fake_daemon())
        plugin = plugin or fake_plugin()
        with open(self.path("in", "Releases.zip"), "wb") as f:
            f.write(make_zip(plugin))
        for name, data in plugin.items():
            with open(self.path("in", "Releases", name), "wb") as f:
                f.write(data)

    def local(self, *extra):
        args = mk.parse_args(["local", "--daemon", self.path("in", "nbt-aad"), "--out", self.path("out")]
                             + list(extra))
        return mk.run_local(nbt, args)

    def test_unsigned_no_record(self):
        self.write_inputs()
        self.assertIsNone(self.local("--zip", self.path("in", "Releases.zip")))
        with open(self.path("out", mk.archive_name(VERSION)), "rb") as f:
            data = f.read()
        info = nbt.verify_package(data, keys=None, allow_unsigned=True)
        self.assertNotIn("signature", info)
        self.assertFalse(os.path.exists(self.path("out", mk.RECORD_ASSET)))

    def test_signed_with_record(self):
        self.write_inputs()
        key = self.path("test.key")
        with open(key, "w") as f:
            f.write("# invented\n%s\n" % self.seed.hex())
        pub = self.path("test.pub")
        with open(pub, "w") as f:
            f.write("%s\n" % self.public.hex())
        document = self.local("--plugin-dir", self.path("in", "Releases"), "--key", key, "--pub", pub,
                              "--repo", REPO, "--tag", TAG, "--commit", COMMIT, "--released", "2030-01-02",
                              "--title", "Invented\ntitle")
        with open(self.path("out", mk.archive_name(VERSION)), "rb") as f:
            data = f.read()
        nbt.verify_package(data, keys=[self.public])
        self.assertEqual(document["record"]["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(document["record"]["notes"], ["Invented title"])
        with open(self.path("out", mk.RECORD_ASSET), "rb") as f:
            self.assertEqual(json.loads(f.read()), document)
        self.check_schema(document["record"])
        # the same again is fine: the build is deterministic
        self.local("--plugin-dir", self.path("in", "Releases"), "--key", key, "--pub", pub,
                   "--repo", REPO, "--tag", TAG, "--commit", COMMIT, "--released", "2030-01-02",
                   "--title", "Invented\ntitle")

    def test_signed_with_wrong_pub(self):
        self.write_inputs()
        key = self.path("test.key")
        with open(key, "w") as f:
            f.write("%s\n" % self.seed.hex())
        pub = self.path("other.pub")
        with open(pub, "w") as f:
            f.write("%s\n" % nbt.public_key(bytes(32)).hex())
        self.refuses("not signed by the key", self.local, "--zip", self.path("in", "Releases.zip"),
                     "--key", key, "--pub", pub)

    def test_no_overwrite(self):
        self.write_inputs()
        os.makedirs(self.path("out"))
        with open(self.path("out", mk.archive_name(VERSION)), "wb") as f:
            f.write(b"something else")
        self.refuses("it is not overwritten", self.local, "--zip", self.path("in", "Releases.zip"))

    def test_bad_commit(self):
        self.write_inputs()
        self.refuses("would be refused by nbtapps-packages", self.local, "--zip", self.path("in", "Releases.zip"),
                     "--repo", REPO, "--tag", TAG, "--commit", "abc")

    def test_missing_plugin_file(self):
        self.write_inputs()
        os.remove(self.path("in", "Releases", "NbtAad.xml"))
        self.refuses("cannot be read", self.local, "--plugin-dir", self.path("in", "Releases"))


class Parts(Base):
    def test_notes(self):
        self.assertEqual(mk.clean_note("A\tb\r\n  c\x7f\x85d"), "A b c d")
        self.assertIsNone(mk.clean_note(" \x00  "))
        self.assertIsNone(mk.clean_note(None))
        self.assertIsNone(mk.clean_note("﻿"))
        long = mk.clean_note("word " * 100)
        self.assertEqual(len(long), 200)
        self.assertTrue(long.endswith("…"))

    def test_version(self):
        self.assertEqual(mk.daemon_version(fake_daemon(version=b"v10.0.123-x")), "10.0.123")
        self.assertEqual(mk.daemon_version(fake_daemon(version=b"1.2.3")), "1.2.3")
        # the same version twice is one version
        self.assertEqual(mk.daemon_version(fake_elf(extra=b"NBT-AAD-VERSION=v1.2.3\0NBT-AAD-VERSION=v1.2.3-b\0")),
                         "1.2.3")

    def test_template(self):
        for change, text in ((("id", "com.example.other"), "must have the id"),
                             (("version", "1.0.0"), "must have the version"),
                             (("contents", []), "must not have contents")):
            with open(mk.TEMPLATE, "rb") as f:
                manifest = json.loads(f.read())
            manifest[change[0]] = change[1]
            path = self.path("manifest-%s.json" % change[0])
            with open(path, "w") as f:
                json.dump(manifest, f)
            self.refuses(text, mk.package_manifest, nbt, VERSION, path)
        path = self.path("dup.json")
        with open(path, "w") as f:
            f.write('{"id": "a", "id": "b"}')
        self.refuses("twice", mk.package_manifest, nbt, VERSION, path)

    def test_released_date(self):
        self.assertEqual(mk.released_date("2030-12-31T23:59:59Z"), "2030-12-31")
        self.refuses("publish time", mk.released_date, None)


class GitHubCalls(unittest.TestCase):
    """The real GitHub class over a replaced urlopen."""

    def setUp(self):
        self.calls = []
        self.answers = []

    def urlopen(self, request, timeout=None):
        self.calls.append(request)
        answer = self.answers.pop(0)
        if isinstance(answer, Exception):
            raise answer
        return _Response(answer)

    def gh(self, token="invented-token"):
        return mk.GitHub(REPO, token)

    def test_token_not_redirected(self):
        self.answers = [b"data"]
        with mock.patch("urllib.request.urlopen", self.urlopen):
            self.assertEqual(self.gh().download({"name": "a", "size": 4, "url": "https://api/a"}, 10), b"data")
        request = self.calls[0]
        self.assertEqual(request.unredirected_hdrs.get("Authorization"), "Bearer invented-token")
        self.assertNotIn("Authorization", request.headers)
        self.assertEqual(request.get_header("Accept"), "application/octet-stream")

    def test_download_limit(self):
        self.answers = [b"x" * 11]
        with mock.patch("urllib.request.urlopen", self.urlopen):
            with self.assertRaises(mk.Refused):
                self.gh().download({"name": "a", "size": 4, "url": "https://api/a"}, 10)
        with self.assertRaises(mk.Refused):
            self.gh().download({"name": "a", "size": 11, "url": "https://api/a"}, 10)

    def test_pages_and_annotated_tag(self):
        self.answers = [json.dumps([{"id": 1}] * 100).encode(), json.dumps([{"id": 2}]).encode(), b"[]",
                        json.dumps({"object": {"type": "tag", "sha": "t" * 40}}).encode(),
                        json.dumps({"object": {"type": "commit", "sha": COMMIT}}).encode()]
        with mock.patch("urllib.request.urlopen", self.urlopen):
            self.assertEqual(len(list(self.gh().releases())), 101)
            self.assertEqual(self.gh().tag_commit("a/b"), COMMIT)
        self.assertTrue(self.calls[3].full_url.endswith("/git/ref/tags/a/b"))

    def test_missing_release(self):
        import urllib.error
        self.answers = [urllib.error.HTTPError("u", 404, "nf", {}, None)]
        with mock.patch("urllib.request.urlopen", self.urlopen):
            self.assertIsNone(self.gh().release_by_tag("x"))

    def test_upload(self):
        self.answers = [b'{"name": "a"}']
        with mock.patch("urllib.request.urlopen", self.urlopen):
            self.gh().upload({"upload_url": "https://uploads/r/1/assets{?name,label}"}, "a b.nbtpkg", b"d",
                             "application/octet-stream")
        request = self.calls[0]
        self.assertEqual(request.full_url, "https://uploads/r/1/assets?name=a%20b.nbtpkg")
        self.assertEqual(request.get_method(), "POST")

    def test_error_names_no_token(self):
        import urllib.error
        self.answers = [urllib.error.HTTPError("u", 500, "err", {}, None)]
        with mock.patch("urllib.request.urlopen", self.urlopen):
            with self.assertRaises(mk.Refused) as caught:
                list(self.gh().releases())
        self.assertNotIn("invented-token", str(caught.exception))

    def test_from_env(self):
        gh = mk.GitHub.from_env({"GITHUB_REPOSITORY": REPO, "GITHUB_TOKEN": "t"})
        self.assertEqual((gh.repo, gh.token, gh.repo_url()), (REPO, "t", "https://github.com/" + REPO))
        with self.assertRaises(mk.Refused):
            mk.GitHub.from_env({})


class _Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


if __name__ == "__main__":
    unittest.main()

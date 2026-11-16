#!/usr/bin/env python3
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Turns a release of NBT-AAD into a signed .nbtpkg package for NBTApps, and attaches it to that release.

  make_nbtpkg.py fetch --tag TAG --work DIR [--nbtapps DIR]
  make_nbtpkg.py sign --work DIR [--nbtapps DIR]
  make_nbtpkg.py local --daemon FILE (--zip FILE | --plugin-dir DIR) [--key FILE] [--pub FILE] [--out DIR]
                       [--nbtapps DIR] [--repo OWNER/REPO --tag TAG --commit SHA [--released YYYY-MM-DD]
                       [--title TEXT]]

Nothing is built here. The package holds the files a release already has: `nbt-aad` and the five plugin files in
`Releases.zip`. The GitHub workflow .github/workflows/nbtpkg.yml runs `fetch` and then `sign`:

  fetch  1. downloads nbt-aad and Releases.zip from the release of TAG
         2. reads the five plugin files out of the zip by their exact names
         3. checks that both programs are ARM ELF files that find the package folders at run time
         4. reads the version X.Y.Z from NBT-AAD-VERSION=vX.Y.Z... in nbt-aad, and checks that no other release
            has the archive of that version
         5. stages the files with the manifest made from tools/nbtpkg/manifest.json into DIR
  sign   6. builds the archive, signs it with the author key and verifies it with tools/nbtpkg/author.pub
         7. attaches the archive to the release
         8. writes the record for nbtapps-packages to the job summary and attaches it as nbtpkg-record.json

`sign` takes the author key from the environment variable NBTPKG_AUTHOR_KEY: the 64 hex characters of the key's
private half. It is never written to disk or printed. Only the sign step of the workflow gets it.

Two cases end the run successfully with nothing packaged, with a notice and a line in the job summary:
  - there is no tools/nbtpkg/author.pub yet: the set-up is not finished. `fetch` decides this first, before any
    network call. Once author.pub is there, a missing or wrong NBTPKG_AUTHOR_KEY fails: that set-up is broken.
  - the release is marked pre-release, and PACKAGE_PRERELEASES (below) is False, as it is for now.
`fetch` then writes only the reason to DIR/state.json, and `sign` reads it and does nothing.

Run again on a release that already has the package, it builds the same bytes again (the build and Ed25519 are
both deterministic) and ends with nothing changed. A package of the same version with other bytes, or on another
release, is refused: a published version never changes, so the daemon's version must be raised.

`local` does the same with files on this computer and uploads nothing. Without --key the archive is not signed.
The record is written only when --repo, --tag and --commit are given.

The GitHub API is reached with the token in GH_TOKEN or GITHUB_TOKEN, for the repo in GITHUB_REPOSITORY.
--nbtapps is the headunit/ folder of a checkout of https://github.com/tom-pryor/nbtapps, whose tools/nbtpkg.py
builds and verifies the archive (the environment variable NBTAPPS_HEADUNIT works too). It needs python3 and, to
sign or verify, the `cryptography` module.
"""
import argparse
import datetime
import hashlib
import importlib.util
import io
import json
import os
import re
import struct
import sys
import tempfile
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
import zipfile
import zlib

PACKAGE_ID = "com.github.sq00ky.android-auto"
PACKAGE_NAME = "Android Auto"
DAEMON = "nbt-aad"
PLUGIN_ZIP = "Releases.zip"
ZIP_FOLDER = "Releases/"
PLUGIN_FILES = ("libNbtAad.so", "NbtAad.manifest", "NbtAad.xml", "NbtAad_ALL_Texts.zip", "NbtAad_ALL_Images.zip")
PLUGIN_LIBRARY = "libNbtAad.so"
RECORD_ASSET = "nbtpkg-record.json"
VERSION_PLACEHOLDER = "@VERSION@"
# For now a release marked pre-release is not packaged: the run ends with a notice and attaches nothing, so the
# version is not used up. Set this to True to package pre-releases as well.
PACKAGE_PRERELEASES = False
# A build that finds its folders at run time names them. One built before that change does not, and is refused.
FOLDER_STRINGS = (b"/var/opt/nbtapp/apps/" + PACKAGE_ID.encode("ascii"),
                  b"/var/opt/nbtapp/data/" + PACKAGE_ID.encode("ascii"))

MAX_DAEMON = 64 << 20
MAX_ZIP = 64 << 20
MAX_PLUGIN_FILE = 32 << 20
MAX_PLUGIN_TOTAL = 64 << 20
MAX_ARCHIVE = 256 << 20
MAX_JSON = 8 << 20
MAX_NOTE = 200

ELF_MAGIC = b"\x7fELF"
EM_ARM = 40

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(HERE))
TEMPLATE = os.path.join(HERE, "manifest.json")
AUTHOR_PUB = os.path.join(HERE, "author.pub")
LICENCE = os.path.join(REPO_ROOT, "LICENSE.md")
DEFAULT_NBTAPPS = os.path.join(os.path.dirname(REPO_ROOT), "nbtapps", "headunit")

_VERSION = re.compile(rb"NBT-AAD-VERSION=v?([0-9]+)\.([0-9]+)\.([0-9]+)(?![0-9])")
_VERSION_TEXT = re.compile(rb"NBT-AAD-VERSION=[!-~]{1,64}")
_NUMBER = re.compile(r"0|[1-9][0-9]{0,8}")
_HEX_KEY = re.compile(r"[0-9a-fA-F]{64}")
_COMMIT = re.compile(r"[0-9a-f]{40}")
_SHA256 = re.compile(r"[0-9a-f]{64}")
_DATE = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}")
_URL = re.compile(r"https://[^\s\x00-\x1f\x7f]+")
_REPO = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")


class Refused(Exception):
    """A step that cannot go on. The message is one line: the reason."""


def clean(text):
    """Text that may come from a release, safe to print on one line of a log."""
    return "".join(c if 0x20 <= ord(c) != 0x7f and not 0x80 <= ord(c) < 0xa0 else "\\x%02x" % ord(c)
                   for c in str(text))


def say(text):
    print("make_nbtpkg: " + clean(text), flush=True)


def archive_name(version):
    return "%s-%s.nbtpkg" % (PACKAGE_ID, version)


# ---- nbtpkg.py of the NBTApps repo -----------------------------------------------------------------------------

def load_nbtpkg(folder):
    """tools/nbtpkg.py of the NBTApps repo's headunit/ folder, loaded as a module."""
    path = os.path.join(folder, "tools", "nbtpkg.py")
    if not os.path.isfile(path):
        raise Refused("%s is not there: --nbtapps (or NBTAPPS_HEADUNIT) is the headunit/ folder of a checkout of "
                      "https://github.com/tom-pryor/nbtapps" % path)
    spec = importlib.util.spec_from_file_location("nbtpkg", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# ---- steps 2 to 4: what the release holds ----------------------------------------------------------------------

def read_plugin_zip(data):
    """Releases.zip -> {file name: bytes} of the five plugin files. Members are looked up by exact name and read
    into memory; nothing is extracted, and no path in the zip is used as a path."""
    if len(data) > MAX_ZIP:
        raise Refused("%s is %d bytes, more than the %d allowed" % (PLUGIN_ZIP, len(data), MAX_ZIP))
    try:
        archive = zipfile.ZipFile(io.BytesIO(data))
        infos = archive.infolist()
    except (zipfile.BadZipFile, zipfile.LargeZipFile, ValueError, EOFError, OSError) as e:
        raise Refused("%s is not a zip archive this reads: %s" % (PLUGIN_ZIP, e))
    wanted = {ZIP_FOLDER + name: name for name in PLUGIN_FILES}
    seen = set()
    for info in infos:
        if info.filename in seen:
            raise Refused("%s holds %s twice" % (PLUGIN_ZIP, clean(info.filename)))
        seen.add(info.filename)
        if info.filename == ZIP_FOLDER and info.file_size == 0:
            continue    # the folder's own entry, which some zip tools write
        if info.filename not in wanted:
            raise Refused("%s holds %s, which is not one of the five plugin files under %s"
                          % (PLUGIN_ZIP, clean(info.filename), ZIP_FOLDER))
    missing = [path for path in wanted if path not in seen]
    if missing:
        raise Refused("%s has no %s" % (PLUGIN_ZIP, ", ".join(missing)))

    files, total = {}, 0
    for info in infos:
        name = wanted.get(info.filename)
        if name is None:
            continue
        if info.flag_bits & 0x1:
            raise Refused("%s in %s is encrypted" % (info.filename, PLUGIN_ZIP))
        if info.file_size == 0:
            raise Refused("%s in %s is empty" % (info.filename, PLUGIN_ZIP))
        if info.file_size > MAX_PLUGIN_FILE:
            raise Refused("%s in %s says it is %d bytes, more than the %d allowed"
                          % (info.filename, PLUGIN_ZIP, info.file_size, MAX_PLUGIN_FILE))
        total += info.file_size
        if total > MAX_PLUGIN_TOTAL:
            raise Refused("the plugin files in %s add up to more than the %d bytes allowed"
                          % (PLUGIN_ZIP, MAX_PLUGIN_TOTAL))
        try:
            with archive.open(info) as member:
                content = member.read(info.file_size + 1)   # reaching the end also checks the CRC
        except (zipfile.BadZipFile, zlib.error, EOFError, NotImplementedError, RuntimeError, ValueError) as e:
            raise Refused("%s in %s cannot be read: %s" % (info.filename, PLUGIN_ZIP, e))
        if len(content) != info.file_size:
            raise Refused("%s in %s is not the size the zip says" % (info.filename, PLUGIN_ZIP))
        files[name] = content
    return files


def read_plugin_folder(folder, read=None):
    """The five plugin files from a folder on this computer (by-hand mode). Other files in it are not used."""
    read = read or read_local
    return {name: read(os.path.join(folder, name), MAX_PLUGIN_FILE, allow_empty=False) for name in PLUGIN_FILES}


def check_program(name, data):
    """Refuses a file that is not a 32-bit little-endian ARM ELF, or that does not name the package folders."""
    if len(data) < 52 or data[:4] != ELF_MAGIC:
        raise Refused("%s is not an ELF program" % name)
    machine = struct.unpack_from("<H", data, 18)[0]
    if data[4] != 1 or data[5] != 1 or machine != EM_ARM:
        raise Refused("%s is not a 32-bit little-endian ARM program (class %d, data %d, machine %d)"
                      % (name, data[4], data[5], machine))
    for folder in FOLDER_STRINGS:
        if folder not in data:
            raise Refused("%s does not contain %s: it was built before the programs found their folders at run "
                          "time, so it only works installed by hand. Package a release built after that change"
                          % (name, folder.decode("ascii")))


def check_programs(daemon, plugin):
    check_program(DAEMON, daemon)
    check_program(PLUGIN_LIBRARY, plugin[PLUGIN_LIBRARY])


def version_text(daemon):
    """The daemon's whole version string, such as NBT-AAD-VERSION=v1.9.76-navsettings, for the log."""
    found = _VERSION_TEXT.search(daemon)
    return found.group(0).decode("ascii") if found else None


def daemon_version(daemon):
    """X.Y.Z from NBT-AAD-VERSION=vX.Y.Z... in the daemon: the package's version."""
    found = {m.groups() for m in _VERSION.finditer(daemon)}
    if not found:
        raise Refused("%s has no NBT-AAD-VERSION=vX.Y.Z string, so its version is not known" % DAEMON)
    if len(found) > 1:
        raise Refused("%s has %d different NBT-AAD-VERSION strings, so its version is not clear"
                      % (DAEMON, len(found)))
    parts = [part.decode("ascii") for part in found.pop()]
    if not all(_NUMBER.fullmatch(part) for part in parts):
        raise Refused("%s says version %s, which is not a package version (no leading zeros, at most 9 digits)"
                      % (DAEMON, ".".join(parts)))
    return ".".join(parts)


# ---- step 5: the staged package --------------------------------------------------------------------------------

def package_manifest(nbt, version, template=TEMPLATE):
    """The manifest without `contents`: tools/nbtpkg/manifest.json with the version filled in."""
    try:
        with open(template, "rb") as f:
            manifest = nbt.parse_json(f.read())
    except OSError as e:
        raise Refused("%s cannot be read: %s" % (template, e.strerror))
    except ValueError as e:
        raise Refused("%s %s" % (template, e))
    if manifest.get("id") != PACKAGE_ID or manifest.get("name") != PACKAGE_NAME:
        raise Refused("%s must have the id %s and the name %s" % (template, PACKAGE_ID, PACKAGE_NAME))
    if manifest.get("version") != VERSION_PLACEHOLDER:
        raise Refused("%s must have the version %s, which is filled in here" % (template, VERSION_PLACEHOLDER))
    if "contents" in manifest:
        raise Refused("%s must not have contents: nbtpkg.py fills them in" % template)
    manifest["version"] = version
    return manifest


def write_file(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def stage(stage_dir, nbt, daemon, plugin, version, template=TEMPLATE, licence=LICENCE):
    """Writes the package's source folder as nbtpkg.py's build takes it."""
    if os.path.exists(stage_dir) and os.listdir(stage_dir):
        raise Refused("%s is not empty" % stage_dir)
    manifest = package_manifest(nbt, version, template)
    try:
        with open(licence, "rb") as f:
            licence_text = f.read()
    except OSError as e:
        raise Refused("%s cannot be read: %s" % (licence, e.strerror))
    write_file(os.path.join(stage_dir, "manifest.json"), (json.dumps(manifest, indent=2) + "\n").encode("utf-8"))
    write_file(os.path.join(stage_dir, "payload", DAEMON), daemon)
    for name in PLUGIN_FILES:
        write_file(os.path.join(stage_dir, "payload", "plugin", name), plugin[name])
    # The licence asks for its terms and notice to go with every copy.
    write_file(os.path.join(stage_dir, "LICENSE.md"), licence_text)
    return manifest


# ---- step 6: build, sign, verify -------------------------------------------------------------------------------

def parse_key_text(text, what):
    """A key as one line of 64 hex characters; lines starting with # are skipped, as in nbtpkg.py's key files.
    The message on a bad key never shows the key."""
    lines = [line.strip() for line in text.splitlines() if line.strip() and not line.strip().startswith("#")]
    if len(lines) != 1 or not _HEX_KEY.fullmatch(lines[0]):
        raise Refused("%s is not a key: it must be one line of 64 hex characters" % what)
    return bytes.fromhex(lines[0])


def key_from_env(environ=None):
    environ = os.environ if environ is None else environ
    text = environ.get("NBTPKG_AUTHOR_KEY", "")
    if not text.strip():
        raise Refused("NBTPKG_AUTHOR_KEY is not set: the `nbtpkg` environment of the repo needs it as a secret "
                      "(tools/nbtpkg/README.md)")
    return parse_key_text(text, "NBTPKG_AUTHOR_KEY")


def read_public_key(path):
    if not os.path.isfile(path):
        raise Refused("%s is not there: the author key has not been made yet (tools/nbtpkg/README.md says how), "
                      "so nothing can be signed" % path)
    try:
        with open(path, "r", encoding="ascii") as f:
            return parse_key_text(f.read(4096), path)
    except (OSError, UnicodeDecodeError):
        raise Refused("%s cannot be read as a key file" % path)


def build_and_verify(nbt, stage_dir, version, seed, public, unsigned_ok=False):
    """Builds the archive from the staged folder, signed with `seed` when given, and checks it as the head unit's
    agent does: with `public` as the only trusted key, or unsigned when there is no key."""
    try:
        _manifest, data = nbt.build_package(stage_dir, seed)
    except nbt.PkgError as e:
        raise Refused("the package does not build: %s: %s" % (e.code, e))
    try:
        if public is None:
            info = nbt.verify_package(data, keys=None, allow_unsigned=unsigned_ok)
        else:
            info = nbt.verify_package(data, keys=[public])
    except nbt.PkgError as e:
        if e.code == "bad-signature" and public is not None:
            raise Refused("the archive is not signed by the key in the author public key file: the private key "
                          "given is another key (%s)" % e)
        raise Refused("the built archive does not verify: %s: %s" % (e.code, e))
    if info["id"] != PACKAGE_ID or info["version"] != version:
        raise Refused("the built archive is %s %s, not %s %s" % (info["id"], info["version"], PACKAGE_ID, version))
    return data


# ---- step 8: the record ----------------------------------------------------------------------------------------

def clean_note(title):
    """The release title as one line of `notes`, or None when nothing usable is left."""
    if not isinstance(title, str):
        return None
    kept = []
    for c in title:
        kind = unicodedata.category(c)
        if kind == "Cs":
            continue
        kept.append(" " if kind in ("Cc", "Zl", "Zp") else c)
    text = " ".join("".join(kept).split())
    if len(text) > MAX_NOTE:
        text = text[:MAX_NOTE - 1].rstrip() + "…"
    if not re.search(r"[^\s﻿]", text):
        return None
    return text


def make_record(version, url, sha256, released, title, repo_url, commit):
    record = {"url": url, "sha256": sha256, "released": released}
    note = clean_note(title)
    if note:
        record["notes"] = [note]
    record["source"] = {"repo": repo_url, "commit": commit}
    document = {"id": PACKAGE_ID, "version": version, "record": record}
    check_record(document)
    return document


def check_record(document):
    """The rules of nbtapps-packages' schema/version.schema.json, so a record that it would refuse is never
    published here."""
    record = document["record"]
    problems = []
    if set(record) - {"url", "sha256", "released", "notes", "source"}:
        problems.append("unknown keys")
    if not _URL.fullmatch(record["url"]) or not record["url"].endswith("/" + archive_name(document["version"])):
        problems.append("url %r" % record["url"])
    if not _SHA256.fullmatch(record["sha256"]):
        problems.append("sha256")
    if not _DATE.fullmatch(record["released"]):
        problems.append("released %r" % record["released"])
    for note in record.get("notes", []):
        if (not 1 <= len(note) <= MAX_NOTE or re.search(r"[\x00-\x1f\x7f]", note)
                or not re.search(r"[^\s﻿]", note)):
            problems.append("a note")
    source = record["source"]
    if set(source) != {"repo", "commit"} or not _URL.fullmatch(source["repo"]) \
            or not _COMMIT.fullmatch(source["commit"]):
        problems.append("source %r" % source)
    if problems:
        raise Refused("the record would be refused by nbtapps-packages: %s" % ", ".join(problems))


def record_bytes(document):
    return (json.dumps(document, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def summary_text(document, archive, release_url=None, prerelease=False):
    text = record_bytes(document).decode("utf-8")
    fence = "```"
    while fence in text:
        fence += "`"
    lines = ["## NBTApps package", "",
             "`%s`, version %s, SHA-256 `%s`." % (archive, document["version"], document["record"]["sha256"]), ""]
    if prerelease:
        lines += ["This release is a pre-release: the record is not proposed to nbtapps-packages.", ""]
    lines += ["The record (`%s`, attached to the release):" % RECORD_ASSET, "", fence + "json", text.rstrip("\n"),
              fence, ""]
    return "\n".join(lines)


def write_summary(text, environ=None):
    environ = os.environ if environ is None else environ
    path = environ.get("GITHUB_STEP_SUMMARY")
    if path:
        with open(path, "a", encoding="utf-8") as f:
            f.write(text + "\n")


def released_date(published_at):
    """The UTC date of an ISO time as GitHub gives it, such as 2026-01-02T03:04:05Z."""
    try:
        moment = datetime.datetime.strptime(published_at, "%Y-%m-%dT%H:%M:%SZ")
    except (TypeError, ValueError):
        raise Refused("the release has no publish time GitHub gave in the usual form: %r" % (published_at,))
    return moment.strftime("%Y-%m-%d")


# ---- the GitHub API --------------------------------------------------------------------------------------------

class GitHub:
    """The few calls this needs. Every network effect goes through here, so the tests replace this class."""

    def __init__(self, repo, token=None, api="https://api.github.com", server="https://github.com"):
        self.repo, self.token, self.api, self.server = repo, token, api.rstrip("/"), server.rstrip("/")

    @classmethod
    def from_env(cls, environ=None):
        environ = os.environ if environ is None else environ
        repo = environ.get("GITHUB_REPOSITORY", "")
        if not _REPO.fullmatch(repo):
            raise Refused("GITHUB_REPOSITORY is not set to OWNER/REPO")
        return cls(repo, environ.get("GH_TOKEN") or environ.get("GITHUB_TOKEN") or None,
                   environ.get("GITHUB_API_URL") or "https://api.github.com",
                   environ.get("GITHUB_SERVER_URL") or "https://github.com")

    def repo_url(self):
        return "%s/%s" % (self.server, self.repo)

    def _call(self, method, url, data=None, accept="application/vnd.github+json", content_type=None,
              limit=MAX_JSON, missing_ok=False):
        request = urllib.request.Request(url, data=data, method=method)
        request.add_header("Accept", accept)
        request.add_header("User-Agent", "nbt-aad-make-nbtpkg")
        request.add_header("X-GitHub-Api-Version", "2022-11-28")
        if content_type:
            request.add_header("Content-Type", content_type)
        if self.token:
            # Not carried over a redirect: an asset download is redirected to another host.
            request.add_unredirected_header("Authorization", "Bearer " + self.token)
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                body = response.read(limit + 1)
        except urllib.error.HTTPError as e:
            e.close()
            if missing_ok and e.code == 404:
                return None
            raise Refused("GitHub answered %d to %s %s" % (e.code, method, url.split("?")[0]))
        except (urllib.error.URLError, OSError) as e:
            raise Refused("GitHub could not be reached for %s %s: %s" % (method, url.split("?")[0], e))
        if len(body) > limit:
            raise Refused("the answer to %s %s is larger than %d bytes" % (method, url.split("?")[0], limit))
        return body

    def _json(self, method, url, **kwargs):
        body = self._call(method, url, **kwargs)
        if body is None:
            return None
        try:
            return json.loads(body.decode("utf-8"))
        except ValueError:
            raise Refused("GitHub's answer to %s %s is not JSON" % (method, url.split("?")[0]))

    def release_by_tag(self, tag):
        return self._json("GET", "%s/repos/%s/releases/tags/%s" % (self.api, self.repo,
                                                                  urllib.parse.quote(tag, safe="")),
                          missing_ok=True)

    def releases(self):
        page = 1
        while True:
            batch = self._json("GET", "%s/repos/%s/releases?per_page=100&page=%d" % (self.api, self.repo, page))
            if not batch:
                return
            for release in batch:
                yield release
            page += 1

    def download(self, asset, limit):
        if asset.get("size", 0) > limit:
            raise Refused("%s on the release is %d bytes, more than the %d allowed"
                          % (asset["name"], asset["size"], limit))
        return self._call("GET", asset["url"], accept="application/octet-stream", limit=limit)

    def upload(self, release, name, data, content_type):
        base = release["upload_url"].split("{", 1)[0]
        return self._json("POST", "%s?name=%s" % (base, urllib.parse.quote(name, safe="")), data=data,
                          content_type=content_type)

    def tag_commit(self, tag):
        """The commit a tag points at, through an annotated tag when it is one."""
        ref = self._json("GET", "%s/repos/%s/git/ref/tags/%s" % (self.api, self.repo,
                                                                urllib.parse.quote(tag, safe="/")),
                         missing_ok=True)
        if not ref or "object" not in ref:
            raise Refused("the tag %s is not in the repo" % tag)
        target = ref["object"]
        for _ in range(4):
            if target.get("type") == "commit":
                return target["sha"]
            if target.get("type") != "tag":
                break
            target = self._json("GET", "%s/repos/%s/git/tags/%s" % (self.api, self.repo, target["sha"]))["object"]
        raise Refused("the tag %s does not point at a commit" % tag)


def find_published(gh, release, archive):
    """The asset named `archive` on `release`, or None. The same name on any other release is refused."""
    found = None
    for other in gh.releases():
        for asset in other.get("assets", []):
            if asset.get("name") != archive:
                continue
            if other.get("id") != release["id"]:
                raise Refused("%s is already on the release %s: a published version never changes, so raise the "
                              "daemon's version (NBT_AAD_BUILD in milestone-7/src/main.cpp) and release again"
                              % (archive, other.get("tag_name")))
            found = asset
    return found


def asset_named(release, name):
    for asset in release.get("assets", []):
        if asset.get("name") == name:
            return asset
    return None


# ---- skipping: set-up not finished, or a pre-release -----------------------------------------------------------

def notice(text, environ=None):
    """A notice in the log, as an annotation on GitHub, and a line in the job summary."""
    environ = os.environ if environ is None else environ
    say(text)
    if environ.get("GITHUB_ACTIONS") == "true":
        print("::notice::" + clean(text).replace("%", "%25"), flush=True)
    write_summary("## NBTApps package\n\nNot packaged: %s.\n" % text, environ)


def skip(work, reason, environ=None):
    """Ends the fetch step with nothing staged. The sign step then reads the reason and does nothing."""
    notice(reason, environ)
    write_file(os.path.join(work, "state.json"), (json.dumps({"skip": reason}, indent=2) + "\n").encode("utf-8"))
    return {"skip": reason}


def prerelease_skip(release):
    """Why this release is not packaged, or None. PACKAGE_PRERELEASES is the switch."""
    if release.get("prerelease") and not PACKAGE_PRERELEASES:
        return ("the release %s is a pre-release, and pre-releases are not packaged for now, so nothing is "
                "attached (tools/nbtpkg/README.md)" % release.get("tag_name"))
    return None


def check_empty(work):
    if os.path.exists(work) and os.listdir(work):
        raise Refused("%s is not empty" % work)


def fetch_step(nbt, tag, work, author_pub=None, environ=None, github=None):
    """The workflow's first step. Without tools/nbtpkg/author.pub the set-up is not finished: that is decided here,
    before anything goes over the network, and the run ends without failing."""
    check_empty(work)
    author_pub = author_pub or AUTHOR_PUB
    if not os.path.exists(author_pub):
        return skip(work, "the set-up is not finished: there is no tools/nbtpkg/author.pub yet, so nothing is "
                          "signed or attached (tools/nbtpkg/README.md says how to finish it)", environ)
    read_public_key(author_pub)     # there but broken is a broken set-up, and fails
    return run_fetch(github or GitHub.from_env(environ), nbt, tag, work, environ)


def read_state(work):
    try:
        with open(os.path.join(work, "state.json"), "rb") as f:
            return json.loads(f.read().decode("utf-8"))
    except (OSError, ValueError):
        raise Refused("%s has no state.json from the fetch step" % work)


def sign_step(nbt, work, author_pub=None, environ=None, github=None):
    """The workflow's second step. Does nothing when the fetch step skipped. Otherwise author.pub and the key
    must both be there and agree, or it fails."""
    state = read_state(work)
    if state.get("skip"):
        say("nothing to sign: %s" % state["skip"])
        return None
    public = read_public_key(author_pub or AUTHOR_PUB)
    seed = key_from_env(environ)
    return run_sign(github or GitHub.from_env(environ), nbt, work, seed, public, environ)


# ---- the steps -------------------------------------------------------------------------------------------------

def run_fetch(gh, nbt, tag, work, environ=None):
    """Steps 1 to 5. Writes the staged package to WORK/stage and what `sign` needs to WORK/state.json."""
    check_empty(work)
    release = gh.release_by_tag(tag)
    if not release:
        raise Refused("the repo %s has no published release with the tag %s" % (gh.repo, tag))
    say("release %s (%s)%s" % (tag, release.get("name") or "no title",
                               ", a pre-release" if release.get("prerelease") else ""))
    reason = prerelease_skip(release)
    if reason:
        return skip(work, reason, environ)

    # 1. the two files
    for name in (DAEMON, PLUGIN_ZIP):
        if asset_named(release, name) is None:
            raise Refused("the release %s has no %s: attach it, then run this again for the tag" % (tag, name))
    daemon = gh.download(asset_named(release, DAEMON), MAX_DAEMON)
    plugin_zip = gh.download(asset_named(release, PLUGIN_ZIP), MAX_ZIP)
    version, plugin = check_files(daemon, plugin_zip=plugin_zip)

    # 4. a published version never changes
    archive = archive_name(version)
    existing = find_published(gh, release, archive)
    if existing:
        say("%s is on this release already; sign checks that it is the same" % archive)

    # 5. stage
    stage(os.path.join(work, "stage"), nbt, daemon, plugin, version)
    state = {"tag": tag, "release": release["id"], "version": version, "archive": archive,
             "commit": gh.tag_commit(tag), "released": released_date(release.get("published_at")),
             "title": release.get("name") or "", "prerelease": bool(release.get("prerelease"))}
    write_file(os.path.join(work, "state.json"), (json.dumps(state, indent=2) + "\n").encode("utf-8"))
    say("staged %s %s from %s, commit %s" % (PACKAGE_ID, version, tag, state["commit"]))
    return state


def check_files(daemon, plugin_zip=None, plugin=None):
    """Steps 2 to 4 up to the version: the plugin files, both programs, and the version. Returns the version and
    the plugin files."""
    if len(daemon) > MAX_DAEMON:
        raise Refused("%s is %d bytes, more than the %d allowed" % (DAEMON, len(daemon), MAX_DAEMON))
    if plugin is None:
        plugin = read_plugin_zip(plugin_zip)
    say("plugin files: " + ", ".join("%s (%d bytes)" % (name, len(plugin[name])) for name in PLUGIN_FILES))
    say("%s: %d bytes, %s" % (DAEMON, len(daemon), version_text(daemon) or "no version string"))
    check_programs(daemon, plugin)
    say("both programs are ARM and find the package folders at run time")
    version = daemon_version(daemon)
    say("package version %s" % version)
    return version, plugin


def run_sign(gh, nbt, work, seed, public, environ=None):
    """Steps 6 to 8. Returns the record, or None when the release has become a pre-release meanwhile."""
    state = read_state(work)
    if state.get("skip"):
        raise Refused("the fetch step packaged nothing: %s" % state["skip"])
    version, archive, tag = state["version"], state["archive"], state["tag"]

    release = gh.release_by_tag(tag)
    if not release or release["id"] != state["release"]:
        raise Refused("the release of the tag %s is not the one the fetch step read" % tag)
    reason = prerelease_skip(release)
    if reason:
        notice(reason, environ)
        return None

    # 6. build, sign, verify
    data = build_and_verify(nbt, os.path.join(work, "stage"), version, seed, public)
    digest = hashlib.sha256(data).hexdigest()
    say("built %s: %d bytes, SHA-256 %s, signed and verified" % (archive, len(data), digest))

    # 7. attach, never over an asset that is there
    asset = find_published(gh, release, archive)
    if asset is not None:
        if hashlib.sha256(gh.download(asset, MAX_ARCHIVE)).hexdigest() != digest:
            raise Refused("%s is on this release already with other bytes: a published version never changes, so "
                          "raise the daemon's version (NBT_AAD_BUILD in milestone-7/src/main.cpp) and release "
                          "again" % archive)
        say("%s is on the release already, the same bytes: nothing to attach" % archive)
    else:
        asset = gh.upload(release, archive, data, "application/octet-stream")
        say("attached %s" % archive)

    # 8. the record
    url = asset.get("browser_download_url") or "%s/releases/download/%s/%s" % (
        gh.repo_url(), urllib.parse.quote(tag, safe=""), archive)
    document = make_record(version, url, digest, state["released"], state["title"], gh.repo_url(), state["commit"])
    body = record_bytes(document)
    write_summary(summary_text(document, archive, prerelease=state.get("prerelease")), environ)
    existing = asset_named(release, RECORD_ASSET)
    if existing is not None:
        if gh.download(existing, MAX_JSON) != body:
            raise Refused("%s on the release differs from the record made now: it is never overwritten. Delete it "
                          "from the release by hand if it is wrong, then run this again" % RECORD_ASSET)
        say("%s is on the release already, the same" % RECORD_ASSET)
    else:
        gh.upload(release, RECORD_ASSET, body, "application/json")
        say("attached %s" % RECORD_ASSET)
    sys.stdout.write(body.decode("utf-8"))
    return document


def read_local(path, limit, allow_empty=True):
    try:
        size = os.path.getsize(path)
        if size > limit:
            raise Refused("%s is %d bytes, more than the %d allowed" % (path, size, limit))
        if size == 0 and not allow_empty:
            raise Refused("%s is empty" % path)
        with open(path, "rb") as f:
            return f.read(limit + 1)
    except OSError as e:
        raise Refused("%s cannot be read: %s" % (path, e.strerror))


def write_new(path, data):
    """Writes a file that is not there; the same bytes there already are fine, other bytes are refused."""
    if os.path.exists(path):
        with open(path, "rb") as f:
            if f.read() == data:
                return
        raise Refused("%s is there already with other bytes; it is not overwritten" % path)
    write_file(path, data)


def run_local(nbt, args):
    """By hand: files on this computer, nothing uploaded."""
    daemon = read_local(args.daemon, MAX_DAEMON)
    if args.zip:
        version, plugin = check_files(daemon, plugin_zip=read_local(args.zip, MAX_ZIP))
    else:
        version, plugin = check_files(daemon, plugin=read_plugin_folder(args.plugin_dir))

    seed = public = None
    if args.key:
        try:
            with open(args.key, "r", encoding="ascii") as f:
                seed = parse_key_text(f.read(4096), args.key)
        except (OSError, UnicodeDecodeError):
            raise Refused("%s cannot be read as a key file" % args.key)
        pub_path = args.pub or AUTHOR_PUB
        if args.pub or os.path.isfile(pub_path):
            public = read_public_key(pub_path)
        else:
            public = nbt.public_key(seed)
            say("%s is not there: the archive is checked against the given key's own public half" % pub_path)

    archive = archive_name(version)
    with tempfile.TemporaryDirectory() as work:
        stage(os.path.join(work, "stage"), nbt, daemon, plugin, version)
        data = build_and_verify(nbt, os.path.join(work, "stage"), version, seed, public, unsigned_ok=True)
    digest = hashlib.sha256(data).hexdigest()
    os.makedirs(args.out, exist_ok=True)
    write_new(os.path.join(args.out, archive), data)
    say("wrote %s: %d bytes, SHA-256 %s, %s" % (os.path.join(args.out, archive), len(data), digest,
                                                "signed" if seed else "NOT signed"))

    if not (args.repo and args.tag and args.commit):
        say("no record written: give --repo, --tag and --commit to write one")
        return None
    if not _REPO.fullmatch(args.repo):
        raise Refused("--repo is OWNER/REPO")
    repo_url = "https://github.com/" + args.repo
    url = "%s/releases/download/%s/%s" % (repo_url, urllib.parse.quote(args.tag, safe=""), archive)
    released = args.released or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
    document = make_record(version, url, digest, released, args.title, repo_url, args.commit)
    write_new(os.path.join(args.out, RECORD_ASSET), record_bytes(document))
    say("wrote %s" % os.path.join(args.out, RECORD_ASSET))
    return document


# ---- command line ----------------------------------------------------------------------------------------------

def parse_args(argv=None):
    parser = argparse.ArgumentParser(prog="make_nbtpkg.py", description=__doc__.split("\n")[0])
    parser.add_argument("--nbtapps", default=os.environ.get("NBTAPPS_HEADUNIT") or DEFAULT_NBTAPPS,
                        help="the headunit/ folder of a checkout of the NBTApps repo")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("fetch", help="steps 1 to 5: fetch a release's files, check them and stage the package")
    p.add_argument("--tag", required=True)
    p.add_argument("--work", required=True, help="an empty or new folder for the staged package")

    p = sub.add_parser("sign", help="steps 6 to 8: build, sign, verify, attach, record")
    p.add_argument("--work", required=True, help="the folder fetch wrote")

    p = sub.add_parser("local", help="by hand, with files on this computer; uploads nothing")
    p.add_argument("--daemon", required=True, help="nbt-aad")
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument("--zip", help="Releases.zip")
    group.add_argument("--plugin-dir", help="a folder with the five plugin files")
    p.add_argument("--key", help="private key file; without it the archive is not signed")
    p.add_argument("--pub", help="public key to verify with (default tools/nbtpkg/author.pub)")
    p.add_argument("--out", default=".", help="folder for the archive and the record")
    p.add_argument("--repo", help="OWNER/REPO, for the record")
    p.add_argument("--tag", help="the release's tag, for the record")
    p.add_argument("--commit", help="the 40 hex characters of the tag's commit, for the record")
    p.add_argument("--released", help="YYYY-MM-DD, for the record (default today, UTC)")
    p.add_argument("--title", help="the release title, for the record's notes")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        nbt = load_nbtpkg(args.nbtapps)
        if args.command == "fetch":
            fetch_step(nbt, args.tag, args.work)
        elif args.command == "sign":
            sign_step(nbt, args.work)
        else:
            run_local(nbt, args)
    except Refused as e:
        reason = clean(e)
        if os.environ.get("GITHUB_ACTIONS") == "true":
            print("::error::" + reason.replace("%", "%25"), flush=True)
        sys.exit("make_nbtpkg: refused: " + reason)
    return 0


if __name__ == "__main__":
    main()

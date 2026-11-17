<!-- SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 -->
<!-- Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto) -->
# NBTApps package

[NBTApps](https://github.com/tom-pryor/nbtapps) installs apps on an NBT Evo head unit from a phone. It installs
signed `.nbtpkg` archives that its catalogue lists. Outside developers add their packages to the catalogue
through [nbtapps-packages](https://github.com/tom-pryor/nbtapps-packages).

This folder makes that package for Android Auto. It does not build anything. It takes the two files a release
already has, `nbt-aad` and `Releases.zip`, checks them, and makes `com.github.sq00ky.android-auto-<version>.nbtpkg`
from them, signed with our own author key. The archive and a small record of it, `nbtpkg-record.json`, are then
attached to the same release.

The files:

- `make_nbtpkg.py`: does the work. The top of the file says what each step checks.
- `manifest.json`: what the package installs. The version is filled in from `nbt-aad`. Change what installs here.
- `test_make_nbtpkg.py`: tests, with invented files only.
- `author.pub`: the public half of the author key. It is not here yet (see below).
- `../../.github/workflows/nbtpkg.yml`: runs it when a release is published.

The package holds the normal release files. That only works with a build that finds its folders at run time, so
an older release is refused.

## One-time setup

1. Get a checkout of [NBTApps](https://github.com/tom-pryor/nbtapps) and make a key with its `nbtpkg.py`:
   ```
   python3 nbtapps/headunit/tools/nbtpkg.py keygen nbt-aad
   ```
   This writes `nbt-aad.key` (private) and `nbt-aad.pub` (public). It needs the python module `cryptography`.
2. Keep `nbt-aad.key` safe and out of every repo. Keep a backup offline. Anyone who has it can sign packages as us.
   If it is lost, a new key means a change of owner in nbtapps-packages.
3. In this repo on GitHub, go to Settings, Environments, and make an environment called `nbtpkg`. Add a secret to
   it called `NBTPKG_AUTHOR_KEY`, holding the line of 64 hex characters from `nbt-aad.key` (not the `#` line).
   You can also add yourself as a required reviewer there, so each run waits for you.
4. Copy `nbt-aad.pub` to `tools/nbtpkg/author.pub` and commit it. Do this after step 3: once `author.pub` is
   there, a run without the right key fails.
5. Make the first release (see below), so there is an archive and a record.
6. Open the first pull request to [nbtapps-packages](https://github.com/tom-pryor/nbtapps-packages) by hand. It
   adds `packages/com.github.sq00ky.android-auto/` with:
   - `listing.json`: summary, author, category, description and homepage, as that repo's README says
   - `author.pub`: the same file as `tools/nbtpkg/author.pub`
   - `versions/<version>.json`: the `record` part of `nbtpkg-record.json` from the release

Until `author.pub` is committed, the set-up is not finished. Each run then ends well and packages nothing: it
shows a notice and a line in the run's summary that say so. It decides this first, before it fetches anything.

Once `author.pub` is there, the set-up has to work. A missing secret, or a secret that is not the key of
`author.pub`, makes the run fail.

## At each release

1. Raise `NBT_AAD_BUILD` in `milestone-7/src/main.cpp` before building. The package version is the `X.Y.Z` part
   of it (`v1.9.77-something` gives `1.9.77`). A version that has been packaged once never changes, so the same
   number is refused.
2. Build and create the release as now, with `nbt-aad` and `Releases.zip` attached. `Releases.zip` holds exactly
   the five plugin files under `Releases/`.
3. Mark a test build as **pre-release**. A pre-release is not packaged, for now: the run ends well with a notice,
   attaches nothing, and the version number is not used up. (`PACKAGE_PRERELEASES` at the top of
   `make_nbtpkg.py` turns this off.)
4. When the release is published, the workflow `nbtpkg` runs. It attaches
   `com.github.sq00ky.android-auto-<version>.nbtpkg` and `nbtpkg-record.json`, and shows the record in the run's
   summary. The bot in nbtapps-packages reads `nbtpkg-record.json` from the release to propose the new version.

A pre-release that is later changed to a full release is not picked up by itself: GitHub does not publish it
again. Run the workflow by hand for its tag (see below).

The workflow stops and says why when:

- the release has no `nbt-aad` or no `Releases.zip`
- `Releases.zip` is missing one of the five plugin files, has any other file, or a file is empty or far too big
- `nbt-aad` or `libNbtAad.so` is not an ARM program, or was built before the programs found their folders at run
  time
- `nbt-aad` has no `NBT-AAD-VERSION=v...` string
- the archive of that version is on another release, or on this one with other bytes
- `author.pub` is there but cannot be read, or the secret is missing or not the key of `author.pub`

## Running it again

If the files were attached after the release was published, the first run fails because they were not there yet.
Run it again from the Actions tab: workflow `nbtpkg`, Run workflow, and give the release's tag. Do the same for a
pre-release that has been changed to a full release. A run by tag skips a pre-release in the same way.

Running it again on a release that already has its package is fine. It builds the same bytes again, sees they are
already there, and changes nothing.

## By hand

The same checks and the same archive, on this computer. Nothing is uploaded.

```
python3 tools/nbtpkg/make_nbtpkg.py --nbtapps ../nbtapps/headunit local \
    --daemon nbt-aad --zip Releases.zip --out out/
```

- `--plugin-dir DIR` instead of `--zip` takes the five files from a folder.
- Without `--key FILE` the archive is not signed. That is fine for a test, not for publishing.
- With `--key`, the archive is checked against `tools/nbtpkg/author.pub`, or `--pub FILE`.
- `--repo OWNER/REPO --tag TAG --commit SHA` also writes `nbtpkg-record.json`. `--released` and `--title` are
  optional.

To check an archive:

```
python3 ../nbtapps/headunit/tools/nbtpkg.py verify out/com.github.sq00ky.android-auto-1.9.77.nbtpkg \
    --key tools/nbtpkg/author.pub
```

To run the tests:

```
NBTAPPS_HEADUNIT=../nbtapps/headunit python3 -m unittest discover -s tools/nbtpkg -p 'test_*.py'
```

`nbtpkg.py` is not copied into this repo: it is under another licence. The workflow checks it out from the
NBTApps repo at a fixed commit.

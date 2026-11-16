"""Run the actual C++ supervisor against isolated commands and temporary files."""

import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest


class NativeSupervisorTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory(prefix="cluster native supervisor ")
        self.root = Path(self.temporary_directory.name)
        (self.root / "bin").mkdir()
        (self.root / "flags").mkdir()
        (self.root / "screen/1").mkdir(parents=True)
        for display_id in (1, 2):
            (self.root / f"screen/1/dpy-{display_id}").write_text(
                "window manager = 42\nscene = (empty)\nwindows = (none)\n"
            )
        (self.root / "version").write_text("A different NBT firmware version\n")
        self.write_executable(
            "bin/ifconfig",
            """#!/bin/sh
echo "$*" >> "$TEST_ROOT/actions"
case "$*" in
  *-alias) test ! -f "$TEST_ROOT/fail_remove" || exit 1; rm -f "$TEST_ROOT/alias";;
  *netmask*) test ! -f "$TEST_ROOT/fail_add" || exit 1; : > "$TEST_ROOT/alias";;
  *) echo ' inet 160.48.199.119 netmask ffffff00'
     if test -f "$TEST_ROOT/alias"; then echo ' inet 160.48.199.120 netmask ffffffff'; fi;;
esac
""",
        )
        self.write_executable("bin/arp", '#!/bin/sh\necho "no entry"\nexit 1\n')
        self.write_player('trap "exit 0" TERM INT HUP\nwhile :; do sleep 0.05; done\n')
        self.process = None
        self.environment = dict(os.environ, TEST_ROOT=str(self.root))

    def write_executable(self, name, text):
        executable = self.root / name
        executable.write_text(text)
        executable.chmod(0o700)

    def write_player(self, body):
        self.write_executable(
            "kombi-player",
            """#!/bin/sh
if test "$1" = --inspect; then
  test ! -f "$TEST_ROOT/fail_inspect" || exit 1
  echo SCREEN_INSPECT_OK secondary=800x480
  exit 0
fi
test "$1" = --service || exit 2
echo $$ > "$TEST_ROOT/player.pid"
"""
            + body,
        )

    def start_supervisor(self, owner=None):
        self.process = subprocess.Popen(
            [
                os.environ["CLUSTER_TEST_SUPERVISOR_BIN"],
                str(self.root),
                str(owner if owner is not None else os.getpid()),
            ],
            env=self.environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )

    def wait_for_player(self):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if (self.root / "player.pid").exists():
                self.assertTrue((self.root / "alias").exists())
                self.assertFalse(list(self.root.rglob("player.log")))
                return
            if self.process.poll() is not None:
                self.fail(self.process.communicate()[0])
            time.sleep(0.02)
        self.fail("Player did not start")

    def expect_cleaned(self, expected=0):
        text = self.process.communicate(timeout=6)[0]
        self.assertEqual(self.process.returncode, expected, text)
        self.assertIn("AA_CLUSTER_SERVICE_CLEANUP alias_removed=1", text)
        self.assertFalse((self.root / "alias").exists())
        self.assertFalse(list(self.root.glob("lease*")))
        return text

    def tearDown(self):
        if self.process and self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            self.process.communicate(timeout=6)
        self.temporary_directory.cleanup()

    def test_other_firmware_allowed_and_normal_stop_cleans(self):
        self.start_supervisor()
        self.wait_for_player()
        self.process.terminate()
        self.assertIn("A different NBT firmware version", self.expect_cleaned())

    def test_owner_death_stops_player_and_cleans(self):
        owner = subprocess.Popen(["sleep", "20"])
        try:
            self.start_supervisor(owner.pid)
            self.wait_for_player()
            owner.terminate()
            owner.wait()
            self.expect_cleaned()
        finally:
            if owner.poll() is None:
                owner.terminate()
                owner.wait()

    def test_stuck_player_is_reaped_before_address_removal(self):
        self.write_player("trap '' TERM INT HUP\nwhile :; do sleep 0.05; done\n")
        self.start_supervisor()
        self.wait_for_player()
        self.process.terminate()
        self.expect_cleaned(128 + signal.SIGKILL)
        pid = int((self.root / "player.pid").read_text())
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)

    def test_lost_log_reader_does_not_prevent_cleanup(self):
        owner = subprocess.Popen(["sleep", "20"])
        try:
            self.start_supervisor(owner.pid)
            self.wait_for_player()
            self.process.stdout.close()
            owner.terminate()
            owner.wait()
            self.assertEqual(self.process.wait(timeout=6), 0)
            self.assertFalse((self.root / "alias").exists())
            self.assertFalse(list(self.root.glob("lease*")))
        finally:
            if owner.poll() is None:
                owner.terminate()
                owner.wait()

    def test_player_failure_diagnostics_survive_cleanup(self):
        self.write_player("echo PLAYER_DIAGNOSTIC\nexit 17\n")
        self.start_supervisor()
        self.assertIn("PLAYER_DIAGNOSTIC", self.expect_cleaned(17))

    def test_existing_alias_untouched(self):
        (self.root / "alias").touch()
        self.start_supervisor()
        self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 92)
        self.assertTrue((self.root / "alias").exists())
        self.assertNotIn("-alias", (self.root / "actions").read_text())
        self.assertFalse(list(self.root.glob("lease*")))

    def test_known_neighbor_untouched(self):
        self.write_executable("bin/arp", '#!/bin/sh\necho "160.48.199.120 at 00:11:22:33:44:55"\n')
        self.start_supervisor()
        self.expect_cleaned(93)
        self.assertNotIn("netmask", (self.root / "actions").read_text())

    def test_missing_manager_refused_before_inspection(self):
        (self.root / "screen/1/dpy-2").write_text("window manager = 0\n")
        self.start_supervisor()
        self.expect_cleaned(98)
        self.assertFalse((self.root / "player.pid").exists())

    def test_unknown_window_refused(self):
        (self.root / "screen/1/dpy-2").write_text(
            "window manager = 42\nscene = win-88\nwindows = win-88\n"
        )
        self.start_supervisor()
        self.expect_cleaned(97)

    def test_factory_window_retained(self):
        (self.root / "screen/1/dpy-2").write_text(
            "window manager = 42\nscene = win-88\nwindows = win-88\n"
        )
        window_directory = self.root / "screen/1/win-88"
        window_directory.mkdir()
        state = (
            "id string = 1056\n"
            "display id = 2\n"
            "buffer size = 800x480\n"
            "format = SCREEN_FORMAT_RGBA8888\n"
            "destination rectangle = (0,0 800x480)\n"
        )
        (window_directory / "win-88").write_text(state)
        self.start_supervisor()
        self.wait_for_player()
        self.process.terminate()
        self.assertIn("FACTORY_WINDOW_RETAINED", self.expect_cleaned())
        self.assertEqual((window_directory / "win-88").read_text(), state)

    def test_bad_geometry_refused_before_alias(self):
        (self.root / "fail_inspect").touch()
        self.start_supervisor()
        self.expect_cleaned(94)
        self.assertNotIn("netmask", (self.root / "actions").read_text())

    def test_address_creation_failure_releases_lock(self):
        (self.root / "fail_add").touch()
        self.start_supervisor()
        self.expect_cleaned(95)
        self.assertFalse((self.root / "player.pid").exists())

    def test_failed_address_cleanup_retains_lock(self):
        self.start_supervisor()
        self.wait_for_player()
        (self.root / "fail_remove").touch()
        self.process.terminate()
        self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 96)
        self.assertTrue((self.root / "lease.lock").exists())
        self.assertTrue((self.root / "alias").exists())

    def test_existing_lease_is_not_removed(self):
        (self.root / "lease.lock").mkdir()
        (self.root / "lease.lock/owner").write_text("someone else\n")
        self.start_supervisor()
        self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 91)
        self.assertEqual((self.root / "lease.lock/owner").read_text(), "someone else\n")

    def test_invalid_owner_rejected(self):
        for owner in ("0", "1", "-3", "42;echo bad", "999999999999999999999999999"):
            self.start_supervisor(owner)
            self.process.communicate(timeout=5)
            self.assertEqual(self.process.returncode, 90)
        self.assertFalse((self.root / "actions").exists())


if __name__ == "__main__":
    unittest.main()

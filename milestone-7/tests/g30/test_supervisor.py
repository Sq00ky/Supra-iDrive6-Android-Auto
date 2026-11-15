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
        self.tmp = tempfile.TemporaryDirectory(prefix='g30 native supervisor ')
        self.root = Path(self.tmp.name)
        (self.root / 'bin').mkdir()
        (self.root / 'flags').mkdir()
        (self.root / 'screen/1').mkdir(parents=True)
        for i in (1, 2):
            (self.root / f'screen/1/dpy-{i}').write_text('window manager = 42\nscene = (empty)\nwindows = (none)\n')
        (self.root / 'version').write_text('A different NBT firmware version\n')
        self.write('bin/ifconfig', '''#!/bin/sh
echo "$*" >> "$TEST_ROOT/actions"
case "$*" in
  *-alias) test ! -f "$TEST_ROOT/fail_remove" || exit 1; rm -f "$TEST_ROOT/alias";;
  *netmask*) test ! -f "$TEST_ROOT/fail_add" || exit 1; : > "$TEST_ROOT/alias";;
  *) echo ' inet 160.48.199.119 netmask ffffff00'
     if test -f "$TEST_ROOT/alias"; then echo ' inet 160.48.199.120 netmask ffffffff'; fi;;
esac
''')
        self.write('bin/arp', '#!/bin/sh\necho "no entry"\nexit 1\n')
        self.player('trap "exit 0" TERM INT HUP\nwhile :; do sleep 0.05; done\n')
        self.process = None
        self.env = dict(os.environ, TEST_ROOT=str(self.root))

    def write(self, name, text):
        p = self.root / name
        p.write_text(text)
        p.chmod(0o700)

    def player(self, body):
        self.write('kombi-player', '''#!/bin/sh
if test "$1" = --inspect; then
  test ! -f "$TEST_ROOT/fail_inspect" || exit 1
  echo SCREEN_INSPECT_OK secondary=800x480
  exit 0
fi
test "$1" = --service || exit 2
echo $$ > "$TEST_ROOT/player.pid"
''' + body)

    def start(self, owner=None):
        self.process = subprocess.Popen([os.environ['G30_TEST_SUPERVISOR_BIN'], str(self.root),
                                         str(owner if owner is not None else os.getpid())],
                                        env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def ready(self):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if (self.root / 'player.pid').exists():
                self.assertTrue((self.root / 'alias').exists())
                self.assertFalse(list(self.root.rglob('player.log')))
                return
            if self.process.poll() is not None:
                self.fail(self.process.communicate()[0])
            time.sleep(.02)
        self.fail('Player did not start')

    def cleaned(self, expected=0):
        text = self.process.communicate(timeout=6)[0]
        self.assertEqual(self.process.returncode, expected, text)
        self.assertIn('AA_CLUSTER_SERVICE_CLEANUP alias_removed=1', text)
        self.assertFalse((self.root / 'alias').exists())
        self.assertFalse(list(self.root.glob('lease*')))
        return text

    def tearDown(self):
        if self.process and self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            self.process.communicate(timeout=6)
        self.tmp.cleanup()

    def test_other_firmware_allowed_and_normal_stop_cleans(self):
        self.start(); self.ready()
        self.process.terminate()
        self.assertIn('A different NBT firmware version', self.cleaned())

    def test_owner_death_stops_player_and_cleans(self):
        owner = subprocess.Popen(['sleep', '20'])
        try:
            self.start(owner.pid); self.ready()
            owner.terminate(); owner.wait()
            self.cleaned()
        finally:
            if owner.poll() is None:
                owner.terminate(); owner.wait()

    def test_stuck_player_is_reaped_before_address_removal(self):
        self.player("trap '' TERM INT HUP\nwhile :; do sleep 0.05; done\n")
        self.start(); self.ready(); self.process.terminate()
        self.cleaned(128 + signal.SIGKILL)
        pid = int((self.root / 'player.pid').read_text())
        with self.assertRaises(ProcessLookupError): os.kill(pid, 0)

    def test_lost_log_reader_does_not_prevent_cleanup(self):
        owner = subprocess.Popen(['sleep', '20'])
        try:
            self.start(owner.pid); self.ready()
            self.process.stdout.close()
            owner.terminate(); owner.wait()
            self.assertEqual(self.process.wait(timeout=6), 0)
            self.assertFalse((self.root / 'alias').exists())
            self.assertFalse(list(self.root.glob('lease*')))
        finally:
            if owner.poll() is None:
                owner.terminate(); owner.wait()

    def test_player_failure_diagnostics_survive_cleanup(self):
        self.player('echo PLAYER_DIAGNOSTIC\nexit 17\n')
        self.start()
        self.assertIn('PLAYER_DIAGNOSTIC', self.cleaned(17))

    def test_existing_alias_untouched(self):
        (self.root / 'alias').touch(); self.start()
        self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 92)
        self.assertTrue((self.root / 'alias').exists())
        self.assertNotIn('-alias', (self.root / 'actions').read_text())
        self.assertFalse(list(self.root.glob('lease*')))

    def test_known_neighbor_untouched(self):
        self.write('bin/arp', '#!/bin/sh\necho "160.48.199.120 at 00:11:22:33:44:55"\n')
        self.start(); self.cleaned(93)
        self.assertNotIn('netmask', (self.root / 'actions').read_text())

    def test_missing_manager_refused_before_inspection(self):
        (self.root / 'screen/1/dpy-2').write_text('window manager = 0\n')
        self.start(); self.cleaned(98)
        self.assertFalse((self.root / 'player.pid').exists())

    def test_unknown_window_refused(self):
        (self.root / 'screen/1/dpy-2').write_text('window manager = 42\nscene = win-88\nwindows = win-88\n')
        self.start(); self.cleaned(97)

    def test_factory_window_retained(self):
        (self.root / 'screen/1/dpy-2').write_text('window manager = 42\nscene = win-88\nwindows = win-88\n')
        win = self.root / 'screen/1/win-88'
        win.mkdir()
        state = 'id string = 1056\ndisplay id = 2\nbuffer size = 800x480\nformat = SCREEN_FORMAT_RGBA8888\ndestination rectangle = (0,0 800x480)\n'
        (win / 'win-88').write_text(state)
        self.start(); self.ready(); self.process.terminate()
        self.assertIn('FACTORY_WINDOW_RETAINED', self.cleaned())
        self.assertEqual((win / 'win-88').read_text(), state)

    def test_bad_geometry_refused_before_alias(self):
        (self.root / 'fail_inspect').touch()
        self.start(); self.cleaned(94)
        self.assertNotIn('netmask', (self.root / 'actions').read_text())

    def test_address_creation_failure_releases_lock(self):
        (self.root / 'fail_add').touch()
        self.start(); self.cleaned(95)
        self.assertFalse((self.root / 'player.pid').exists())

    def test_failed_address_cleanup_retains_lock(self):
        self.start(); self.ready()
        (self.root / 'fail_remove').touch()
        self.process.terminate(); self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 96)
        self.assertTrue((self.root / 'lease.lock').exists())
        self.assertTrue((self.root / 'alias').exists())

    def test_existing_lease_is_not_removed(self):
        (self.root / 'lease.lock').mkdir()
        (self.root / 'lease.lock/owner').write_text('someone else\n')
        self.start(); self.process.communicate(timeout=5)
        self.assertEqual(self.process.returncode, 91)
        self.assertEqual((self.root / 'lease.lock/owner').read_text(), 'someone else\n')

    def test_invalid_owner_rejected(self):
        for owner in ('0', '1', '-3', '42;echo bad', '999999999999999999999999999'):
            self.start(owner); self.process.communicate(timeout=5)
            self.assertEqual(self.process.returncode, 90)
        self.assertFalse((self.root / 'actions').exists())


if __name__ == '__main__':
    unittest.main()

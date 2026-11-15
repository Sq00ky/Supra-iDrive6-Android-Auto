import importlib.util
import os
from pathlib import Path
import signal
import shutil
import subprocess
import tempfile
import time
import unittest
import xml.etree.ElementTree as ET
import zipfile

HERE = Path(__file__).resolve().parents[1]
def service_script():
    return (HERE / 'service.sh').read_text()


class ServiceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='aa-pip-service-')
        self.root = Path(self.tmp.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        # Model the car's small command set, rather than silently borrowing
        # missing QNX utilities (notably dirname/rmdir) from the Mac's PATH.
        for name in ('cat', 'grep', 'mkdir', 'rm', 'sed', 'sleep', 'wc'):
            (self.bin / name).symlink_to(shutil.which(name))
        (self.root / 'flags').mkdir()
        screen = self.root / 'screen/1'
        screen.mkdir(parents=True)
        for n in (1, 2):
            (screen / f'dpy-{n}').write_text('window manager = 42\nscene = (empty)\nwindows = (none)\n')
        (self.root / 'version').write_text('NBTevo_ID5_2111_Release ID5_2111-540-2142A Build 28\n')
        self.write(self.bin / 'uname', '#!/bin/sh\necho hu-omap\n')
        self.write(self.bin / 'arp', '#!/bin/sh\necho "no entry"\n')
        self.write(self.bin / 'ifconfig', '''#!/bin/sh
case "$*" in
  *-alias) rm -f "$TEST_ROOT/alias";;
  *netmask*) : > "$TEST_ROOT/alias";;
  *) echo 'vlan73: flags=UP'; echo ' inet 160.48.199.119 netmask ffffff00';
     if test -f "$TEST_ROOT/alias"; then echo ' inet 160.48.199.120 netmask ffffffff'; fi;;
esac
''')
        self.write(self.root / 'inspect', '#!/bin/sh\n'
                   'test "$LD_LIBRARY_PATH" = /opt/mm/lib:/lib:/usr/lib:/opt/sys/lib:/opt/car/lib\n')
        self.write(self.root / 'player', '''#!/bin/sh
echo $$ > "$TEST_ROOT/player.pid"
trap 'exit 0' TERM INT HUP
while :; do sleep 0.1; done
''')
        script = service_script().replace('/tmp/idriver-kombi-demo', str(self.root / 'lease'))
        script = script.replace('PATH=/bin:/usr/bin:/sbin:/usr/sbin:/opt/sys/bin:/opt/car/bin',
                                'PATH=' + str(self.bin))
        script = script.replace('/dev/screen', str(self.root / 'screen'))
        script = script.replace('/opt/hmi/ID5/data/ro/version.txt', str(self.root / 'version'))
        script = script.replace('/net/hu-jacinto/var/opt/sys', str(self.root / 'flags'))
        self.write(self.root / 'service.sh', script)
        self.env = dict(os.environ, PATH=str(self.bin), TEST_ROOT=str(self.root))
        self.process = None

    def write(self, path, text):
        path.write_text(text)
        path.chmod(0o700)

    def tearDown(self):
        if self.process and self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            self.process.communicate(timeout=12)
        self.tmp.cleanup()

    def start(self, owner=None):
        self.process = subprocess.Popen(['/bin/sh', str(self.root / 'service.sh'), str(owner or os.getpid())],
                                        env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def ready(self):
        until = time.monotonic() + 5
        while time.monotonic() < until:
            if (self.root / 'player.pid').exists():
                self.assertTrue((self.root / 'alias').exists())
                return
            if self.process.poll() is not None:
                self.fail(self.process.communicate()[0])
            time.sleep(.02)
        self.fail('Player did not start')

    def cleaned(self):
        text = self.process.communicate(timeout=12)[0]
        self.assertIn('AA_CLUSTER_SERVICE_CLEANUP alias_removed=1', text)
        self.assertFalse((self.root / 'alias').exists())
        self.assertFalse(list(self.root.glob('lease*')))

    def test_stop_owns_and_removes_only_its_alias(self):
        self.start(); self.ready()
        self.process.send_signal(signal.SIGTERM)
        self.cleaned()

    def test_parent_death_cleans_up_without_mac(self):
        parent = subprocess.Popen(['sleep', '20'])
        try:
            self.start(parent.pid); self.ready()
            parent.terminate(); parent.wait()
            self.cleaned()
        finally:
            if parent.poll() is None: parent.terminate(); parent.wait()

    def test_stuck_player_is_killed_and_alias_released(self):
        self.write(self.root / 'player', '''#!/bin/sh
echo $$ > "$TEST_ROOT/player.pid"
trap '' TERM INT HUP
while :; do sleep 0.1; done
''')
        self.start(); self.ready()
        self.process.send_signal(signal.SIGTERM)
        self.cleaned()

    def test_no_unavailable_car_commands(self):
        self.assertNotRegex(service_script(), r'\b(?:dirname|printf|rmdir|mktemp|touch|awk|sync)\b')

    def test_failed_player_diagnostics_survive_cleanup(self):
        self.write(self.root / 'player', '#!/bin/sh\necho PLAYER_DIAGNOSTIC\nexit 17\n')
        self.start()
        text = self.process.communicate(timeout=12)[0]
        self.assertEqual(self.process.returncode, 17)
        self.assertIn('AA_CLUSTER_PLAYER_FAILED status=17', text)
        self.assertIn('PLAYER_DIAGNOSTIC', text)
        self.assertFalse((self.root / 'alias').exists())
        self.assertFalse(list(self.root.glob('lease*')))

    def test_existing_alias_is_retained(self):
        (self.root / 'alias').touch()
        self.start()
        text = self.process.communicate(timeout=5)[0]
        self.assertIn('REFUSE_EXISTING_ALIAS', text)
        self.assertTrue((self.root / 'alias').exists())
        self.assertFalse((self.root / 'player.pid').exists())

    def test_unknown_display_manager_is_refused(self):
        (self.root / 'screen/1/dpy-2').write_text('window manager = 0\n')
        self.start()
        self.assertIn('REFUSE_MISSING_FACTORY_DISPLAY_MANAGER', self.process.communicate(timeout=5)[0])
        self.assertFalse((self.root / 'alias').exists())

    def test_plugin_declares_setting_and_text_in_every_language(self):
        rhmi = HERE.parent / 'rhmiapp'
        root = ET.parse(rhmi / 'NbtAad.xml')
        checkbox = root.find(".//hmiState[@id='14']/components/checkbox[@action='177']")
        self.assertIsNotNone(checkbox)
        self.assertEqual(checkbox.get('model'), '179')
        self.assertIsNotNone(root.find(".//raBoolModel[@id='179']"))
        self.assertEqual(root.find(".//textIdModel[@id='180']").get('textId'), '48')
        with zipfile.ZipFile(rhmi / 'NbtAad_ALL_Texts.zip') as z:
            for name in z.namelist():
                text = z.read(name).decode('utf-8')
                self.assertRegex(text, r'(?m)^48=.+$')


if __name__ == '__main__':
    unittest.main()

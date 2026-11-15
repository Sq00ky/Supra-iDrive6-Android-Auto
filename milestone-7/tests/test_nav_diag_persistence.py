"""Source contracts and independent filesystem model; does NOT execute C++.
Run with Python only. No compiler, SDK, daemon, vehicle or real /opt path used.
"""
import json
import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE='46d77e0f4e878ba6c4859da281b3581d96136067'
HEADER=(ROOT/'src/nav_diag.hpp').read_text()
TOTAL=15*1024*1024
SEGMENT=5*1024*1024

class RotationModel:
    """Independent POSIX-file model of the C++ rotation/pre-write quota policy."""
    def __init__(self,path,total=TOTAL):
        self.path=path
        self.maximum=total
        self.segment=total//3
        self.rotations=0
    def files(self):return [self.path,pathlib.Path(str(self.path)+'.1'),pathlib.Path(str(self.path)+'.2')]
    def write(self,body):
        record=body+b'\n'
        if len(record)>self.segment:return False
        self.path.parent.mkdir(mode=0o700,exist_ok=True)
        current,previous,oldest=self.files()
        size=current.stat().st_size if current.exists() else 0
        if size and (size>self.segment or len(record)>self.segment-size):
            oldest.unlink(missing_ok=True)
            if previous.exists():previous.rename(oldest)
            if current.exists():current.rename(previous)
            self.rotations+=1
            size=0
        available=self.maximum-size-len(record)
        older_size=previous.stat().st_size if previous.exists() else 0
        oldest_size=oldest.stat().st_size if oldest.exists() else 0
        if oldest_size>self.segment or older_size>self.segment or older_size>available or oldest_size>available-older_size:
            oldest.unlink(missing_ok=True)
        if older_size>self.segment or older_size>available:previous.unlink(missing_ok=True)
        with current.open('ab') as stream:stream.write(record)
        return True

def payload(sequence,size):
    start=(' {"sequence":%d,"padding":"'%sequence).encode('ascii')
    return start+b'x'*(size-len(start)-3)+b'"}' # plus newline = requested size

def check(test,model):
    sizes=[p.stat().st_size if p.exists() else 0 for p in model.files()]
    test.assertLessEqual(sum(sizes),TOTAL)
    test.assertTrue(all(n<=SEGMENT for n in sizes))

class PersistenceChecks(unittest.TestCase):
    def test_path_budget_flags_and_native_interfaces(self):
        self.assertIn('path("/opt/aa/logs/nbt-aad-nav-diag.jsonl")',HEADER)
        self.assertIn('maxTotalBytes(15u*1024u*1024u)',HEADER)
        self.assertIn('enabled(false),text(false)',HEADER)
        for flag in ('NBT_NAV_DIAG','NBT_NAV_DIAG_TEXT','NBT_NAV_DIAG_SOURCE'):self.assertIn(flag,HEADER)
        self.assertIn('segment_(maximum/3)',HEADER)
        self.assertIn('line.size()>=segment_',HEADER)
        self.assertIn('if(!trim(incoming))return false',HEADER)
        self.assertIn('mkdir(parent.c_str(),0700)',HEADER)
        self.assertIn('uint64_t bytes_',HEADER)
        self.assertIn('bytes=(uint64_t)st.st_size',HEADER)
        self.assertNotIn('std::filesystem',HEADER)

    def test_many_records_rotate_oldest_and_keep_total_cap(self):
        with tempfile.TemporaryDirectory()as folder:
            model=RotationModel(pathlib.Path(folder)/'logs/nav.jsonl')
            for i in range(31):
                self.assertTrue(model.write(payload(i,1024*1024)));check(self,model)
            self.assertEqual(model.rotations,6)
            ids=[]
            for file in reversed(model.files()):
                if file.exists():
                    for line in file.read_bytes().splitlines():ids.append(json.loads(line)['sequence'])
            self.assertEqual(ids,list(range(20,31)))

    def test_exact_file_boundary_and_atomic_oversized_rejection(self):
        with tempfile.TemporaryDirectory()as folder:
            model=RotationModel(pathlib.Path(folder)/'logs/nav.jsonl')
            self.assertTrue(model.write(payload(0,SEGMENT)));check(self,model)
            self.assertEqual(model.path.stat().st_size,SEGMENT)
            self.assertTrue(model.write(b'{}'));self.assertEqual(model.rotations,1);check(self,model)
            before=[p.read_bytes() if p.exists()else None for p in model.files()]
            self.assertFalse(model.write(payload(1,SEGMENT+1)))
            self.assertEqual(before,[p.read_bytes() if p.exists()else None for p in model.files()])

    def test_reopen_preserves_prior_records(self):
        with tempfile.TemporaryDirectory()as folder:
            path=pathlib.Path(folder)/'logs/nav.jsonl'
            self.assertTrue(RotationModel(path).write(b'{"first":1}'))
            self.assertTrue(RotationModel(path).write(b'{"second":2}'))
            self.assertEqual(path.read_bytes(),b'{"first":1}\n{"second":2}\n')

    def test_preexisting_oversized_generations_normalized(self):
        with tempfile.TemporaryDirectory()as folder:
            model=RotationModel(pathlib.Path(folder)/'nav.jsonl')
            for p in model.files():
                with p.open('wb')as f:f.truncate(SEGMENT+1)
            self.assertTrue(model.write(b'{"new":1}'));check(self,model)
            self.assertEqual(model.path.read_bytes(),b'{"new":1}\n')

    def test_parent_creation_and_invalid_parent_error(self):
        with tempfile.TemporaryDirectory()as folder:
            model=RotationModel(pathlib.Path(folder)/'logs/nav.jsonl')
            self.assertTrue(model.write(b'{}'));self.assertTrue(model.path.parent.is_dir())
            blocked=pathlib.Path(folder)/'not-dir';blocked.write_bytes(b'x')
            with self.assertRaises(FileExistsError):RotationModel(blocked/'nav.jsonl').write(b'{}')
        # Production exporter catches storage failures and explicitly leaves AA running.
        self.assertIn('catch(...)',HEADER)
        self.assertIn('Android Auto continues',HEADER)

    def test_decoder_original_fields_and_producer_unchanged(self):
        original=subprocess.check_output(['git','show',BASE+':milestone-7/src/nav_diag.hpp'],cwd=ROOT.parent,text=True)
        for start,end in [('inline Config environment()', 'struct Stats'),('inline Field describe(', 'class FileLog'),
                          ('void enqueue(', 'std::string envelope('),('std::string envelope(', 'static void* threadMain(')]:
            self.assertEqual(HEADER.split(start,1)[1].split(end,1)[0],original.split(start,1)[1].split(end,1)[0])
        paths=['src/cluster','src/aa_session.cpp','src/aa_session.hpp','src/most','src/dsi_client.cpp']
        self.assertEqual(subprocess.check_output(['git','diff',BASE,'--']+['milestone-7/'+p for p in paths],cwd=ROOT.parent),b'')

if __name__=='__main__':unittest.main()

#!/usr/bin/env python3
"""Host regressions for the G30 stream, native player lifecycle and pixel contract."""
import concurrent.futures
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
SRC=ROOT/'src/cluster/g30'
VENDOR=ROOT/'third_party/openh264'
OUT=ROOT/'tests/build/g30'
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'result.json').unlink(missing_ok=True)
compiler=shlex.split(os.environ.get('CXX','clang++'))
flags=['-std=c++11','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
inc=['-I'+str(ROOT/'src')]+['-I'+str(VENDOR/'codec'/p) for p in ('api','api/wels','common/inc','decoder/core/inc','decoder/plus/inc')]
env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1',ASAN_OPTIONS='detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1')
passed=[]
def run(name,cmd,cwd=ROOT):
    p=subprocess.run([str(x) for x in cmd],cwd=cwd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=180)
    (OUT/(name+'.log')).write_text(p.stdout)
    if p.returncode: raise RuntimeError(name+' failed:\n'+p.stdout[-4000:])
    return p.stdout
sources=[p for p in sorted((VENDOR/'codec/common/src').glob('*.cpp')) if not p.name.startswith('Wels')]
sources += [p for p in sorted((VENDOR/'codec/decoder/core/src').glob('*.cpp')) if p.name!='wels_decoder_thread.cpp']
sources += [VENDOR/'codec/decoder/plus/src/welsDecoderExt.cpp',SRC/'decoder_port.cpp']
def compile_one(p):
    obj=OUT/(p.stem+'.o')
    run('compile-'+p.stem,[*compiler,*flags,'-Wno-deprecated-register','-fno-exceptions','-fno-rtti','-fno-strict-aliasing',*inc,'-c',p,'-o',obj])
    return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool: objects=list(pool.map(compile_one,sources))
archive=OUT/'decoder.a'
archive.unlink(missing_ok=True)
run('archive',['ar','rcs',archive,*objects])
run('fixture',[sys.executable,HERE/'make_fixture.py',OUT])
for name in ('router','router_faults','encoded_queue','live_setting','screen','control','pixels','player_lifecycle'):
    run('compile-'+name,[*compiler,*flags,*inc,HERE/f'test_{name}.cpp',*([archive] if name in ('pixels','player_lifecycle') else []),'-o',OUT/name])
    run(name,[OUT/name],OUT);passed.append(name);print('PASS',name,flush=True)
ssl_flags=shlex.split(os.environ.get('OPENSSL_CFLAGS',''))
if not ssl_flags:
    p=subprocess.run(['pkg-config','--cflags','openssl'],stdout=subprocess.PIPE,text=True)
    if p.returncode: raise RuntimeError('Set OPENSSL_CFLAGS to your OpenSSL include directory')
    ssl_flags=shlex.split(p.stdout)
run('compile-source-session',[*compiler,*flags,'-pthread','-DNBT_HOST_TEST','-DIDRIVER_G30_CLUSTER','-include',ROOT/'tests/stub_cryptor.hpp','-I'+str(ROOT/'src'),'-I'+str(ROOT/'include'),*ssl_flags,HERE/'test_source_session.cpp',*(ROOT/p for p in ('src/aa_session.cpp','src/audio/audio_acks.cpp','src/crash_handler.cpp','src/sdr_builder.cpp')),'-o',OUT/'source-session'])
run('source-session',[OUT/'source-session']);passed.append('source-session');print('PASS source-session',flush=True)
(OUT/'result.json').write_text(json.dumps({'passed':passed,'native_qnx_build':False,'physical_car_test':False},indent=2)+'\n')

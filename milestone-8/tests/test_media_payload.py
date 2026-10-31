# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)
"""Compile the actual payload-boundary block from aa_session.cpp and test it.

Run: python tests/test_media_payload.py
Uses MSVC on Windows (via vswhere) or g++/clang++ elsewhere. No QNX SDK needed.
This tests payload extraction, NOT decoding or target hardware playback.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src' / 'aa_session.cpp').read_text(encoding='utf-8')
start = source.index('const uint8_t* p =', source.index('if (channel == 6)'))
end = source.index('if (msg_id == 0x0001)', start)
block = source[start:end]

harness = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <vector>

static size_t payload_offset(const std::vector<uint8_t>& frame_data, uint16_t msg_id) {
    const unsigned channel = 6;
    (void)channel;
    do {
        BLOCK
        (void)nal_len;
        return static_cast<size_t>(nal_ptr - &frame_data[0]);
    } while (false);
    return static_cast<size_t>(-1);
}

int main() {
    // Captured Channel 6 prefix from aa-session 7.txt. Timestamp contains a
    // false Annex-B match at offset 1; actual video starts at offset 10.
    const uint8_t capture[] = {
        0x00,0x00,0x00,0x00,0x01,0x78,0x2e,0x17,0x7c,0xb2,
        0x00,0x00,0x00,0x01,0x65,0xb8
    };
    std::vector<uint8_t> frame(capture, capture + sizeof(capture));
    const size_t got = payload_offset(frame, 0);
    if (got != 10) {
        printf("FAIL captured timestamp: expected payload offset 10, got %u\n", (unsigned)got);
        return 1;
    }
    puts("PASS captured timestamp: payload starts at offset 10");
    // The message ID itself can overlap a start-code pattern when PTS is 0.
    for (unsigned i = 2; i < 10; ++i) frame[i] = 0;
    if (payload_offset(frame, 0) != 10) return 2;
    puts("PASS zero timestamp");

    const uint8_t config[] = {0,1,0,0,0,1,0x67,0x42};
    frame.assign(config, config + sizeof(config));
    if (payload_offset(frame, 1) != 2) return 3;
    puts("PASS codec config: payload starts at offset 2");

    const uint8_t three_byte[] = {0,0,0,0,1,0x78,0x2e,0x17,0x7c,0xb2,0,0,1,0x65};
    frame.assign(three_byte, three_byte + sizeof(three_byte));
    if (payload_offset(frame, 0) != 10) return 4;
    puts("PASS three-byte Annex-B start code");

    for (size_t n = 0; n <= 10; ++n) {
        frame.assign(n, 0);
        if (payload_offset(frame, 0) != static_cast<size_t>(-1)) return 5;
    }
    for (size_t n = 0; n <= 2; ++n) {
        frame.assign(n, 0);
        if (payload_offset(frame, 1) != static_cast<size_t>(-1)) return 6;
    }
    puts("PASS truncated/empty video and config messages rejected");
    return 0;
}
'''.replace('BLOCK', block)

scratch = os.environ.get('TMPDIR')
if os.name == 'nt':
    scratch = str(Path.home() / 'AppData/Local/hermes/cache/scratch')
if scratch:
    Path(scratch).mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='aa-payload-test-', dir=scratch, ignore_cleanup_errors=True) as temp:
    work = Path(temp)
    cpp = work / 'payload_test.cpp'
    exe = work / ('payload_test.exe' if os.name == 'nt' else 'payload_test')
    cpp.write_text(harness, encoding='utf-8')
    if os.name == 'nt':
        vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        install = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
            '-property', 'installationPath'
        ], text=True).strip()
        if not install:
            raise RuntimeError('MSVC C++ toolchain not found')
        devcmd = Path(install) / 'Common7/Tools/VsDevCmd.bat'
        batch = work / 'build.cmd'
        batch.write_text(
            '@echo off\ncall "' + str(devcmd) + '" -arch=x64 >nul\n'
            'if errorlevel 1 exit /b 1\n'
            'cl /nologo /EHsc /W4 "' + str(cpp) + '" /Fe:"' + str(exe) + '"\n'
            'exit /b %errorlevel%\n', encoding='utf-8')
        subprocess.run(['cmd.exe', '/d', '/c', str(batch)], cwd=work, check=True)
    else:
        compiler = shutil.which('g++') or shutil.which('clang++')
        if not compiler:
            raise RuntimeError('A C++ compiler is required')
        subprocess.run([compiler, '-std=c++98', '-Wall', '-Wextra', str(cpp), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], cwd=work)
    raise SystemExit(result.returncode)

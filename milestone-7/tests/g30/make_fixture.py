#!/usr/bin/env python3
"""Synthetic H.264 access units, never a recording of phone/car data."""
from pathlib import Path
import os
import sys
import re
import shutil
import struct
import subprocess
HERE=Path(__file__).resolve().parents[2]
def main():
    dest=Path(sys.argv[1]) if len(sys.argv)>1 else HERE/'tests/build/g30'
    dest.mkdir(parents=True,exist_ok=True)
    ffmpeg=os.environ.get('FFMPEG') or shutil.which('ffmpeg')
    if not ffmpeg:raise RuntimeError('Set FFMPEG or install ffmpeg on PATH')
    for name,source in [('fixture','testsrc2=size=800x480:rate=10'),
                        ('static','color=c=blue:size=800x480:rate=10')]:
        h264=dest/(name+'.h264')
        subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-y','-f','lavfi','-i',
            source,'-frames:v','30','-an','-c:v','libx264',
            '-profile:v','baseline','-pix_fmt','yuv420p','-x264-params',
            'aud=1:repeat-headers=1:keyint=10:bframes=0:threads=1','-f','h264',str(h264)],check=True)
        data=h264.read_bytes();starts=[]
        for m in re.finditer(b'\x00\x00(?:\x00)?\x01',data):
            if data[m.end()]&31==9:starts.append(m.start())
        assert starts[0]==0 and len(starts)==30
        starts.append(len(data))
        def packet(kind,payload=b'',pts=0):return struct.pack('!IIIIQ',0x41435031,kind,3,len(payload),pts)+payload
        wire=packet(1)
        for i in range(30):wire+=packet(2,data[starts[i]:starts[i+1]],i*100000)
        wire+=packet(3)
        (dest/(name+'.acp')).write_bytes(wire)
        print(f'{name}: 30 access units, {len(wire)} bytes')
if __name__=='__main__':main()

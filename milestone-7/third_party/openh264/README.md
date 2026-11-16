# OpenH264 decoder subset

Cisco OpenH264 2.6.0, from https://github.com/cisco/openh264/tree/v2.6.0.
Only API, common and decoder C/C++ sources are included. See LICENSE (BSD-2-Clause).
The build uses the single-threaded decoder and the QNX port in
`src/cluster/video/decoder_port.cpp`; no encoder or assembly optimizations are built.

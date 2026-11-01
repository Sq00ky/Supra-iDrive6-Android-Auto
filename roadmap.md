# Overview
This document is going to contain a list of "What's missing", "What's left", and "What do we need to do to get there".

## Roadmap
Audio Improvements
- Moving over to WiFi instead of Bluetooth

HUD/KOMBI Integration
- Working towards this, proof of concept is working, navigation arrows are being shown on the HUD/KOMBI.

Main Menu Entry
- I've done some research and have been able to make

Parking Assistant
- There's still a bit more work that needs to be done in terms of state checking

Carplay Parity Improvements
- There may be a few bugs remaining that need to be worked out re: Session creation & Session teardown

Resolution Upscaling
- Proposed solution to request higher resolution from the phone, and drop a few rows to improve res.

USB Support
- Currently everything relies on Carplay's WiFi network
- There's three possible solutions, a free FSC installer to enable Carplay (possibly WIP), Move to WiFi Direct hotspot,  USB

Bug Fixes
- Better Bluetooth pairing (re-pair phone seems to consistently work now)
- Verifying multi-tap fix
- Firewall needs to be modified so we don't straight up disable it
- Better MyBMW/Supra Connect/MINI App Support
- Installation Failure


One Click USB Update
- More discussion needs to be done around this
- Proposed solution is tar containing releases.tar & nbt-aad

Lastly... Installer
- Enable SSH
- Transfer files via SSH/SCP
- Close SSH when done for 

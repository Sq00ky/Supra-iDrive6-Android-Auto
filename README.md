# Overview
I'm a believer of humans writing things, but this is well beyond my skillset, so I (me, Ronnie) am writing this (the words on the page you're reading). I'm not a C-dev, but I do know these things can be done... So... This repository contains code for the J29 Toyota Supra to run Android Auto natively on the NBT Evo head unit. Yeah, talk about a big one. I'll preface it with this: Big screw you to all the companies who have profited for **years** on MMI units. You could have drove innovation, yet you chose to implement a "just good enough" solution to get people to bite. Okay, now that that's out of the way...

# A Note on Bench Setups & Retrofits
I cannot provide support for bench setups. I cannot provide support for retrofits. I cannot guarantee hardware compatibility. I'm not familiar enough with 4 -> 6, or 5 -> 6, and cannot verify the hardware will work. I need to test on things I have easy access to, and I just don't have that hardware.
I went through maybe 60-90 rounds of testing on my real car, and I cannot do that with you remotely.

## License
NBT-AAD is licensed under the **PolyForm Noncommercial License 1.0.0** (`SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0`).
The full text is embedded in `src/main.cpp` / `src/license_text.hpp` and printed by `./nbt-aad --license`.

Required Notice: Copyright (c) 2026 nanaisu - NBT-AAD (https://github.com/Sq00ky/Supra-iDrive6-Android-Auto)

NBT-AAD is free of charge. Selling it, or bundling it with anything sold, is not permitted.
If you paid for it, you were scammed: official releases are only published at https://github.com/Sq00ky/Supra-iDrive6-Android-Auto
Please report any sellers in here: https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/issues

---

The only authorized bundlers is TurboCode for directly supporting the development of this project by offering free SSH unlocks, and providing guidance for native integration.

## Disclaimer
**I am not responsible for anything you do on your device, if you execute a command, or change files, do so with care - if you break something, only Toyota/BMW can reflash and virginize your head unit. The code is shipped as-is. You may fork the repo, clone it, change whatever you like. I cannot ensure compatibility.**

## Requirements
- Temporarily Apple CarPlay (Looking into WiFi Direct compatibility)
- SSH Access to your NBT Evo head unit.
- An Ethernet -> ODB2 cable
- A computer w/ an SSH Client

## No Flashdrive? No Problem.
Grab a QNX Compiled SCP Binary and run:
```
ssh root@<hu-ip> 'cat > /tmp/scp-server' < scp-server
ssh root@<hu-ip>
mv /tmp/scp-server /bin/scp
chmod +x /bin/scp
```

## Instructions
Installation instructions can be found in the [install.md file](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/blob/main/install.md)

### Common Problems
1. We've noticed running AAIDrive in parallel can break NBT-AAD. We'd suggest uninstalling it for the time being until I have some more time to experiment.
2. We've also noticed that a /dev/btspp0 not found error. This seems to come an issue with the head unit not fully opening a Bluetooth channel. I recommend trying to re-pair your phone, or going into the device settings in iDrive and go through the list. Tap "Phone", tap "Bluetooth Audio", tap "Apps", and see if any of them trigger a Bluetooth connection again, and see if your phone starts searing for Android Auto.

## Building Yourself
The code needs to be compatible with Armv7LE. I built it with [QNX Momentics](https://archive.org/details/qnxsdp-65.7z) because that's probably what the developers iDrive6 did. Be warned, this is from archive.org. Run it in a VM. I cannot guarntee the safety/security of the software. It is as simple as creating a new project in Momentics, adding all the files to the project folder, right clicking on your project folder, selecting "clean", and then "build". 

### Feeling Generous?
I don't charge for this project, I don't charge for deployments, if this has helped you and you're feeling generous, here's my paypal: https://paypal.me/banananaisu
Please do not feel obligated to send me anything. I've had a couple people ask, if it makes you feel better, you can buy me a drink. I like Red Wine & Bourbon :) My goal in this was never to make any money. It is to provide a cheap/low cost solution so we don't have to deal with MMI products & don't have to deal with people selling native Android Auto integrations.

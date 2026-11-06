# Installation Guide
**11/6/2026 Update - Self Service Install**
The following tool can be used to enable SSH temporarily: https://ssh.bavarian-aa.uk/
The following tool can be used to install Android Auto: https://turbocode.shop/nbt-evo-android-auto/

### Hardware Requirements for Self Service Install
1. [ODB to ENET Cable](https://www.amazon.com/dp/B0CSX3F5GR)
2. OPTIONAL - [USB A/C to Ethernet Cable](https://www.amazon.com/TP-Link-Ethernet-Thunderbolt-Compatible-UE310C/dp/B0GF9SZBSZ) **ONLY ORDER THIS IF YOUR LAPTOP DOESN'T HAVE AN ETHERNET PORT**
3. Laptop or Desktop
4. High speed internet

If you have issues, please put it in this issue: https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/issues/24


# DIY Install Guide
**Note: Releases are in the release folder. The milestones are source code, designed to be built, and then deployed. For releases, see: https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/releases**
## Overview
Computers are hard. I'm going to try to make this as easy as possible for folks to understand, hopefully this will help you understand what's happening behind the scenes a little bit more. The whole process takes roughly 30-45 minutes. The more experienced you are, the faster this will probably go.

### Known Limitations
- AAIDrive cannot be running/installed
- 3d Buildings in Google Maps may cause lag
- Apple CarPlay currently required (we use it's WiFi network).

### Hardware Requirements
1. [ODB to ENET Cable](https://www.amazon.com/dp/B0CSX3F5GR)
2. OPTIONAL - [USB A/C to Ethernet Cable](https://www.amazon.com/TP-Link-Ethernet-Thunderbolt-Compatible-UE310C/dp/B0GF9SZBSZ) **ONLY ORDER THIS IF YOUR LAPTOP DOESN'T HAVE AN ETHERNET PORT**
3. Flash Drive formatted in FAT32/exFAT
4. Laptop or Desktop
5. High speed internet

## What are we doing?
Opening up a temporary path to execute commands on the cars head unit. This is being done with software by [TurboCode](https://turbocode.shop/bmw-nbt-evo-ssh-unlocker/). It enables an IT protocol called SSH (Secure Shell) that will let us execute those commands. This protocol is encrypted and secure.
During this process, we are going to install two applications.
1. The Android Auto Daemon
2. An iDrive6 plugin that lets you start Android Auto.

The Daemon sits on your headunit and waits for you to click "Android Auto" and start it. It does not run on start, this is by design.

## What You Need
1. [BMW TurboCode Proxy](https://turbocode.shop/bmw-remote-enet-tool/) - This tool discovers your Head Unit IP Address
2. [Bavarian SSH Client](https://ssh.bavarian-aa.uk/) - This tool enables SSH access to your NBT Evo Head Unit
3. A prepared Flash Drive

### Bavarian SSH Client
[!(video)](https://ssh.bavarian-aa.uk/demo.webm)
Click start and you should be good to go.
### TurboCode Proxy
It's super easy, press "Search ZGW", you should see your VIN number pop up as long as your ODB -> ENet cable is successfully connected.
Take note of the IP Address you need to connect to. This may vary, you should see a 169.254.x.x:22 IP Address. You can click **Start** as well and SSH in to 127.0.0.1 -P 6969

<img width="752" height="519" alt="image" src="https://github.com/user-attachments/assets/9e0ffd59-3a7f-48aa-b363-4a199e75bde0" />

Make sure you see: Forwarding 0.0.0.0:6969 → 169.254.199.119:22  

### Prepared Flash Drive
The flash drive must be formatted in FAT32 or exFAT. You can do this by right clicking on your flash drive, selecting format, and selecting FAT32 or exFAT. Do not select NTFS.

<img width="361" height="488" alt="image" src="https://github.com/user-attachments/assets/d12c189c-12c1-4dae-ae5a-2901bfd109fa" />

Once formatted, head over to the [Releases page](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/releases/) and download the latest nbt-aad and release.zip

<img width="949" height="367" alt="image" src="https://github.com/user-attachments/assets/e2ffed2a-afd1-428b-8ca6-62a562cefd2f" />

Please **extract** releases.zip into a folder on your flash drive, and copy nbt-aad into the root of your flash drive. If done correctly, it shoud look like this:

<img width="999" height="241" alt="image" src="https://github.com/user-attachments/assets/446cd97a-104d-47e4-ac64-8ca97d713ada" />


<img width="917" height="382" alt="image" src="https://github.com/user-attachments/assets/26f9b559-b2be-4f53-ac07-a870f6630d49" />

If it looks like this, you're ready to go!

### What We Are Doing
From there, we will change the head unit from Read Only to Read Write temporarily to install the Android Auto Daemon and the Plugin to enable access in iDrive. This will be done by executing a series of commands over SSH. 
Either I can do this, or you can do this. No pressure. I don't mind doing this for you. I've done both ways. If you're technically inclined and want to poke around, feel free. If not, no worries. I got you!

### WARNING
Don't use PuTTY - these SSH connection commands are meant for a normal SSH client. **NOT PuTTY**

Open cmd.exe / powershell.exe and run them. **Don't use PuTTY.**

### What are those commands?
I'm glad you asked!
```bash
ssh root@127.0.0.1 -o HostKeyAlgorithms=+ssh-dss -m hmac-sha1 -p 6969
# or
ssh root@169.254.X.X -o HostKeyAlgorithms=+ssh-dss -m hmac-sha1
# This will be the IP Address mentioned
# The password is ts&SK412
mount -o remount rw /net/hu-omap/fs/sda0
on -f /net/hu-omap/ /bin/sh
ls -l /mnt/
# find your flashdrive, it should be named something like /mnt/umass01200  , umassXXXX is a place holder name. Please don't blindly copy & paste these.
cd /fs/sda0/opt/
mkdir aa
cd /opt/aa/
cp /mnt/umassXXXXX/nbt-aad ./
chmod +x /opt/aa/nbt-aad
```

We're then install the plugins, which is done with these commands:
```bash
mkdir -p /opt/hmi/ID5/data/ro/Plugins/NbtAad
cd /opt/hmi/ID5/data/ro/Plugins/NbtAad/
cp /mnt/umassXXXXX/release/* ./
ls -l /opt/hmi/ID5/data/ro/Plugins/NbtAad/
```

Before restarting, we're going to use pf_aa to add a Firewall rule to permit Android Auto communication, so we don't need to tear down the whole firewall.
```bash
cp /mnt/umassXXXXX/pf_aa /dev/shmem/
cd /dev/shmem/
chmod +x /dev/shmem/pf_aa
/dev/shmem/pf_aa --install
/dev/shmem/pf_aa --check
# MAKE SURE IT DID NOT ERROR OUT AND THE FIREWALL RULE SUCCESSFULLY INSTALLED
# DO NOT RUN THIS COMMAND UNTIL YOU VERIFY IT RAN SUCCESSFULLY.
OnOffDSICommander appreset
```

After the HMI restarts, go into Media/Radio, look for Android Auto. Enable Log to USB, and click start.
**DO NOT EXPECT ANDROID AUTO TO START IMMEDIATELY.**
You 9/10 times will need to re-pair your phone as a new bluetooth device to the car.
If you're installing it yourself, read the logs. It'll often tell you what's wrong.

### Common Issues
/dev/btspp0 (No such file or directory)? - Re-pair your phone to the car.
WiFi Information seen but phone not connecting? - Turn off your hotspot. Check ``/var/opt/conn/carplay/*.cfg | grep -i pass`` and see if the network passwords vary. Reboot your phone.

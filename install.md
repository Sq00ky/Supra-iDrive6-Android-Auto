# Installation Guide
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

## What I need From You
Two pieces of software need to be downloaded and installed on your computer
1. [ZeroTier](https://www.zerotier.com/) - This lets me remotely access a private network with just me and your computer.
2. [BMW TurboCode Proxy](https://turbocode.shop/bmw-remote-enet-tool/) - This lets me specifically access your head unit, via an enet cable
3. A prepared Flash Drive
I will give you a private ZeroTier network to connect to, this will be sent via Direct Message on whatever platform we're talking on! I'll have to approve your access afterwards. This is how I will connect to your car and enable SSH.

### Joining Zero Tier Network
Zero Tier has a very small UI, it's hidden in the bottom corner of your task bar, right click it, and select Join Network:

<img width="376" height="270" alt="image" src="https://github.com/user-attachments/assets/d95154d4-4e4b-42f8-af79-a9a1036babeb" />

### TurboCode Proxy
It's super easy, press "Search ZGW", you should see your VIN number pop up as long as your ODB -> ENet cable is successfully connected.
Once you find it, press **Start**!

<img width="752" height="519" alt="image" src="https://github.com/user-attachments/assets/9e0ffd59-3a7f-48aa-b363-4a199e75bde0" />

Make sure you see: Forwarding 0.0.0.0:6969 → 169.254.199.119:22  

### Prepared Flash Drive
The flash drive must be formatted in FAT32 or exFAT. You can do this by right clicking on your flash drive, selecting format, and selecting FAT32 or exFAT. Do not select NTFS.

<img width="361" height="488" alt="image" src="https://github.com/user-attachments/assets/d12c189c-12c1-4dae-ae5a-2901bfd109fa" />

Once formatted, head over to the [Releases page](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/releases/) and download the latest nbt-aad and release.zip

<img width="949" height="367" alt="image" src="https://github.com/user-attachments/assets/e2ffed2a-afd1-428b-8ca6-62a562cefd2f" />

Please **extract** releases.zip into a folder on your flash drive, and copy nbt-aad into the root of your flash drive. If done correctly, it shoud look like this:

<img width="856" height="226" alt="image" src="https://github.com/user-attachments/assets/bf77e4b6-4a1c-465d-b0c9-f3d5207207b4" />


<img width="917" height="382" alt="image" src="https://github.com/user-attachments/assets/26f9b559-b2be-4f53-ac07-a870f6630d49" />

If it looks like this, you're ready to go!

### What I Need To Do For You
From there, we will change the head unit from Read Only to Read Write temporarily to install the Android Auto Daemon and the Plugin to enable access in iDrive. This will be done by executing a series of commands over SSH. 
Either I can do this, or you can do this. No pressure. I don't mind doing this for you. I've done both ways. If you're technically inclined and want to poke around, feel free. If not, no worries. I got you!

### WARNING
Don't use PuTTY - these SSH connection commands are meant for a normal SSH client. **NOT PuTTY**

Open cmd.exe / powershell.exe and run them. **Don't use PuTTY.**

### What are those commands?
I'm glad you asked!
```bash
ssh root@YOURIP -o HostKeyAlgorithms=+ssh-rsa -m hmac-sha1 -p 6969
# or
ssh root@169.254.199.99 -o HostKeyAlgorithms=+ssh-rsa -m hmac-sha1
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

From there, we're going to test and verify that the Android Auto Daemon does **not** crash, and functions as expected. We'll make sure your phone connects to it okay, and nothing breaks. If something does break or doesn't work out of the gate, we may spend a few minutes troubleshooting it.

After we verify it's working okay, we then install the plugins, which is done with these commands:
```bash
mkdir -p /opt/hmi/ID5/data/ro/Plugins/NbtAad
cd /opt/hmi/ID5/data/ro/Plugins/NbtAad/
cp /mnt/umassXXXXX/release/* ./
ls -l /opt/hmi/ID5/data/ro/Plugins/NbtAad/
OnOffDSICommander appreset
```

After the HMI restarts, go into Media/Radio, look for Android Auto. Enable Log to USB, and click start.
**DO NOT EXPECT ANDROID AUTO TO START IMMEDIATELY.**
You 9/10 times will need to re-pair your phone as a new bluetooth device to the car.
If you're installing it yourself, read the logs. It'll often tell you what's wrong.

### Common Issues
/dev/btspp0 (No such file or directory)? - Re-pair your phone to the car.
WiFi Information seen but phone not connecting? - Turn off your hotspot. Check ``/var/opt/conn/carplay/*.cfg | grep -i pass`` and see if the network passwords vary. Reboot your phone.

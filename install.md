# Installation Guide

Using the experimental G30 cluster-map build? Follow the
[three-file installation steps](#g30-cluster-map-build) below.
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

[demo](https://github.com/user-attachments/assets/e38313fe-8473-4dc8-bfd0-d32993cbc0f9)

Click start and you should be good to go.

### TurboCode Proxy
It's super easy, press "Search ZGW", you should see your VIN number pop up as long as your ODB -> ENet cable is successfully connected.
Take note of the IP Address you need to connect to. This may vary, you should see a 169.254.x.x:22 IP Address. You can click **Start** as well and SSH in to 127.0.0.1 -P 6969

<img width="752" height="519" alt="image" src="https://github.com/user-attachments/assets/9e0ffd59-3a7f-48aa-b363-4a199e75bde0" />

Make sure you see: Forwarding 0.0.0.0:6969 → 169.254.199.119:22  

### Prepared Flash Drive
The flash drive must be formatted in FAT32 or exFAT. You can do this by right clicking on your flash drive, selecting format, and selecting FAT32 or exFAT. Do not select NTFS.

<img width="361" height="488" alt="image" src="https://github.com/user-attachments/assets/d12c189c-12c1-4dae-ae5a-2901bfd109fa" />

Once formatted, head over to the [Releases page](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/releases/) and download the latest nbt-aad, pf_aa, and release.zip

<img width="880" height="413" alt="image" src="https://github.com/user-attachments/assets/a67a3675-966a-4ddb-898f-9fb2151cdcd4" />

Please **extract** releases.zip into a folder on your flash drive, and copy nbt-aad and pf_aa into the root of your flash drive. If done correctly, it shoud look like this:

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
cp /mnt/umassXXXXX/Releases/* ./
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

## G30 cluster-map build

This experimental build adds **Show map in instrument cluster** to the Android
Auto settings. Download these three assets from the same custom build:

- `nbt-aad`
- `kombi-player`
- `Releases.zip`

Extract `Releases.zip` onto the FAT32/exFAT USB drive. Copy the other two files
to its root. Leave the text and image ZIPs inside `Releases/` compressed:

```text
USB drive/
  nbt-aad
  kombi-player
  Releases/
    libNbtAad.so
    NbtAad.manifest
    NbtAad.xml
    NbtAad_ALL_Texts.zip
    NbtAad_ALL_Images.zip
```

Enable SSH using the instructions above. Keep the car parked and awake; use
**Force Stop** in the Android Auto menu before updating. Connect to the OMAP
node (usually `169.254.199.119`) and confirm `uname -n` prints `hu-omap`.
Use `ls /mnt` to find your USB drive, then replace `umassXXXXX` below.

Run in a root shell on OMAP. The subshell stops on a failed command and restores
the read-only mount. A backup is kept in `/opt/aa/g30-backup-PID`; note the
printed path. It stages replacements first and renames the loaded plugin
directory instead of overwriting its library.

```sh
(
set -e
g30_usb=/mnt/umassXXXXX
test "$(uname -n)" = hu-omap
test -s "$g30_usb/nbt-aad"
test -s "$g30_usb/kombi-player"
test ! -d /tmp/idriver-kombi-demo.lock
g30_processes=$(pidin ar)
if echo "$g30_processes" | grep '[n]bt-aad' >/dev/null; then
    echo 'Force Stop Android Auto before updating.'
    exit 1
fi
mount -uw /fs/sda0
trap 'mount -ur /fs/sda0' 0
g30_backup=/opt/aa/g30-backup-$$
mkdir -p /opt/aa
mkdir "$g30_backup"
echo "Backup: $g30_backup"
mkdir "$g30_backup/new-plugin"
for g30_file in NbtAad.manifest NbtAad.xml NbtAad_ALL_Texts.zip NbtAad_ALL_Images.zip libNbtAad.so; do
    cp "$g30_usb/Releases/$g30_file" "$g30_backup/new-plugin/$g30_file"
    diff -q "$g30_usb/Releases/$g30_file" "$g30_backup/new-plugin/$g30_file"
done
for g30_file in nbt-aad kombi-player; do
    cp "$g30_usb/$g30_file" "$g30_backup/new-$g30_file"
    diff -q "$g30_usb/$g30_file" "$g30_backup/new-$g30_file"
    chmod 755 "$g30_backup/new-$g30_file"
    if test -f "/opt/aa/$g30_file"; then
        cp -p "/opt/aa/$g30_file" "$g30_backup/$g30_file"
    fi
done
chmod 644 "$g30_backup/new-plugin/"*
chmod 755 "$g30_backup/new-plugin/libNbtAad.so"
if test -f /opt/aa/nbt-aad.env; then
    cp -p /opt/aa/nbt-aad.env "$g30_backup/nbt-aad.env"
fi
g30_plugin=/opt/hmi/ID5/data/ro/Plugins/NbtAad
if test -d "$g30_plugin"; then mv "$g30_plugin" "$g30_backup/NbtAad"; fi
mkdir -p /opt/hmi/ID5/data/ro/Plugins
mv "$g30_backup/new-plugin" "$g30_plugin"
mv "$g30_backup/new-nbt-aad" /opt/aa/nbt-aad
mv "$g30_backup/new-kombi-player" /opt/aa/kombi-player
echo 'Files installed. Review environment and firewall before restarting.'
)
```

If any step fails, keep the printed backup and restore the files before
restarting. In `/opt/aa/nbt-aad.env`, preserve unrelated settings and set
`NBT_VEHICLE=bmw`, `NBT_KOMBI_NAV=0` and `NBT_KEEP_PF=1`. Remove any old
`NBT_G30_CLUSTER_*` overrides so the installed player and checkbox are used.
Remount writable only while editing, then run `mount -ur /fs/sda0` again.

For a first installation, use the daemon's built-in firewall tool:

```sh
(
set -e
mount -uw /fs/sda0
trap 'mount -ur /fs/sda0' 0
/opt/aa/nbt-aad --pf-install
g30_pf_result=0
/opt/aa/nbt-aad --pf-check || g30_pf_result=$?
case "$g30_pf_result" in
    0) echo 'Firewall rules verified.' ;;
    9) echo 'Rules saved; full head-unit restart required.' ;;
    *) echo 'Firewall check failed; inspect before restarting.'; exit 1 ;;
esac
)
```

A separate `pf_aa` download is not required. Restart iDrive after the files
and checks are complete; a full head-unit restart is needed if persistent
firewall rules changed.

Cancel BMW route guidance for the first test.
Start Android Auto, open Google Maps and enable **Settings → Show map in
instrument cluster**. The player starts automatically. Switching the setting
off restores the dial. Switching it on requests the map again. With the map enabled,
select HUD navigation in iDrive to move the map to the HUD. Turn HUD navigation
off to return it to the cluster. No reboot is
needed for toggling. Video buffers stay in RAM. Kombi diagnostics use the daemon's existing log destination.

The secondary map starts at 200 DPI. To try a larger layout, set
`NBT_PIP_MAP_DPI=240` in `/opt/aa/nbt-aad.env`, restart Android Auto and
reconnect the phone. Set it to 160 for the original cluster and HUD scale.

The reference setup is a G30 with NBT Evo 007.039.008. Firmware version is
reported in the log and does not reject a different version by itself. Live
network/display checks still apply; other firmware and cluster combinations
remain unvalidated. HUD map output is experimental and still needs a physical
test. Run it with BMW route guidance cancelled.

For diagnostics, save `/dev/shmem/nbt-aad.log`. Lines from the supervisor and
decoder carry the `[kombi-player]` prefix in this same file, including successful
sessions and failures. The daemon's logging-off setting, RAM limit and USB log
destination apply to these lines too. There is no separate player log to collect.
To roll back, Force
Stop Android Auto, restore the saved daemon, player, plugin directory and
environment from the printed backup, remount read-only and restart iDrive.
If the previous installation had no `kombi-player`, remove only the newly installed
`/opt/aa/kombi-player` during rollback.

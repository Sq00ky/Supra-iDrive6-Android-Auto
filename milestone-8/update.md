# Update Guide
**Note: Releases are in the release folder. The milestones are source code, designed to be built, and then deployed. For releases, see: https://github.com/Sq00ky/Supra-iDrive6-Android-Auto/releases**
## Overview
Already have Android Auto installed and want the newest version? Good news, updating is a lot quicker than installing. No TurboCode, no ZeroTier session with me (unless you want one!), just a flash drive and SSH. The whole process takes roughly 10-15 minutes.

This guide assumes you followed the [Installation Guide](install.md) before, so `/opt/aa/nbt-aad` and the plugin folder `/opt/hmi/ID5/data/ro/Plugins/NbtAad` already exist on your head unit.

### What are we doing?
We're swapping out two things for their newer versions:
1. The Android Auto Daemon (`nbt-aad`)
2. The iDrive6 plugin (the "Android Auto" entry in Media/Radio)

Your settings stay where they are. The settings file (`/opt/aa/nbt-aad.env`, if you have one) and the "Start with the car" checkbox are not touched by the update.

**The golden rule:** never copy a new file over one that's in use. The head unit can crash if you overwrite a running program. Instead, we *move* the old file out of the way (renaming it to `.old`), copy the new one in, and reboot. As a bonus, the `.old` copies are your undo button if something goes wrong.

### What you need
1. Flash drive formatted in FAT32/exFAT, with the new release on it:
   - `nbt-aad` in the root of the drive
   - the plugin files in a folder called `release` (`libNbtAad.so`, `NbtAad.manifest`, `NbtAad.xml`, `NbtAad_ALL_Texts.zip`, `NbtAad_ALL_Images.zip`)
2. The same SSH access you used during the install (OBD to ENET cable + laptop)

Plug the flash drive into the car's USB port before you start.

## Step 1: Connect and unlock the file system
Same as the install. Connect over SSH, switch the head unit to Read Write, and hop over to the host processor:
```bash
ssh root@169.254.199.99 -o HostKeyAlgorithms=+ssh-rsa -m hmac-sha1
# The password is ts&SK412
mount -o remount rw /net/hu-omap/fs/sda0
on -f /net/hu-omap/ /bin/sh
```

Next, find your flash drive's name. It shows up as `umass` followed by some numbers:
```bash
ls /mnt
```
Everywhere you see `umassXXXXX` below, use that name (for example `umass01200t7`).

## Step 2: Stop Android Auto
If Android Auto is running, stop it first. The easiest way is in iDrive: **Media/Radio → Android Auto → Force Stop**.

Or, from SSH:
```bash
pidin | grep nbt-aad
nbt-aad 19320
# NBT-AAD will be some random number, in this case 19320.
kill 19320
```
If `pidin | grep nbt-aad` prints nothing, it isn't running and you're good to go.

## Step 3: Update the Android Auto Daemon
```bash
cd /opt/aa/
mv /opt/aa/nbt-aad /opt/aa/nbt-aad.old
cp /mnt/umassXXXXX/nbt-aad /opt/aa/nbt-aad
chmod +x /opt/aa/nbt-aad
ls -l /opt/aa/
```
You should see both `nbt-aad` (the new one) and `nbt-aad.old` (the previous one) in the list.

## Step 4: Update the iDrive plugin
The plugin is loaded by iDrive itself, so it's *always* in use while the car is on. Instead of overwriting it, we move the whole old plugin folder out of the way and create a fresh one.

First, clear out the backup from your last update (if this is your first update, this will say the folder doesn't exist, which is fine):
```bash
rm -r /opt/aa/NbtAad.old
```
Then move the current plugin aside and copy in the new one:
```bash
mv /opt/hmi/ID5/data/ro/Plugins/NbtAad /opt/aa/NbtAad.old
mkdir -p /opt/hmi/ID5/data/ro/Plugins/NbtAad
cd /opt/hmi/ID5/data/ro/Plugins/NbtAad/
cp /mnt/umassXXXXX/release/* ./
ls -l /opt/hmi/ID5/data/ro/Plugins/NbtAad/
```
We're going to verify all five files copied over okay: `libNbtAad.so`, `NbtAad.manifest`, `NbtAad.xml`, `NbtAad_ALL_Texts.zip` and `NbtAad_ALL_Images.zip`.

The old plugin goes into `/opt/aa/NbtAad.old` on purpose. If it stayed inside the `Plugins` folder, iDrive could try to load it as a second app.

## Step 5: Reboot
Press and hold the power button for 30 seconds, and the head unit will reboot. This loads the new plugin and puts the file system back to Read Only.

## Step 6: Check it worked
Same as before: make sure your phone is connected via Bluetooth, head over to Media/Radio, and select "Android Auto".

Want to double check you're on the new version? Once Android Auto has started, run this over SSH:
```bash
grep Build /dev/shmem/nbt-aad.log
```
It prints the version of the Android Auto Daemon that's running, something like `Build: v1.8.6-cpgeom (...)`. For the plugin version:
```bash
grep create /dev/shmem/nbt-aad-app.log
```
which shows something like `create(name=NbtAad) rhmiapp-7 (...)`.

## Something went wrong? Roll back
The `.old` copies are exactly what you had before the update. To put them back, connect and unlock like in Step 1, stop Android Auto like in Step 2, then:
```bash
mv /opt/aa/nbt-aad /opt/aa/nbt-aad.new
mv /opt/aa/nbt-aad.old /opt/aa/nbt-aad
mv /opt/hmi/ID5/data/ro/Plugins/NbtAad /opt/aa/NbtAad.new
mv /opt/aa/NbtAad.old /opt/hmi/ID5/data/ro/Plugins/NbtAad
```
Then hold the power button for 30 seconds to reboot. Send me your log (see below) so we can figure out what happened!

## Grabbing a log for me
If something doesn't work, a log helps a ton. The log lives in memory and is **wiped on reboot**, so copy it to the flash drive *before* restarting:
```bash
cp /dev/shmem/nbt-aad.log /mnt/umassXXXXX/nbt-aad.log
cp /dev/shmem/nbt-aad-app.log /mnt/umassXXXXX/nbt-aad-app.log
```
Want a log that survives reboots? Add this line to `/opt/aa/nbt-aad.env` and it'll write straight to the flash drive every time Android Auto starts:
```
NBT_LOG_FILE=/mnt/umassXXXXX/nbt-aad.log
```

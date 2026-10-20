# Installing Project Droidintosh

**iOS 16.7.16 (20H392, iPhone X userland) on the Xiaomi Redmi Note 9 Pro**
(codename `joyeuse`, part of the `miatoll` family, Qualcomm SM7125 / Snapdragon 720G "Atoll").

Project Droidintosh boots Apple's open-source XNU kernel (xnu-8796, iOS 16
generation) on the stock Xiaomi bootloader, with the unmodified iOS 16.7.16
root filesystem living in the `userdata` partition. The sibling port for the
Redmi Note 10 Pro (`sweet`) is documented in [README.md](README.md); this file
covers installing on miatoll.

> **Experimental bring-up, not a daily phone OS.** Works: boot to the home
> screen, touchscreen (FocalTech FT8756), hardware buttons, all 8 CPU cores,
> display, Settings / SpringBoard / Control Center. Missing: Wi-Fi,
> Bluetooth, cellular, audio, camera, sensors, GPS, SEP/Face ID and any GPU
> rendering (CPU drawing, ~20–30 fps). Code signing and sandbox policies are
> largely disabled — do not put anything personal on it.

## What you need

| | |
| --- | --- |
| Phone | Redmi Note 9 Pro (`joyeuse`), **bootloader unlocked** |
| PC | Linux, with `fastboot`, `adb`, `python3`, `mkbootimg`, `aarch64-linux-gnu-gcc` |
| Files | The iOS root image (`ios16-root-miatoll.simg`) and a boot image (`shim-boot-*.img`) — build them below or take them from a working install |
| Optional | [GitHub account](https://github.com) for the kernel CI build; an OrangeFox recovery image for backups and log reading |

> **Warning — this erases Android user data.** iOS occupies the whole
> `userdata` partition. Back up first (step 0). The bootloaders, recovery,
> modem and persist partitions are **never** touched, so the phone is never
> bricked by this — but Android's data is gone once you flash.

## Install

### 0. Back up the phone

Boot OrangeFox temporarily and dump at least `persist`, `modemst1`,
`modemst2`, `fsg`, `fsc`, `boot`, `dtbo` and `vbmeta` — they carry
per-device calibration data. Keep the dumps off git.

```sh
fastboot boot recovery/recovery.img    # OrangeFox, temporary
adb shell                              # then dd the by-name partitions to /sdcard and adb pull
```

### 1. Get the iOS root image (one-time)

The root image is a 16 GiB case-sensitive journaled HFS+ volume built from
the iPhone X iOS 16.7.16 IPSW. It is **not** in git (Apple's files). Build it
with `apfs-fuse`, `hfsprogs` and sudo — full details in
[README.md](README.md) §2:

```sh
# OS.dmg SystemOS.dmg AppOS.dmg from the IPSW (names in BuildManifest.plist)
tools/mkiosroot.sh OS.dmg SystemOS.dmg AppOS.dmg ios16-root-miatoll.img 16G
sudo fsck.hfsplus -fy ios16-root-miatoll.img
sudo mount -t hfsplus -o loop,force ios16-root-miatoll.img /mnt/iosroot
sudo python3 tools/sweet-root-tweaks.py /mnt/iosroot     # skip Setup, activation seed, watchdogs off...
sudo umount /mnt/iosroot
img2simg ios16-root-miatoll.img ios16-root-miatoll.simg
rm ios16-root-miatoll.img                                # frees ~12 GB; keep only the .simg
```

### 2. Build the boot image (one-time)

```sh
# a) Kernel: CI on GitHub macOS runners (~8 min)
gh workflow run xnu.yml --ref miatoll -R YOUR/ios-miatoll
gh run download RUN_ID -R YOUR/ios-miatoll -n xnu-kernel -D xnu/out

# b) Device tree (skip if shim/out/miatoll.adt exists)
python3 tools/mkadt.py shim/out/miatoll.adt --device miatoll --highmem

# c) Payload: kernel + ADT + boot-args
python3 tools/mkpayload.py xnu/out/kernel.development.sweet shim/out/miatoll.adt \
    shim/out/payload.bin \
    --cmdline 'debug=0xc serial=3 cpus=8 keepsyms=1 -enable_kprintf_spam -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_hz=30 sweettouchdelay=5 panichalt=1 ufslogsecs=2 sweetfb_noconsole=1'

# d) Pack the boot.img (uses the stock miatoll DTB, dtb/miatoll_unpacked/dtb)
cd shim && DEVICE=miatoll ./build.sh ../shim/out/payload.bin && cd ..
cp shim/out/shim-boot.img shim/out/shim-boot-miatoll.img
```

Useful boot-arg changes: `cpus=8` is the full SoC (six A55 + two A76, proven);
`cpus=6` or `cpus=1` are the fallbacks. Everything else matters — see the
boot-arg table in [README.md](README.md).

### 3. Flash the root (destructive, one-time)

```sh
fastboot -S 512M flash userdata ios16-root-miatoll.simg
```

About 7–10 minutes for ~12 GB — do not unplug. Xiaomi's fastboot often hangs
after a large flash: if it does, hold **Power + Volume down** to re-enter
fastboot mode and continue with the next step.

### 4. Boot iOS

```sh
fastboot boot shim/out/shim-boot-miatoll.img
```

Nothing is written to the boot partitions — `fastboot boot` loads the image
into RAM only, so the stock Android bootloader stays in charge between
sessions. First boot takes ~4–5 minutes on the Apple logo, then the lock
screen appears; swipe up to unlock (touch works).

**Every subsequent iOS session starts with the same `fastboot boot` command**
(after any reboot the phone boots Android again). Get to fastboot mode from
Android/iOS with **Power + Volume down**.

### 5. Read the kernel log

```sh
fastboot boot recovery/recovery.img          # OrangeFox
tools/sweet-log-pull.sh logs/boot.txt        # 4 MiB console ring from the end of userdata
adb reboot bootloader
```

The log has the drivers' `SWEET ...` / touch lines and any crash backtraces
(`CRASH name ...`); symbolicate with `tools/sweet-crash.py`.

### 6. Going back to Android

The stock boot chain is intact, so after a **userdata reformat** (factory
reset / "Format data" in OrangeFox) Android boots normally again. Your old
Android data was already erased in step 3 — that is what the step-0 backups
are for.

## Troubleshooting

| Symptom | Fix |
| --- | --- |
| `fastboot getvar ...` hangs forever | This ABL never answers it — don't use it |
| "Sending 'boot.img'" never finishes | Ctrl+C, re-enter fastboot (Power + Vol down), retry — the link recovers |
| fastboot hangs right after the userdata flash | Known Xiaomi behaviour; re-enter fastboot, proceed to `fastboot boot` |
| Phone boots Android | The RAM boot was lost on a reboot — run `fastboot boot` again |
| Boots to a black screen / panic | Pull the log (step 5); try `cpus=1` in the cmdline as a fallback |
| Touch dead but GUI up | Kernel older than 0098 (FT8756/SPI driver) — rebuild from current `miatoll` |
| Want the console text on screen | Drop `sweetfb_noconsole=1` from the cmdline |

## More

- [README.md](README.md) — how it boots, patch series, hardware facts, full build background
- [NOTES.md](NOTES.md) — the engineering log: every boot, every decision, current status

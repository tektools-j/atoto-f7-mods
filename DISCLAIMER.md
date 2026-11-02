# DISCLAIMER — read before doing anything

## Safety / risk

- **This can brick your head unit.** Modifying firmware is inherently risky. During this
  project the unit boot-looped **twice** before the repack pipeline was fixed, and was
  only recoverable because a clean factory image was on hand.
- **It will likely void your warranty** and may violate the terms of sale/service.
- Keep a **verified clean factory/recovery image** for your exact model before you start,
  and know how to enter your unit's firmware-recovery (forced-ISP) mode.
- Everything here is provided **as-is, with no warranty**. You are solely responsible for
  what you do to your own device. The authors accept no liability for damage, data loss,
  bricked units, or anything else.

## Legal / what this repo deliberately does NOT contain

- **No ATOTO firmware, libraries, resources, or images**, and **no vehicle boot logo.**
  This repo is only original research notes, byte offsets, and tooling.
- You apply the documented changes to firmware **you already have on a device you own**.
  The patch tooling asserts the stock library checksum and refuses to run otherwise.
- **Do not redistribute the vendor's firmware or binaries.** They are copyrighted by
  their owner. Share the *method and a patch*, not their files.
- ATOTO, Sunplus/SunMedia, and any vehicle-manufacturer names, logos, and trademarks are
  the property of their respective owners. This project is **independent, unofficial, and
  not affiliated with or endorsed by any of them.**
- Modifying/repairing a device you own is broadly permitted in many jurisdictions, and
  reverse engineering for interoperability has specific legal recognition in some (e.g.
  US DMCA §1201(f)). Laws vary by country and change over time. **This is not legal
  advice** — if you intend to distribute tools at scale or receive any legal notice,
  consult a qualified lawyer.

## Credentials / secrets

- Nothing here contains Wi-Fi passwords, keys, or other secrets, and you shouldn't add
  any when you publish. The debug shell uses **your own unit's hotspot** — find its SSID
  and password in the unit's Wi-Fi settings; never commit them.

# iOSMirror & the YouTube-video limitation (iPhone)

A field note for anyone trying to get **iPhone** video — especially the **YouTube app** —
onto this unit's screen. Short version: **the stereo is not the problem, and no
firmware mod or custom app fixes it.** The limit is enforced on the iPhone, by the
YouTube app. The one route that works today is **YouTube in a browser (Safari),
mirrored.**

> This is observed behavior on an ATOTO F7-series unit (`SOC F7-SOC5P-230609`), iPhone
> on current iOS, diagnosed live over the `:2323` debug shell (see
> [README §1](README.md#1-root-shell-via-usb-debug-stick)). Treat specifics as a worked
> example; the mechanism is general to these "iOSMirror"/"Zlink"-style receivers.

## The two iPhone paths on this unit

- **CarPlay** — plugin `libcarplay.so` ("AppleCarPlay"), iAP2/MFi. The board carries a
  genuine **MFi authentication coprocessor** (device node `mfi_iap_carplay` on i2c-0),
  so the finished product passed Apple's MFi certification. **Who the MFi licensee is
  is unknown from the firmware** — it is whoever ships/certifies the head unit (the
  brand/ODM), **not** necessarily the SoC vendor. Do not assume "Sunplus licensed
  CarPlay from Apple"; the firmware does not say that.
- **iOSMirror** — plugin `libmirrorlink.so` ("iOSMirror v2.0"), engine
  `libspiosmirror.so`. On this unit it is an **AirPlay receiver** carried over the
  unit's own Wi-Fi hotspot: `_airplay._tcp` advertised on TCP **:7000** (RTSP),
  `srcvers=365.10.3`, control port `192.168.0.1:8765`, mDNS on `:5353`. It shares the
  **one** AirPlay receiver with CarPlay. Apple does **not** broadly license AirPlay
  *mirroring-receiver* functionality to aftermarket car gear, so this is almost
  certainly an **unofficial / reverse-engineered AirPlay receiver**, not an
  Apple-licensed component.

## Why the YouTube app shows a black (and silent) screen

AirPlay has **two different modes**, and YouTube treats them oppositely:

1. **AirPlay *streaming* (casting)** — the in-app AirPlay/cast button. The app hands the
   receiver a *stream reference*; the receiver fetches and plays the video itself (like
   Chromecast). This is how YouTube plays to an **Apple TV** or a certified AirPlay TV.
   YouTube fully supports it.
2. **AirPlay *screen mirroring*** — Control Center → Screen Mirroring. The phone streams
   a live copy of the whole display. **YouTube deliberately blanks its own video during
   screen mirroring** (it detects iOS's screen-capture signal and withholds the frame,
   steering you toward mode 1 instead).

This unit's iOSMirror is a **mirroring** receiver (mode 2) — so the YouTube app blanks.
And mode 1 is **not available** either: the receiver advertises itself as a *mirroring*
target (it even names itself "CarPlay"), not as a certified AirPlay **video** target, so
**it does not appear in the YouTube app's in-app cast picker** (confirmed: it does not
show up there). Both iPhone→unit video routes therefore dead-end for the YouTube app.

The block lives in the YouTube app and keys off the mirroring/screen-capture state — so
it is identical regardless of the receiver. A cleaner or "better" receiver would hit the
exact same black screen.

## What does NOT fix it

- **A firmware mod / repacked plugin.** Nothing on the unit changes the iPhone-side
  decision. (Reimplementing a full AirPlay *video* receiver so YouTube's cast picker
  trusts the unit is a large reverse-engineering project with low odds YouTube accepts
  it, and it solves a problem the browser route already solves. Not worth it.)
- **A custom iPhone app.** iOS does not let a third-party app stream arbitrary
  screen/video to an accessory the way Android's AOA does; and the block is YouTube's,
  not the OS's.
- **Disabling CarPlay to "free up" the mirror.** On this unit CarPlay and iOSMirror
  share the one AirPlay receiver, so a wired CarPlay auto-launch can grab `:7000` and
  lock mirroring out — but disabling CarPlay via Screen Time then makes iOS *hide* the
  (CarPlay-identified) target from Control Center → Screen Mirroring. Leave CarPlay
  **allowed but not auto-launched** (Wi-Fi only, no cable) to reach the mirror target.

## What works (do this)

Watch **YouTube in a browser**, mirrored — browsers don't run the anti-mirror blanking:

1. **Reach the mirror:** CarPlay allowed-but-not-auto-launched; iPhone on the F7 hotspot;
   no cable; Control Center → Screen Mirroring → pick the unit.
2. **Open the TV interface:** Safari → `youtube.com/tv` (YouTube's big-screen "leanback"
   UI — large targets, simple navigation, ideal for a dashboard). `m.youtube.com` or the
   desktop site also work.
3. **Make it app-like:** Safari → Share → **Add to Home Screen**. Launched from that
   icon it opens fullscreen with no Safari chrome — looks and behaves like an app, and
   still mirrors.
4. **Optional:** another browser (Brave/Chrome) renders YouTube in a web view too (no
   blanking) if you prefer its player; `youtube.com/embed/<VIDEO_ID>?autoplay=1` gives a
   bare chrome-free player for a single video.

## Debug-shell safety note

`logcat` on this box **reboots the unit** (watchdog) — never run it while diagnosing the
mirror stack. `dmesg` / `netstat` / `ps` / `ifconfig` are safe.

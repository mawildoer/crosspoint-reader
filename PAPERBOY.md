# Paperboy firmware

This is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), based on upstream `develop` at `3c42dfd`, with Paperboy article delivery for Xteink X3. It is independent of the upstream project. Upstream licensing and attribution are preserved.

The phone fetches content using its own subscriptions and sends EPUB files over an encrypted, bonded Bluetooth LE connection. The reader stores them on its SD card and opens them with CrossPoint's existing EPUB reader. The companion iPhone app is maintained separately; its source is not included in this firmware fork.

## Added behavior

- An Articles entry on Home shows the newest 60 delivered articles.
- Phone sync runs on Home and Articles, and briefly before sleep. It stops while reading to release RAM.
- Articles use the normal reading-position files and recent-books list. Opening an article does not mark it finished. Completion requires the final page of a fully generated final section or the end-of-book screen.
- X3 stays awake while USB power is detected and starts a fresh idle timeout after unplugging. A USB-powered cold boot continues into the reader. The power button still permits manual sleep.
- Transfers use bounded buffering, byte acknowledgements, and whole-file CRC-32 verification. The [protocol](docs/paperboy-protocol.md) documents the wire format.

Phone-side retention determines which articles are removed during a sync. A finished article may be removed unless locked on the phone; unfinished loaded articles remain available for resuming.

## Build

Follow upstream's [toolchain setup](docs/contributing/getting-started.md). The tested target for this fork is:

```sh
pio run -e default
```

Run the host storage regression checks with `test/paperboy_host/run.sh`. They cover short-write rollback, read-state lookup errors, index preservation after a read error, and the transfer CRC test vector.

This configuration replaces the Bluetooth remote page-turner role with the phone-sync peripheral role. Other device profiles have not been validated with these changes.

## Verification and limits

The X3 build compiles and has been flashed with esptool hash verification. Encrypted EPUB transfer, persisted article listings, and phone/reader sync have been exercised. The latest USB and completion changes compile and boot; manual confirmation of resume across sleep/reboot, inactivity past the configured timeout, and wake on cable insertion is still needed.

At full charge, USB detection is reliable only with an active computer host. A wall charger, power bank, or sleeping computer may not be detected. The cold-boot behavior does not guarantee that cable insertion wakes every deep-sleep state.

This branch is development firmware, not an upstream release or endorsement. No subscription credentials, device backups, or built firmware images are included.

## Pairing

Open Articles on the reader, then connect from Paperboy on the phone and accept the system pairing prompt. Keep Articles open when pairing another device. The **Pair a new phone** row clears the reader's stored bonds, including when no articles are present. If iOS reports a removed pairing, forget Paperboy X3 in iOS Bluetooth settings before pairing again.

OPDS and enabled Plugins remain accessible through **Home → File Transfer**.

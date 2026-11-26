# Paperboy firmware

This is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), based on upstream `develop` at `3c42dfd`, with Paperboy article delivery for Xteink X3. It is independent of the upstream project. Upstream licensing and attribution are preserved.

The phone fetches content using its own subscriptions and sends EPUB files over an encrypted, bonded Bluetooth LE connection. The reader stores them on its SD card and opens them with CrossPoint's existing EPUB reader. The companion iPhone app is maintained separately; its source is not included in this firmware fork.

## Added behavior

- Home opens the unified Library. Books and articles share its Added, Published, Title and Author views; articles have a paper icon, author/source subtitle and publication-date value. There is no separate Articles destination.
- Firmware can be updated from the phone over the bonded Bluetooth link after one USB bootstrap installation. The reader stages and validates the image on SD, requires USB power or at least 30% battery, writes the inactive OTA slot, and restarts in Library for version verification.
- Phone sync runs in the Library and briefly before sleep. The list refreshes after article transfers settle. It stops while reading to release RAM.
- Articles use the normal reading-position files and recent-books list. Opening an article does not mark it finished. Completion requires the final page of a fully generated final section or the end-of-book screen.
- X3 stays awake while USB power is detected and starts a fresh idle timeout after unplugging. A USB-powered cold boot continues into the reader. The power button still permits manual sleep.
- Transfers use bounded buffering, byte acknowledgements, and whole-file CRC-32 verification. The [protocol](docs/paperboy-protocol.md) documents the wire format.

Phone-side retention determines which articles are removed during a sync. A finished article may be removed unless locked on the phone; unfinished loaded articles remain available for resuming.

## Build

Follow upstream's [toolchain setup](docs/contributing/getting-started.md). The tested target for this fork is:

```sh
pio run -e default
```

Run the host storage regression checks with `test/paperboy_host/run.sh`. They cover short-write rollback, read-state lookup errors, index preservation after a read error, the transfer CRC test vector, article metadata/date formatting, and Library refresh timing across clock wrap. The native `LibraryIndexFileTest` also covers article-folder lookup on a 4,096-folder index.

This configuration replaces the Bluetooth remote page-turner role with the phone-sync peripheral role. Other device profiles have not been validated with these changes.

## CI and firmware releases

The fork's **Paperboy firmware** workflow (`.github/workflows/paperboy.yml`) runs on `feat/paperboy`, pull requests targeting it, manual runs, and tags beginning with `paperboy-v`. It runs the article-storage and library-index/format tests, builds the `gh_release` X3/X4 profile, and keeps a downloadable `paperboy-x3-firmware` artifact for 30 days. Only the X3 hardware has been checked.

Publishing a tag such as `paperboy-v0.1.0` creates a GitHub release after all checks and the build pass. It attaches `paperboy-x3-update.bin`, license notices/source references, `BUILD.txt`, and SHA-256 checksums. The binary is an application update written at `0x10000`; it does not include the bootloader or partition table. Normal branch pushes produce artifacts without creating releases. The release job alone receives repository write permission, and no Apple signing secrets are needed in the firmware repository.

The companion app has its own **Publish TestFlight** workflow in [mawildoer/paperboy](https://github.com/mawildoer/paperboy). App tags use `v…`; firmware tags use `paperboy-v…` so their release versions are independent.

## Verification and limits

The X3 build compiles and has been flashed with esptool hash verification. Encrypted EPUB transfer, persisted article listings, and phone/reader sync have been exercised. The latest USB and completion changes compile and boot; manual confirmation of resume across sleep/reboot, inactivity past the configured timeout, and wake on cable insertion is still needed.

The unified Library update builds for `default` and passes 8 index-file tests, 18 index-format tests, and the host storage/metadata/refresh checks. Four independent reviews covered correctness, architecture, embedded resources, and documentation; their verified findings were fixed. The installed application's flash hash was verified. Its serial log shows entry into Library and reconnection with one existing bond over an encrypted link. A captured framebuffer shows paper icons, author/source subtitles, publication dates, and the Menu button hint. Physical button handling and the e-ink panel still need human confirmation.

Article metadata storage belongs to the Library activity and grows only to its visible row window. The activity streams metadata from the article index instead of retaining every article's details in RAM. A quiet-period refresh briefly disconnects Bluetooth to free memory for index reconciliation; it preserves the selected document by path and then allows reconnection. The article-folder ID is resolved once after each index rebuild, keeping folder-table scans out of row rendering.

On the X3, check Back from the rows to the tabs and then Menu, holding Back from a row, returning from Settings/File Transfer, and resuming a partially read article. During phone sync, the list should refresh after transfers settle without opening a different row. After deleting a test book that empties a folder, article icons and details should remain correct. Wake from a normal sleep image with fading fix enabled should leave no retained sleep-image pixels in the Library.

At full charge, USB detection is reliable only with an active computer host. A wall charger, power bank, or sleeping computer may not be detected. The cold-boot behavior does not guarantee that cable insertion wakes every deep-sleep state.

This branch is development firmware, not an upstream release or endorsement. No subscription credentials, device backups, or built firmware images are included.

## Pairing

Open the Library on the reader, then connect from Paperboy on the phone and accept the system pairing prompt. Use **Menu → Pair a new phone** when replacing the paired device. This action clears the reader's stored bonds, including when the Library is empty. If iOS reports a removed pairing, forget Paperboy X3 in iOS Bluetooth settings before pairing again.

From the Library, Back at the tab strip opens Menu; holding Back opens it from any list position. On touch devices the leading header control opens it. Menu contains Settings, Browse Files, File Transfer, Refresh Library and Pair a New Phone. OPDS and enabled Plugins remain accessible through **Menu → File Transfer**.

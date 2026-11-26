# Paperboy firmware

This is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), based on upstream `develop` at `3c42dfd`, with Paperboy article delivery for Xteink X3. It is independent of the upstream project. Upstream licensing and attribution are preserved.

The phone fetches content using its own subscriptions and sends EPUB files over an encrypted, bonded Bluetooth LE connection. The reader stores them on its SD card and opens them with CrossPoint's existing EPUB reader. The companion iPhone app is maintained separately; its source is not included in this firmware fork.

## Added behavior

- The default sleep screen uses the Paperboy artwork and wordmark. Custom and cover sleep-screen modes retain their selected behavior.
- Home opens the unified Library. Books and articles share its Touched, Added and Published views (all newest first); articles have a paper icon, author/source subtitle and publication-date value. There is no separate Articles destination.
- Firmware can be updated from the phone over the bonded Bluetooth link after one USB bootstrap installation. The reader stages and validates the image on SD, requires USB power or at least 30% battery, writes the inactive OTA slot, and restarts in Library for version verification.
- A phone Bump command changes only an article’s Touched position. Opening books and articles also updates Touched, without changing arrival/publication dates or progress.
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

### Paperboy 0.3.0: Touched library and Bump

Library now exposes exactly Touched, Added, and Published, newest first. Touched
is the default. Opening a document or receiving a phone Bump moves it in Touched;
Added, Published, and reading progress are unchanged. Untouched documents and
ties follow Added order. The phone queues bumps until this protocol-2 firmware
is installed; protocol-1 phones can still sync articles.

The reader owns touch history in checked 16-byte records under
`/.crosspoint/library-touches`, sharded by path hash. A persistent counter gives
local opens and phone bumps one ordering without relying on a reader clock.
Records follow shared rename/move transactions and are removed with their
document. The index stores a Touched permutation; rebuilding needs a checked
two-byte Added rank per book and reuses four-byte timestamp scratch for each
date order. At 4,096 books these arrays total 24 KiB, released after rebuilding.
There is no resident touch-history cache. This adds small SD writes when opening
documents and an SD read per document during rebuilds; physical SD latency and
heap behavior still require hardware checks.

Validation: 86 library/format/move tests and the article/touch-store host checks
passed, including short writes, interrupted renames, idempotent bumps, rollback,
and a 4,096-document sharding corpus. Four independent read-only reviews covered
correctness, architecture, embedded resources, and i18n/docs, with one follow-up
round. Recovery, shared move ownership, and directory layout findings were fixed
and reviewed; the final stale documentation phrase was corrected. No verified
finding remains. The logo is generated from the companion app's Paperboy artwork.
Final `default` and `gh_release` builds passed. The release version is `0.3.0-paperboy`;
formatting, diff whitespace checks, and workflow validation passed. The existing
NimBLE service-start deprecation warning remains. The SDK stays pinned at
`3f3e4c1`. The requested behavior and fork ownership follow the human's three-view,
Bump, and branding requirements; the requested delivery route is TestFlight then
Bluetooth, with no upstream PR.

Both branch CI and [release CI](https://github.com/mawildoer/crosspoint-reader/actions/runs/45911582421)
passed for `77d25af`. The latest-release API selects `paperboy-v0.3.0`. Downloaded
assets matched `SHA256SUMS`; esptool verified the ESP32-C3 image checksum and
validation hash. The 5,532,640-byte update fits the 0x640000-byte OTA slot and
contains the configured runtime version `0.3.0-paperboy` and new touch-store/UI
markers. Binary SHA-256:
`2891b36182953390c7ece07dac71da378874a1fb179ed233476419fe89c9d014`.
The embedded ESP-IDF descriptor reports older build metadata
(`f021681`); the runtime HELLO/About version comes from `CROSSPOINT_VERSION`,
not that descriptor. CI compiled the changed firmware sources for this release.

Install through TestFlight and then the app's Bluetooth firmware updater. On the
X3, check that the app reconnects and reports `0.3.0-paperboy`; interrupted
transfers must not report installation success. Verify the three tabs in portrait,
inverted portrait, and both landscape orientations, including any button remapping.
Read part of an article, return to Library, bump a different loaded article, and
confirm the Touched order changes while Added/Published and both reading
positions remain unchanged. Repeat a bump after disconnect/reconnect, then rename
or move a test document and confirm its touch position follows it. Sleep with the
default mode to see Paperboy; custom/cover modes should retain their selection.
Check repeated reader/Library transitions for leaks, more than 50 KiB free C3
heap, the largest free block, and acceptable rebuild latency. These are pending
human hardware checks for this release; host results do not establish them.

## Pairing

Open the Library on the reader, then connect from Paperboy on the phone and accept the system pairing prompt. Use **Menu → Pair a new phone** when replacing the paired device. This action clears the reader's stored bonds, including when the Library is empty. If iOS reports a removed pairing, forget Paperboy X3 in iOS Bluetooth settings before pairing again.

From the Library, Back at the tab strip opens Menu; holding Back opens it from any list position. On touch devices the leading header control opens it. Menu contains Settings, Browse Files, File Transfer, Refresh Library and Pair a New Phone. OPDS and enabled Plugins remain accessible through **Menu → File Transfer**.

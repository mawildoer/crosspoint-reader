# Paperboy firmware

This is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), based on upstream `develop` at `3c42dfd`, with Paperboy article delivery for Xteink X3. It is independent of the upstream project. Upstream licensing and attribution are preserved.

The phone fetches content using its own subscriptions and sends EPUB files over an encrypted, bonded Bluetooth LE connection. The reader stores them on its SD card and opens them with CrossPoint's existing EPUB reader. The companion iPhone app is maintained separately; its source is not included in this firmware fork.

## Added behavior

- The default sleep screen uses an upright Paperboy mark, about two-thirds of the viewable screen width, with dark artwork and text directly on white. The former Dark setting is called Paperboy; Light remains a compatible alias. Custom and cover sleep-screen modes retain their selected behavior.
- Home opens the unified Library. Books and articles share its Touched, Added and Published views (all newest first); articles have a paper icon, author/source subtitle and the last touch time in Touched or publication date in the other views. There is no separate Articles destination.
- Firmware can be updated from the phone over the bonded Bluetooth link after one USB bootstrap installation. The reader stages and validates the image on SD, requires USB power or at least 30% battery, writes the inactive OTA slot, and restarts in Library for version verification.
- Protocol 3 retains firmware checkpoints on SD between sync windows. A matching image continues from its saved prefix; a different release starts a new stage. The full actual file is checked before flashing, and article sync/reading remain available between windows.
- A phone Bump command moves the article to the top of Touched and records when the reader applied it. Opening books and articles also updates Touched, without changing arrival/publication dates or progress.
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

The reader owns touch history in checked records (16 bytes in 0.3.0, 20 bytes with a touch timestamp from 0.4.1) under
`/.crosspoint/library-touches`, sharded by path hash. A persistent counter gives
local opens and phone bumps one ordering without relying on a reader clock.
Records follow shared rename/move transactions and are removed with their
document. The index stores a Touched permutation; rebuilding needs a checked
two-byte Added rank per book and reuses four-byte timestamp scratch for each
date order. At 4,096 books these arrays total 24 KiB, released after rebuilding.
Only the current window of article rows caches touch times while the Library is open. This adds small SD writes when opening
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

### Paperboy 0.3.1: light sleep artwork

The human requested an upright mark around two-thirds of the screen width, then
a light background with only artwork and text dark. The built-in screen now
draws a 384-pixel source in logical coordinates through the renderer, removing
the physical-blit rotation error. It scales to two-thirds of the viewable width,
shrinking only when needed to leave room for both text lines. The generator
removes the icon's dark matte and makes a dark outline and details on white.
Legacy mode IDs remain unchanged: former Dark is labeled Paperboy, and Light is
a compatible alias. Other sleep modes retain their selection and routing.

The 18,432-byte asset is immutable flash data (ELF symbol type `r`), replacing the
1,800-byte version. No bitmap buffer or persistent state is allocated. Static
RAM remains 64,760 bytes for `default` and 64,736 bytes for `gh_release`.
Both final targets passed; the release uses 5,534,633 bytes of its 6,553,600-byte
flash budget. Formatting and whitespace checks passed. A host diagnostic ran
the actual default-screen body with the actual coordinate/dimension/inset code
for 12 panel/orientation cases; white corners and text bounds passed. The preview
uses a substitute host font. Asset generation is deterministic. Four independent
read-only reviews found no correctness, architecture, embedded, or i18n/docs
issues. SDK revision remains `3f3e4c1`.

Hardware check after Bluetooth installation: confirm `0.3.1-paperboy`, then sleep
from Library and from each reader orientation. The built-in sleep screen retains
its normal portrait presentation: its P must be upright relative to the labels,
approximately two-thirds of the width, on white without a rectangular matte.
Check the Paperboy and Light selections, wake without retained artwork, and
confirm a selected cover/custom/quick-resume screen still behaves normally.
Physical panel appearance remains a human check; host rendering is not a pass
for ghosting or refresh quality.

### Paperboy 0.4.0: firmware checkpoints

The phone can prepare updates after article sync, stage a bounded part in a short
window, and resume it at the next connection. The current image keeps running
until a longer window verifies the whole actual SD file, writes the inactive
slot, and reboots. The paired phone confirms the changed version. Old phones
still negotiate protocol 1 or 2 and use the existing whole-transfer path.

The reader owns one staging image and a checked 56-byte identity/checkpoint plus
temp/backup records under `/.crosspoint`. The image identity includes its size,
whole CRC32 and SHA-256. Data closes before a checkpoint is published. Resume
reads the bounded checkpoint and truncates any uncheckpointed tail; it does not
rescan the growing file every window. The phone checks the prefix CRC when
resuming and pausing. End streams the complete SD file through a 128-byte buffer
before the existing board/image validation and A/B installation. No whole-image
MCU allocation or resident history cache is added. Files and state transitions
belong to the main-loop PhoneSync receiver through `HalStorage`.

Main-agent host checks cover checkpoint continuation, different identities,
uncheckpointed tails, short writes, interrupted metadata publication/recovery,
read errors and actual-file CRC corruption. Companion tests cover repeated short
windows, simulated reader restarts, sending only remaining bytes, full staging
without premature commit, bad prefixes/offsets, expiration and confirmation-record
write failures. Protocol 3 phones retain basic sync with protocol 1/2 readers.

Human hardware checks: install this firmware once over Bluetooth in a complete
window (older firmware cannot resume its own capability upgrade). On a subsequent
different-image update, interrupt after some bytes, reconnect in Library, and
confirm progress continues above zero while articles and partial reading positions
remain available. Repeat across reader sleep/reboot and leaving Library. Turn
automatic updates off during staging, then on again; the prefix should remain.
A different image must start a fresh stage. A fully staged image must not reboot
until validation/commit in a long window. Confirm the expected changed version
after reboot and check repeated interruptions for stable C3 heap/largest block
and responsive controls. These physical checks remain pending.

Final `default` and `gh_release` builds passed after review fixes. Static RAM is
unchanged at 64,760 and 64,736 bytes respectively; release flash use is 5,537,491
of 6,553,600 bytes (84.5%). The SDK stays at `3f3e4c1`. Four independent reviews
and one targeted follow-up covered the reader/app contract; all verified findings
were resolved. Real frame-handler host checks now exercise final checkpoint short
writes/rename failures, validated pause followed by actual article upload, and a
declined low-battery commit followed by resume and accepted commit. 170 companion
Swift tests, Release app/extension and Mac CLI builds, share registration, and
settings migration/persistence checks passed. No new runtime allocation owns a
full firmware image on the reader; final CRC verification uses a 128-byte buffer.
The SD-stage/A/B strategy follows the human's requested design in this fork.

Source `2956d36` passed branch and [release CI](https://github.com/mawildoer/crosspoint-reader/actions/runs/45919202303).
The published [0.4.0 release](https://github.com/mawildoer/crosspoint-reader/releases/tag/paperboy-v0.4.0)
is selected by the updater's latest-release API. Downloaded assets matched their
SHA-256 sums; esptool validated the image checksum and hash. The 5,551,696-byte
ESP32-C3 binary fits the OTA slot and contains the exact 18,432-byte logo, runtime
`0.4.0-paperboy` marker and checkpoint metadata path. BUILD.txt matches the source
commit. Binary SHA-256:
`ed08ef8e9a9fe2e12792ba3c5ee49fdb4ef8b5f7748b6e6c7b014ef64cec5580`.
The companion's actual download/cache code also verified this live release.
TestFlight build 108.1 passed Apple processing and internal-test availability.

## Pairing

Open the Library on the reader, then connect from Paperboy on the phone and accept the system pairing prompt. Use **Menu → Pair a new phone** when replacing the paired device. This action clears the reader's stored bonds, including when the Library is empty. If iOS reports a removed pairing, forget Paperboy X3 in iOS Bluetooth settings before pairing again.

From the Library, Back at the tab strip opens Menu; holding Back opens it from any list position. On touch devices the leading header control opens it. Menu contains Settings, Browse Files, File Transfer, Refresh Library and Pair a New Phone. OPDS and enabled Plugins remain accessible through **Menu → File Transfer**.

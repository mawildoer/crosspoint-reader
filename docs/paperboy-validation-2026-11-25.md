# Paperboy validation — 2026-11-25

User requirements: Medium uses browser sharing or the signed-in Following page;
Library has Added, Published, Title, Author; one USB bootstrap followed by an
actual Bluetooth firmware update from the USB-installed developer iPhone app.
The user requested their own firmware fork and CI publication earlier in this
session and explicitly authorized overwriting firmware and the two update steps
after the existing two-slot updater, battery gate, and validation were explained.
Development phone installation takes priority over waiting for TestFlight.

## Behavior and ownership

Added uses arrival Unix seconds for new articles and normalized FAT timestamps
for books/legacy articles, with discovery order when timestamps are unavailable.
Published uses full publication seconds; undated books sort last in descending
order. Title and Author retain grouping. Opening a document no longer overrides
these orders. Progress files and article contents are unchanged.

Library rebuild owns a temporary checked timestamp snapshot (16 bytes/article,
up to 64 KiB at 4096 entries). Failure retains the previous index. UI rendering
still retains only visible rows. The index adds a publication permutation and a
TZ-rule fingerprint; stale indexes reconcile without losing firstSeen history.

Firmware transfer reuses the existing receiver and encrypted bonded link. The
phone's explicit update action trusts this fork's HTTPS GitHub release and digest.
The reader requires USB or 30% battery, stages on SD, verifies CRC and full image
integrity/board, then uses the existing flasher for the inactive slot. Accepted
commit is followed by reboot into Library and a changed-version check. The old
slot is preserved; automatic rollback of a later crashing image is not provided.
These downstream behaviors and the release channel remain maintained in the
user's mawildoer fork.

## Recorded checks

- All 59 library format, file, builder and name-filter host tests passed.
- Article-store fault tests passed, including legacy TSV rows, snapshot read and
  allocation failures, 512 lookups without further SD reads, and exact even-tick/
  wrapping commit grace periods. Snapshot and publication allocations are checked.
- Required clang-format wrapper passed. SDK pin remained
  `3f3e4c1126e2cb9a278c2c1076fb4a3334ffdc30`.
- Four independent read-only reviews covered correctness, architecture, embedded
  behavior and i18n/docs, followed by one targeted follow-up each. All verified
  findings were fixed. Final follow-up fixes were timezone freshness/fast-path
  invalidation (new passing regression), receiver-close-before-unlink ordering,
  and one stale guide label. No third review round was run.
- Final `default` and `gh_release` X3 builds passed. Release image:
  5,530,768 bytes, version `0.2.0-paperboy`.
- Full 16 MiB pre-update flash backup is ignored locally under
  `.pio/paperboy-backups/`; it contains private device state and is not published.

## Observed device execution

The authorized USB bootstrap wrote only app0 at `0x10000`; boot selection,
Bluetooth bonds, and SD data were preserved. The new Library rebuilt three
entries in 163 ms, and the phone reconnected encrypted with the existing bond.

The USB-installed Debug iPhone app then transferred 5,530,768 bytes over Bluetooth.
Reader serial output recorded validation OK, staging status 0, destination app1
at `0x650000`, boot sequence changing from slot0/sequence1 to slot1/sequence2,
and firmware install OK. The phone recorded:

    22:27:49 Firmware update verified after reboot: 0.2.0-paperboy
    22:27:52 HELLO_ACK v1 ... fw="0.2.0-paperboy"
    22:27:52 Sync: up to date

Active-BLE free heap remained above 73,000 bytes, largest block 69,620 bytes. These are
serial/protocol observations, not a claim that all human hardware QA was done.
Full local logs are `/tmp/paperboy-bluetooth-update.log` and
`/tmp/paperboy-phone-bluetooth-update.log`; they are not committed.

## Remaining human QA

Check the four sorts, direction toggles, grouping and long-press menus in all four
orientations and with remapped buttons. Confirm unfinished article resume.
Exercise navigation/manual-sleep cancellation, battery-only near-idle updates,
power loss and SD errors, and check SD allocation after repeated cancellations.
The completed device run covers the successful update and reconnect path;
it does not establish every interruption or orientation behavior. No upstream
PR or public App Store submission is part of this work.

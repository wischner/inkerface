# 10. Storage on the device

Chapter 9 used storage through its API. This chapter covers what happens
underneath on the Xteink X4: where the bytes go, what each operation
costs in power and flash wear, and the limits you design around.

> **Status:** the X4 storage backend builds and links against the real
> LittleFS and SdFat libraries (the chapter 9 example builds for the X4),
> but has not yet run on hardware. Mounting, formatting and card behavior
> below follow the libraries' documented behavior and should be
> confirmed on a device.

## What is under each volume

| | `volume::internal` | `volume::card` |
| --- | --- | --- |
| Hardware | the X4's 16 MB flash chip | microSD card on the SPI bus, shared with the display |
| Filesystem | LittleFS | FAT32 or exFAT, through SdFat |
| Reached through | ESP-IDF's POSIX layer, mounted at `/littlefs` | FreeInk's `SDCardManager` |
| Size | about 9.9 MB (the data partition) | the card |
| Power loss while writing | safe: LittleFS never leaves a half-written file system | FAT can lose the file being written |
| Mounted | on first use | on first use; retried later if no card was found |

The desktop build has the same two volumes as folders (chapter 9), so the
same code and the same tests run on both targets.

## The flash layout

`partitions-x4.csv` divides the 16 MB flash:

```text
0x000000  bootloader, partition table
0x009000  nvs        20 KB   Wi-Fi and system settings (ESP-IDF)
0x00E000  otadata     8 KB   which firmware slot is active
0x010000  app0        3 MB   firmware slot A
0x310000  app1        3 MB   firmware slot B (safe updates, e.g. from the SD card)
0x610000  spiffs    9.9 MB   LittleFS: volume::internal
0xFF0000  coredump   64 KB   crash dumps
```

The data partition is named `spiffs` only because that is the name
Arduino's LittleFS support looks for. The filesystem on it is LittleFS.

The first mount of an empty partition **formats** it. That takes a moment
on the first boot and erases nothing that matters, because a valid
LittleFS volume is never reformatted.

**Changing the layout** (for example, a larger firmware slot): edit
`partitions-x4.csv`. Keep each partition aligned to 0x10000 (64 KB),
make the sizes add up to the end of the flash (0x1000000), and flash over
USB. Moving or resizing the data partition erases the internal volume.

**Putting files on the internal volume at build time:** PlatformIO's
`pio run -e x4 -t uploadfs` writes the contents of the project's `data/`
folder into the partition. This replaces everything stored there.

## Cost of each operation

| Operation | Internal flash | SD card |
| --- | --- | --- |
| `storage` construction | nothing | nothing |
| first use of a volume | mount: reads a few blocks | wakes and initializes the card: the most expensive step |
| `available()` | mounts once; free afterwards | the same; retries while no card is found |
| `size()`, `position()` | free (tracked in memory) | free |
| `read()` | flash read through LittleFS's small cache | SPI transfer of whole 512-byte sectors |
| `write()` | erases and writes 4 KB blocks, which wears the flash | sector writes |
| `close()` after writing | commits the file | updates the directory entry and FAT |
| `info()`, `next()` | metadata reads | metadata reads; one small file open per entry |
| `free_space()` | cheap | a FAT scan, cached by FreeInk for 20 seconds |

### Saving power

- **Don't touch the card until you need it.** Mounting it is the most
  expensive storage step, and a program that never uses a volume never
  powers it up. The X4 has no separate switch for the card's power; it
  goes off only when the whole device powers off.
- **Read and write in blocks of 512 bytes or more.** Every call goes
  straight to the filesystem: there is no buffer in between, which
  saves RAM, but small calls are paid for one by one.
- **Batch writes, and write when there is something to save:** on leaving
  a screen or turning a chapter, not on every page turn.
- **Ask `free_space()` sparingly**, especially for the card.

### Saving flash

Flash blocks wear out after many erase cycles (roughly 100,000).
LittleFS spreads writes across the whole partition, so wear is only a
concern for data rewritten very often:

- Don't write a file on every key press. Keep state in memory and save
  at natural moments.
- Prefer **one file rewritten occasionally** over many tiny files.
  LittleFS stores each file in at least one 4 KB block.
- Large or frequently changing data (downloads, caches) belongs on the
  card.

## Memory

| Item | RAM |
| --- | --- |
| `storage` | a few bytes |
| an open `file` | a small heap object, plus LittleFS's cache buffer for internal files, or a small SdFat file object for card files |
| an open `directory` | as for a file |
| the path you pass | copied into a short `std::string` for the call |
| SdFat's shared sector cache | inside FreeInk's SD manager, once |

At most **4** files and listings can be open on internal flash at the
same time. A fifth one fails with `too_many_open`. Close files as soon as
you are done; with `file` objects in local scope this happens
automatically.

There are no stdio or iostream buffers and no `std::filesystem` on the
device, which keeps both RAM use and firmware size down.

Storage only costs flash if the program uses it. The linker drops unused
code, so the hello-world firmware contains none of it. The chapter 9
example, which uses both volumes, is about **67 KB** larger (LittleFS,
SdFat with FAT and exFAT, and inkerface's storage code) and uses
**under 1 KB** more static RAM.

## Limits

- **Paths** are at most 255 bytes. Card entries with longer names are
  skipped in listings.
- **File size:** FAT32 cards limit files to 4 GB. Internal files are
  limited by the partition.
- **Case:** FAT ignores letter case (`A.TXT` and `a.txt` are the same
  file); LittleFS and the desktop don't. Use lower case to stay portable.
- **No replacing rename:** `rename()` refuses an existing target on every
  volume. Remove the target first (chapter 9, "Saving safely").
- **One thread:** storage is not thread-safe. Use it from the UI thread,
  or from exactly one worker.
- **Display and card share the SPI bus.** Use storage from the same
  thread that drives the display, never in parallel with a refresh.

## Summary

- Internal flash is LittleFS on a 9.9 MB partition: safe against power
  loss and good for settings.
- The card goes through FreeInk's SD manager and SdFat: large, removable,
  for books and downloads.
- Both mount on first use. Use 512-byte or larger blocks, save at natural
  moments, and keep frequently changing data on the card.
- The flash layout is in `partitions-x4.csv`, and changing the data
  partition erases the internal volume.

Next: [Networking](11-NETWORKING.md).

# Drive share app

The **Drive** row at the end of the **Apps** list (after the installed local apps) lists the files of a public Google Drive
shared folder and saves a chosen file to the SD card root. It is independent of
the local text-list apps described in [local-apps.md](local-apps.md).

## Configuration

The app folder on the SD card is `/.crosspoint/apps/drive-share/`. On first
launch the firmware creates `config.json` there with an example share:

```json
{
  "share_url": "https://drive.google.com/drive/folders/199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb?usp=sharing"
}
```

Edit `share_url` on the SD card to point at your own folder. The folder must be
shared as "Anyone with the link". A bare folder id is also accepted. Invalid or
unreadable configuration shows an error instead of the list.

## Use

1. Open **Apps** from the Home screen, then **Drive**; connect to Wi-Fi if prompted.
2. The folder content is listed. Sub-folders (`>`) open in place; Back goes up
   and, at the top level, returns to the Apps list.
3. Select a file and confirm the **Save to SD root?** popup. The file is
   downloaded to `/<file name>` (name sanitized for FAT) and replaces an
   existing file with the same name only when the download completes.
4. Saved files appear in the file browser and library and can be opened by the
   reader like any other file.

## Notes

- The listing uses Drive's public `embeddedfolderview` page, which needs no API
  key. Up to 64 entries per folder are shown.
- Google Docs/Sheets/Slides are not regular files and cannot be downloaded.
- Downloads use `drive.usercontent.google.com` with `confirm=t`, so files that
  trigger Drive's virus-scan interstitial still download.

# Drive share app

The **Drive** row at the end of the **Apps** list (after the installed local apps) lists the files of a public Google Drive
shared folder and saves a chosen file to the SD card root. It is independent of
the local text-list apps described in [local-apps.md](local-apps.md).

## Configuration

Drive has no built-in folder. Set it from the device web page:

1. Open the web page: start **File Transfer** in hotspot mode and connect to the
   device hotspot, or connect the device to Wi-Fi and browse to its IP address.
2. Go to **Settings > Google Drive**.
3. Enter the folder code, i.e. the last part of the share link
   `https://drive.google.com/drive/folders/<code>?usp=sharing` (for example
   `199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb`). The whole link is accepted too.
4. **Save**. **Clear** removes the setting.

The folder must be shared as "Anyone with the link". Until a folder is set,
**Apps > Drive** shows "Drive is not configured" with these instructions; after
saving, press **Retry** on the device.

The setting is stored in `/.crosspoint/apps/drive-share/config.json`:

```json
{
  "share_url": "https://drive.google.com/drive/folders/199fXhNtfxjrxpfR8fgpPQOaRYKhGhjIb?usp=sharing"
}
```

The file can also be edited on the SD card; a bare folder id is accepted as
`share_url`. The API is documented in [webserver-endpoints.md](webserver-endpoints.md).

## Use

1. Open **Apps** from the Home screen, then **Drive**; connect to Wi-Fi if prompted.
2. The first row, **Refresh**, reloads the list from Drive (reconnecting to
   Wi-Fi if needed) and shows when it was last synced (`Last sync: DD/MM HH:MM`,
   from the clock; omitted if the clock is not set). Use it after waking from
   standby, when the list may be out of date.
3. The folder content is listed. Sub-folders (`>`) open in place; Back goes up
   and, at the top level, returns to the Apps list.
4. Select a file and confirm the **Save to SD root?** popup. The file is
   downloaded to `/<file name>` (name sanitized for FAT) and replaces an
   existing file with the same name only when the download completes.
5. Saved files appear in the file browser and library and can be opened by the
   reader like any other file.

## Notes

- The listing uses Drive's public `embeddedfolderview` page, which needs no API
  key. Up to 64 entries per folder are shown.
- Google Docs/Sheets/Slides are not regular files and cannot be downloaded.
- Downloads use `drive.usercontent.google.com` with `confirm=t`, so files that
  trigger Drive's virus-scan interstitial still download.

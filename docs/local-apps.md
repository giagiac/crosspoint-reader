# Local SD-card apps

CrossPoint can display simple, declarative apps whose content is stored on the
SD card. The firmware includes a generic app picker and paged text-list screen;
adding or updating an app's folder does not require another firmware build.
Local apps do not execute code and can only present a text list.

## Install an app

Create one subfolder per app under `/.crosspoint/apps/`:

```text
/.crosspoint/apps/
  sample/
    app.json
    dati.txt
```

`app.json` describes the app and its text file:

```json
{
  "title": "Sample list",
  "data": "dati.txt",
  "page_size": 6
}
```

- `title` is the name displayed in the app picker (up to 80 UTF-8 bytes).
- `data` is a `.txt` filename in the same folder. Only letters, digits, `_`,
  `-`, and `.` are accepted; paths and parent-directory references are refused.
  If omitted, it defaults to `dati.txt`.
- `page_size` is optional and clamped to 1–8; the default is 6.
- Text files use one entry per line, with a maximum of 160 bytes per line.
  UTF-8 text is supported.

Copy the folder to the SD card, eject it safely, and open **Apps** from the
Home screen. Select an app, then use the on-screen **Previous page** and
**Next page** rows to page through its entries. Back returns to the app picker.
The app/data content is loaded from SD when needed; it is not copied into the
firmware image.

To update an app, replace its folder on the SD card. Up to 16 app folders are
shown. Invalid manifests or missing data files are logged and excluded from
the picker.

## Scope

This is a fixed firmware host for local text lists, not a native plugin loader.
It does not run JavaScript or C++ from the SD card. Supporting new app behavior
requires a firmware change and build; adding more list apps or changing their
data does not.

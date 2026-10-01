# Launchpad skins

A skin changes how the Launchpad looks. Choose one in the Launchpad under **Extra > Launchpad skin** (or on
Horizon's Settings page). Start Orbiter with `ORBITER_LAUNCHER_SKIN=classic` to get the classic Launchpad for one
run, whatever skin is chosen.

**Escape key.** Ctrl+Shift+L in the Launchpad window switches back to the classic Launchpad and keeps it chosen.
The window title shows the key while a skin is active. It also works when a skin shows nothing or hides its text.
A reset stores Classic even when `ORBITER_LAUNCHER_SKIN` chose the skin (the variable still wins at the next
start). Some keyboards have no Latin layout configured; there Ctrl+Shift+L may not arrive, so use the variable.

There are two kinds, and a skin can be both:

- **Style sheet (QSS)**: a Qt style sheet, CSS syntax, applied to the classic Launchpad. Example: `Dark`.
- **QML launcher**: a new front end written in QML. The classic Launchpad keeps working underneath; the skin reads
  and drives it through the `Launcher` object. Example: `Horizon`.

## A skin folder

Each folder in `Skins/` is a skin; the folder name is its id. `skin.cfg` describes it:

```
; comment lines start with ; or #
Name = My skin
Author = me
Version = 1.0
Description = One or two sentences for the skin list.
Api = 1
Qml = qml/Main.qml
Qss = style.qss
MinWidth = 1100
MinHeight = 680
Width = 1400
Height = 860
```

- `Qml` and `Qss` are optional, but at least one is needed. Paths are relative to the skin folder and must stay
  inside it (no `..`, no absolute paths, no links out of the folder).
- `Api` is the launcher API version the skin needs (this build: 1). `MinWidth`..`Height` are for QML skins: the
  smallest and the preferred size of the Launchpad window while the skin shows.
- There are no comments at the end of a line: `#` and `;` inside a value are part of it.

## Style sheets

- Change colours, borders and backgrounds. Don't change font sizes, font families or padding of labels: the classic
  layout is sized from the dialog font, so bigger text gets clipped.
- Controls are named after their resource ids: `QPushButton#IDLAUNCH`, `QWidget#IDC_BLACKBOX`, `#IDC_SCN_LIST`...
  The "Back to skin" button of QML skins is `QPushButton#customSkinBack`.
- `${SKIN}` is replaced by the skin folder's path; write `url("${SKIN}/image.svg")` with the quotes.
- The style sheet also reaches windows opened from the Launchpad (help, add-on dialogs), but not message boxes
  opened without a parent.

## QML skins

Import the API with `import Orbiter.Launcher 1.0` and use the `Launcher` object. Only `QtQuick` and `QtQml` (with
`QtQuick.Window`, `.Layouts`, `.Shapes`, `.Effects`, `.Particles`) are available; other modules fail to import.
The QML files of the skin folder can import each other as usual (a `qmldir` for singletons is fine, `plugin` lines
are not allowed).

- Stop animations and timers while `Launcher.active` is false (Launchpad hidden or in the background).
- Use `Launcher.launch()`, `Launcher.quit()` and `Launcher.openUrl()` rather than `Qt.quit()` or
  `Qt.openUrlExternally()`.
- A skin that fails to load falls back to the classic Launchpad; the errors are in Orbiter.log.
- The root object of the entry file must be an `Item` (or a type based on it), not a `Window`. Make it a
  `FocusScope` when items inside take keyboard focus: Orbiter gives the root the focus whenever the view is shown or
  activated.
- Ctrl+Shift+L is reserved: a skin doesn't receive it, and its `Launcher.setSkin` calls are ignored until the
  switch to Classic has run. One exception: after the first key of a two-key `Shortcut` in the skin, Qt may give
  the key to that shortcut. Still offer your own way to the classic pages (`Launcher.showClassic`).

**Safety.** A QML skin is code that runs inside Orbiter with your rights, like an add-on module. Orbiter keeps skins
from using the network and from loading files outside their folder in the usual ways, but this is not a sandbox:
`Qt.openUrlExternally`, images inside rich text and `Canvas.loadImage` can still reach local files. Install skins
you trust, as you would add-ons.

### Launcher API 1

Scenario paths are the classic ones: folder names and the scenario name joined by `/`, without `.scn`.

| Property | Type | |
|---|---|---|
| `apiVersion` | int | 1 |
| `version`, `build` | string | Orbiter's version and build text |
| `skin`, `skinUrl` | string, url | active skin id; its folder as a `file:` URL ending in `/` |
| `skins` | list | `{id, name, author, version, description, kind, compatible, reason}` |
| `scenarios` | list | the scenario tree in order: `{path, name, folder, isFolder, depth}` |
| `currentScenario` | string, writable | the selected scenario or folder |
| `currentIsScenario` | bool | the selection is a scenario |
| `currentDescription` | string | the selection's description as plain text |
| `canLaunch` | bool | the selection can be launched |
| `startPaused` | bool, writable | the "Start paused" option |
| `recent`, `favourites` | list of paths | recently launched, and marked with `toggleFavourite` |
| `modules` | list | plugin modules: `{name, category, info, active, locked}` |
| `setup` | map | `{graphicsClient, device, fullscreen, width, height, activeModules, nonsphericalGravity, radiationPressure, distributedMass, atmWind}`; `graphicsClient` is the Video tab's text, "Console mode (no engine loaded)" without a client |
| `active` | bool | the skin is shown and the Launchpad is the active window |
| `page` | string, writable | kept for the skin while its view is rebuilt after a flight |
| `state` | map, writable | same, for up to 64 KiB of data |

| Method | |
|---|---|
| `scenarioInfo(path)` | facts from the file: `{name, folder, isFolder, description, system, focus, focusClass, focusStatus, focusBody, focusBase, focusPad, vesselCount, vessels, mjd, date}` |
| `launch(path)` | launches `path` (or the selection) |
| `showClassic(page)` | shows the classic Launchpad on `scenarios`, `parameters`, `modules`, `video`, `extra` or `about`, with a Back button |
| `help(page)` | the help of that classic page |
| `setModuleActive(name, on)`, `deactivateAllModules()` | as the classic Modules page |
| `saveCurrentState()`, `clearQuicksaves()` | as the classic Scenarios page |
| `toggleFavourite(path)` | returns whether it is a favourite now |
| `setSkin(id)` | switches skin ("" for classic) |
| `refreshSetup()` | reads `setup` again |
| `openUrl(url)` | opens an http, https or mailto URL in the browser |
| `quit()` | closes Orbiter |
| `log(text)` | writes a line to Orbiter.log |

Signals: each property has its `...Changed` signal; `returnedFromClassic()` fires when the user comes back from the
classic pages.

Writing `currentScenario` or `startPaused` takes effect at once; every method that opens a dialog or launches runs
after the current QML call returns.

`date` is the scenario's simulation date as `YYYY-MM-DD HH:MM`; scenarios without a date start at the current time
and have no `mjd` or `date`.

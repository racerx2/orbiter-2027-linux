# Launchpad skins

A skin changes how the Launchpad looks. Choose one in the Launchpad under **Extra > Launchpad skin** (or on
Horizon's Settings page). Start Orbiter with `ORBITER_LAUNCHER_SKIN=classic` to get the classic Launchpad for one
run, whatever skin is chosen.

Under a skin's style sheet every Launchpad control keeps the dialog font (Orbiter sets it on each control), also in
windows opened later.

**Escape key.** Ctrl+Shift+L in the Launchpad window switches back to the classic Launchpad and keeps it chosen.
The window title shows the key while a skin is active. It also works when a skin shows nothing or hides its text.
A reset stores Classic even when `ORBITER_LAUNCHER_SKIN` chose the skin (the variable still wins at the next
start). Some keyboards have no Latin layout configured; there Ctrl+Shift+L may not arrive, so use the variable.

There are three kinds, and a skin can be any mix of them:

- **Style sheet (QSS)**: a Qt style sheet, CSS syntax, applied to the classic Launchpad. Example: `Dark`.
- **QML launcher**: a new front end written in QML. The classic Launchpad keeps working underneath; the skin reads
  and drives it through the `Launcher` object. Example: `Horizon`.
- **Layout (Qt Designer)**: the classic Launchpad's windows as Qt Designer forms, edited in Qt Designer. Make one
  with **New layout...** in the skin list (see Layouts below).

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
Ui = ui
MinWidth = 1100
MinHeight = 680
Width = 1400
Height = 860
```

- `Qml`, `Qss` and `Ui` are optional, but at least one is needed. `Ui` is a folder of Qt Designer forms. Paths are
  relative to the skin folder and must stay inside it (no `..`, no absolute paths, no links out of the folder).
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

## Layouts (Qt Designer)

**Making one.** Extra > Launchpad skin > **New layout...** asks for a name and, if you like, a skin whose colours
to copy. It writes `Skins/<name>/` with `skin.cfg`, `ui/<dialog>.ui` for the 32 Launchpad windows (`IDD_MAIN` the
main window, `IDD_PAGE_*` its pages, `IDD_OPTIONS_*` the Options pages, `IDD_EXTRA_*` the Extra dialogs,
`IDD_SAVESCN`, `IDD_MSG`), the stock pictures in `ui/images/` and a `README.txt`. **Open in Qt Designer** opens the
main window and its pages in Qt 6 Designer (`/usr/lib/qt6/bin/designer`; `/usr/bin/designer` is often Qt 5's).

**What edits do.** Orbiter builds each window as before, then applies its form before the classic code sets it up,
so the classic code works from the new places.

- Move and resize controls, also into group boxes, frames and splitters (Orbiter keeps them where you put them).
- Text, title, tool tip, font, style sheet, alignment, word wrap and flat buttons are applied. Texts are shown as
  typed, not as rich text; a longer text needs a wider control, as Designer shows.
- The form's own font reaches every control without a font of its own, and its style sheet applies to that window
  (the main window's comes before a skin's style sheet). A window's title is applied; the main window keeps its own
  for the skin hints.
- A picture's `pixmap` can be any file inside the skin folder, up to 4096 x 4096 (paths are relative to the form). A
  label shows a picture or a text: remove the `pixmap` to show a text.
- Deleting a control hides it. Launch, Exit, the six page buttons, the page area, the Extra list and its Edit button,
  and the Options list and page area can't be deleted.
- The tab order (Edit > Edit Tab Order) is applied.
- Added `QLabel`s (texts and pictures), `QFrame`s, lines and `QGroupBox`es are decorations: everything is stacked
  as Designer paints the form, decorations let clicks through and stay where they are; a dynamic string property `anchor` with `right` and/or
  `bottom` keeps one at its distance from that edge. Other added widgets do nothing and are left out.
- Anything else (class, names, enabled, sizes of fonts in the classic text measure) is the classic code's.

**Rules.** Absolute positions only: a form with a Designer layout is refused (Form > Break Layout). Orbiter finds
its controls by the dynamic properties `orbiterCtl` and `orbiterControls`, not by name, so keep them; copies of a
control are ignored. Framed boxes with `orbiterStandIn` are areas Orbiter draws itself: only their place and size
count. A changed control must stay on the form and be at least 8 x 8 (the page area 100 x 100). The form's size is
the window's starting size; `IDD_MAIN` can't be smaller than 550 x 350. `${SKIN}` in a style sheet is the skin
folder; other relative `url()`s resolve against Orbiter's folder. Options pages have no scroll bar in the Launchpad:
a page taller than its area is cut off. `RefitText` measures with the dialog's font, so a label whose text Orbiter
sets may not fit a font you enlarged.

**What Orbiter places itself** (at the start and whenever the window is resized; the forms note it in
`orbiterNote`):

| Window | Placed by Orbiter |
|---|---|
| IDD_MAIN | Launch, Help, Exit: their top follows the bottom edge, height = Exit's; Help and Exit move right with the width; in a window narrower than the layout's minimum the three shrink into a row from Launch. Black box: the width grows. Shadow bar: the full window width. Page area: grows. Version: follows the bottom edge. Banner: resized to the black box's height when their heights differ. |
| IDD_PAGE_SCN | List, description and HTML description: inside the splitter (its place and height; the split at the list's width). Save, Clear quicksaves and Info follow the bottom edge; Clear quicksaves is Save's width; Info is at the description's right edge. Start paused follows the right edge. |
| IDD_PAGE_MOD, IDD_PAGE_EXT | The list and text inside the splitter; the buttons follow the bottom edge. |
| IDD_PAGE_OPT | The split keeps its place and is sized to the page; the page list is 120 pixels wide. |
| IDD_PAGE_DEV, IDD_PAGE_ABT | Centred in the page area, cut off if larger. |

**When it shows.** The main window, its pages and the Options pages change when Orbiter starts again; the other
windows at their next open. Choosing a skin with another layout changes colours and QML at once and the layout at
the next start (while `ORBITER_LAUNCHER_SKIN` is set, it chooses the next start's skin). Orbiter.cfg's saved list widths start again from the layout whenever the layout changes (also for a
one-off `ORBITER_LAUNCHER_SKIN=classic` run). Problems are written to Orbiter.log (`Launcher layout:`); a window
whose form can't be used stays stock, and one message at the start counts them.

**Ctrl+Shift+L** with a layout undoes its looks at once (decorations, texts, fonts, style sheets, pictures; deleted
controls come back, except those the classic code shows and hides: the Scenarios description and Info button, the
Video page and the Options pages, which come back at the next start). Places stay until the next start, which uses
the stock layout; the title says so.

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
| `skins` | list | `{id, name, author, version, description, kind, layout, compatible, reason}`; `kind` is `qml`, `qss`, `qml+qss` or `ui` (a layout only); `layout`: the skin has a layout |
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

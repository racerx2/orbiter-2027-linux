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

There are four kinds, and a skin can mix them (but has one launcher: QML or Qt Designer):

- **Style sheet (QSS)**: a Qt style sheet, CSS syntax, applied to the classic Launchpad. Example: `Dark`.
- **Qt Designer launcher**: a new front end made as one Qt Designer form with a little JavaScript, edited in Qt
  Designer. The classic Launchpad keeps working underneath; the skin reads and drives it through the `Launcher`
  object. Examples: `Horizon`, `PlanetaryDefense` (see Qt Designer launchers below).
- **QML launcher**: the same, written in QML. Examples: `HorizonQml`, `PlanetaryDefenseQml` (the QML versions of
  the two skins above).
- **Layout (Qt Designer)**: the classic Launchpad's windows as Qt Designer forms, edited in Qt Designer. Make one
  with **New layout...** in the skin list (see Layouts below).

**Copy...** in the skin list copies a skin's folder under a new name, to change your own copy: the build and an
update of Orbiter replace the files of the skins that come with it (Dark, Horizon, PlanetaryDefense, HorizonQml,
PlanetaryDefenseQml). **Open in Qt Designer** on one of those offers the copy first. A copy takes at most 4000
files, folders and links, and 64 MiB; links inside the skin stay links, links out of it are left out and listed.
It is all or nothing: a copy that fails leaves no folder behind.

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
Forms = forms/Main.ui
Qss = style.qss
Ui = ui
MinWidth = 1100
MinHeight = 680
Width = 1400
Height = 860
```

- `Qml`, `Forms`, `Qss` and `Ui` are optional, but at least one is needed; `Qml` and `Forms` not both. `Ui` is a
  folder of Qt Designer forms, `Forms` the Qt Designer launcher's form. Paths are relative to the skin folder and
  must stay inside it (no `..`, no absolute paths, no links out of the folder).
- `Api` is the launcher API version the skin needs (this build: 1). `MinWidth`..`Height` are for QML and Qt
  Designer launchers: the smallest and the preferred size of the Launchpad window while the skin shows.
- There are no comments at the end of a line: `#` and `;` inside a value are part of it.

## Style sheets

- Change colours, borders and backgrounds. Don't change font sizes, font families or padding of labels: the classic
  layout is sized from the dialog font, so bigger text gets clipped.
- Controls are named after their resource ids: `QPushButton#IDLAUNCH`, `QWidget#IDC_BLACKBOX`, `#IDC_SCN_LIST`...
  The "Back to skin" button of QML and Qt Designer launchers is `QPushButton#customSkinBack`.
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

## Qt Designer launchers

A Qt Designer launcher is one form, `forms/Main.ui`, that Orbiter builds as widgets, with JavaScript files for its
logic. It uses the same `Launcher` object as QML skins (Launcher API 1 below).

```
Skins/Horizon/
  skin.cfg           Forms = forms/Main.ui, Qss = style.qss (the classic pages' colours)
  style.qss
  forms/
    Main.ui          the whole launcher: background, top bar, a QStackedWidget with one page per tab, footer, toast
    skin.qrc         the pictures, so Designer shows them (prefix /horizon; Planetary Defense: /pd)
    Format.js        texts for the scenario facts
    Logic.js         the logic, and update(): the names every binding sees
    images/          icons, toggle pictures, and previews of the painted widgets for Designer
```

**Editing.** Extra > Launchpad skin > **Open in Qt Designer** opens `forms/Main.ui` in Qt 6 Designer (for a skin
that comes with Orbiter, after offering a copy; Apply the copy to see it). While the skin is the active one, save
in Designer and the launcher builds the form again about half a second later; a form that can't be used keeps the
old one and says why in Orbiter.log and in the launcher.

- Pages: the page area is a QStackedWidget; right-click it > Page n, or use the arrows at its top right. Each page
  has a dynamic property `page` with its name.
- Lists show one card, row or chip: the template, edited in place; Orbiter repeats it for each element.
- Bindings, actions and the like are dynamic properties: Property Editor > Dynamic Properties, the green +.
- Pictures: `:/horizon/images/...` names from skin.qrc in `pixmap`, `icon` and style sheet `url()`. Orbiter reads
  them from the skin folder; files outside it are refused. Pictures up to 4096 x 4096.
- The look is the root widget's `styleSheet`. It begins with a reset, because the classic pages' style sheet
  reaches these widgets too; rules pick widgets by the dynamic property `role` (`QLabel[role="h1"]`) and by states
  set from bindings (`QFrame[role="card"][current="true"]`).
- The painted widgets (stars, planet, thumbnails, orbit diagram) are QLabels promoted to their class; Designer shows
  a preview picture. Designer doesn't show letter spacing, line height, elision, glows, live data or animation.
- Layouts work as in Designer. A widget in a parent without a layout keeps its place; `anchor` keeps its distance
  to edges of the parent.
- Widget classes: QWidget, QFrame, Line, QLabel, QPushButton, QToolButton, QCheckBox, QRadioButton, QLineEdit,
  QStackedWidget, QScrollArea, QGroupBox, QTabWidget, QProgressBar and the painted classes. A widget promoted to
  another class is built as the class it was promoted from, with a warning; other classes stop the load.

**JavaScript.** The root's string list `scripts` names the files (in `forms/`, at most 16 of 1 MiB), run in that
order. Expressions in the properties are JavaScript. Names:

| Name | |
|---|---|
| `Launcher` | the Launcher API 1 object |
| `update()` | your function: each refresh calls it first; each key of the object it returns is a name for that refresh |
| `ui` | an object for the skin's own state (search text, filters) |
| `view` | `width`, `height`, `revision` (counts Launcher changes, for caches), `toast(text)`, `hasFocus(name)`, `focus(name)` |
| `toast(text)` | shows a short message in the widget named `toast` |
| `w` | the script objects of painted widgets by name: `w.diagram.flow`, `w.diagram.reset()` |
| `Orbits` | planet and asteroid positions and the Apophis clock for Planetary Defense: `countdown(ms)`, `epochText(mjd)`, `dayText(mjd)`, `mjdOfMs(ms)`, `pad(v, n)`, `planet(name, mjd)`, `neo(name, mjd)`, `neoSet(name, mjd)`, `neoSets(name)`, `planetNames`, `neoNames`, `MJD_MIN`, `MJD_MAX`, `NEO_YEARS` |

A refresh runs after every Launcher change, action, edit, resize and focus change: `update()`, then the bindings of
the shown widgets (hidden pages are left until they show). A script error stops the load; an error in a binding is
written to Orbiter.log once and leaves the widget as it was. A call that runs longer than 2 s is stopped.

**Dynamic properties.** `item` and `index` are the list element and its position inside a template.

| Property | On | |
|---|---|---|
| `bind` | QLabel, buttons (text), checkable buttons and QCheckBox (checked), QStackedWidget (page name, or a page number), QProgressBar, QLineEdit, QGroupBox (title) | the main value |
| `bind_<name>` | any | sets `<name>`: a Qt property (`toolTip`, `maximumHeight`), a dynamic property for style sheet rules, a property below, or `fixedWidth`, `fixedHeight`, `layoutMargins` (`[l, t, r, b]`), `layoutSpacing` |
| `showIf`, `enableIf` | any | shown, enabled while true |
| `model` | QLineEdit | two-way: shows the value and assigns what is typed (`ui.query`); Escape clears it |
| `action`, `doubleAction` | any | statements on click, double click; checkable buttons don't toggle themselves, the binding shows the state |
| `key_<Key>` | any | statements for that key (`key_Return`, `key_Up`) when the focused widget doesn't use it; not while Ctrl, Alt or Meta is held |
| `list` | a container with a layout | an array; the container's template children are repeated for each element |
| `template`, `templateIf` | children of a `list` container | a template; `templateIf` picks among several |
| `maxItems`, `fit` | the `list` container | at most N; only the clones that fit whole |
| `cellWidth`, `columns` | a `list` container with a grid layout | columns from the width, or a number |
| `cellWidth`, `cellHeight` | a `list` container that is a QScrollArea's content, without a layout | a grid that makes only the rows in view (thousands of scenarios) |
| `flow` | a container with a box layout | wraps its items to new rows |
| `tick` | any | runs the widget's bindings every N ms while the Launchpad is active (clocks) |
| `fade` | any with `showIf` | fades in and out over N ms |
| `disabledOpacity` | any | drawn at that opacity while disabled |
| `anchor` | a child of a parent without layout | `left right top bottom hcenter vcenter fill` |
| `letterSpacing`, `lineHeight`, `maxLines`, `elide`, `autoSize` | QLabel (`letterSpacing` any text) | letter spacing in px; line height factor; at most N lines with "…"; one line with "…"; size to the text |
| `fitContent` | QScrollArea | as tall as its content, up to the room it has |
| `hover`, `clickThrough` | any | `:hover` in style sheets; mouse clicks pass through |
| `glowColor`, `glowRings`, `glowStep`, `glowWidth`, `glowRadius`, `glowAlpha`, `glowHoverAlpha`, `glowFade` | any | soft rings around the widget, as the QML skins' glows |
| `page` | pages of a QStackedWidget | the page's name |
| `scripts` | the root | the JavaScript files |

**Painted widgets.**

| Class | Properties |
|---|---|
| `Starfield` | `seed`, `count`, `alpha`, `drift` (px), `driftPeriod` (ms) |
| `Planet` | `glare`, `pulse` (Horizon's Earth and sun) |
| `ScenarioThumb` | `kind`, `seed`, `corner` (Horizon's card pictures) |
| `GridBackdrop` | `step` (Planetary Defense's grid) |
| `MiniOrbit` | `kind`, `seed`, `corner` (Planetary Defense's card pictures) |
| `OrbitDiagram` | `baseMjd`, `fromScenario`, `focusBody`, `system`; script object with `flow`, `offset`, `mjd`, `baseMjd`, `focusPlanet`, `hiddenNeos`, `epochOutside`, `reset()` |

**Safety.** A Qt Designer launcher runs JavaScript inside Orbiter with your rights. The JavaScript has no file or
network access, and the form loads pictures and scripts only from its skin folder; `Launcher.openUrl` is the way
out. Rich text in labels and tool tips shows pictures from the skin only, `<link>` style sheets are removed, and so
are styles with `&` or `@` and style elements broken up by comments; `openExternalLinks` stays off, Markdown
labels show plain text, and style sheets can't set `text`, `toolTip`, `styleSheet` and the like through
`qproperty-`. Install skins you trust, as you would add-ons.

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
| `skins` | list | `{id, name, author, version, description, kind, layout, compatible, reason}`; `kind` is `qml`, `qml+qss`, `forms`, `forms+qss`, `qss` or `ui` (a layout only); `layout`: the skin has a layout |
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

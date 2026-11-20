# Handoff — Prepress PDF

For an AI agent (or developer) continuing this work. Read this before touching code.
`README.md` is the end-user guide; keep it in plain language.

## Goal

Open-source cross-platform desktop PDF tool for print production: Acrobat-style
Output Preview, PDF/X handling per ISO 15930 (display, verify, fix), and
PitStop-style editing that never damages files.

## Decisions made by the owner (do not reopen without asking)

| Topic | Decision |
|---|---|
| Platform | Cross-platform desktop: Windows, macOS, Linux |
| Licence | AGPL-3.0 (forced by MuPDF) |
| Stack | C++20, Qt 6 Widgets, MuPDF, Little CMS 2, CMake |
| PDF/X scope | Display + verify + fix |
| Editing scope | Object editing, text editing, preflight fix-ups, page operations |
| ICC "install" | App library by default, plus optional system-wide install |
| Order of work | Viewer + output preview, then preflight, then editing, text editing last |

The owner prefers to be asked clarifying questions rather than have things assumed.

## State

Phase 1 (viewer + output preview) is mostly done. Phase 2: verification is
done, fix-ups are not started. Phases 3-4 are not started.

| Platform | How it is built | Versions seen in CI |
|---|---|---|
| Linux (Ubuntu 24.04) | system packages, static MuPDF | Qt 6.4.2, MuPDF 1.23.10, lcms2 2.14 |
| Windows | MSYS2 UCRT64 (MinGW gcc), MuPDF as DLL | Qt 6.12.0, MuPDF 1.28.5 |
| macOS | not built; ColorSync paths never compiled | - |

CI (`.github/workflows/build.yml`) builds and tests both on every push. The
Windows job also builds the NSIS installer, installs it silently, starts the
installed app with a bare PATH on a test PDF and uploads the installer as the
`windows-installer` artifact. To publish a release, start the workflow by hand
with "release" ticked (`gh workflow run build.yml --ref main -f release=true`):
it creates tag `v<CMake version>` and a GitHub release with the installer.
Bump the version in `CMakeLists.txt` first; an existing tag makes it fail.
Tag pushes also publish, but the Claude cloud sandbox cannot push tags (403).

### Done

- Open PDF, page navigation, zoom, fit page, drag-and-drop, recent files.
- Rendering to CMYK + spot channels with overprint simulated.
- Separations list with per-plate show/hide; single plate shown as grey.
- Ink % under cursor, total area coverage, 1/3x3/5x5 sampling.
- Coverage highlight above a limit.
- Continuous scrolling with tile rendering (v0.5.0): see "Rendering model".
- PDF/X-4 XMP rules matched to an Acrobat Preflight comparison (v0.5.0).
- Per-box overlay switches (trim, bleed, crop, art, media) and middle-button panning.
- PDF/X preflight (verification only): five standards, clickable findings,
  PDF/HTML/JSON report, command-line mode. See "Preflight" below.
- Pages panel with thumbnails (requested one at a time so they never queue
  ahead of the page being viewed).
- Object Inspector: click a point, list objects under it (bounding-box test,
  clipping ignored) with colour space, values, overprint, opacity, font,
  image pixels and effective ppi.
- Text search across pages with highlights; view rotation (0/90/180/270).
- App icon, About dialog, third-party notices; monitor profile auto-detected
  on Windows (`GetICMProfileW`, main screen only).
- Developer mode: editable theme and layout with live reload.
- Windows installer, published as GitHub release v0.1.0.
- Output Preview as a separate floating window.
- ICC: simulation profile (output intent or library profile), monitor profile,
  relative / absolute colorimetric display.
- Profile library: import, remove, system-wide install.
- PDF/X version detection (Info dict and XMP), output intent listing.
- Trim/bleed/art overlays, trim size in mm.

### Phase 1 still open

- Black-ink simulation.
- Rendering uses one thread (one `fz_context`). Several render threads need
  `fz_clone_context` with lock callbacks.
- Opening reads every page's info (`Document::pageInfo`) before showing
  anything; for documents with thousands of pages make that lazy.
- Monitor profile detection on Linux and macOS; per-screen profiles.
- Bundled press profiles (licence must be confirmed per profile).
- Dock layout and main window geometry are not saved between sessions.
- macOS build; a MuPDF version pinned and shared by all platforms.
- Object inspector: honour clipping; real point-in-path test.

## Build and test

```bash
sudo apt-get install cmake g++ qt6-base-dev libmupdf-dev liblcms2-dev libharfbuzz-dev libopenjp2-7-dev libjbig2dec0-dev libmujs-dev libgumbo-dev libfreetype-dev libjpeg-dev zlib1g-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build --output-on-failure
```

Headless: tests set `QT_QPA_PLATFORM=offscreen` through CTest. To see the
window without a display:

```bash
xvfb-run -a ./build/prepresspdf --screenshot out.png tests/data/ink_patches.pdf
UI_SCREENSHOT=out.png QT_QPA_PLATFORM=offscreen ./build/test_ui
```

Regenerate test PDFs with `python3 tests/data/make_test_pdf.py tests/data`.

## Windows packaging

- `packaging/windows/deploy.sh` (MSYS2 shell): copies the exe, runs
  `windeployqt6`, then copies every non-Qt DLL found with `ldd` until nothing
  new appears.
- `packaging/windows/installer.nsi`: per-machine install to Program Files,
  Start menu shortcut, uninstaller, Add/Remove Programs entry. No file
  association on purpose.
- Why MSYS2 and not MSVC/vcpkg: the vcpkg `libmupdf` port is built with
  `FZ_ENABLE_ICC=0`, which is useless for colour-managed rendering. MSYS2's
  `libmupdf` is built from the full source with ICC.
- MSYS2 is a rolling distribution, so Qt and MuPDF versions on Windows are
  **not pinned** and differ from Linux. A MuPDF API change can break the
  Windows build without any change in this repo.
- The installer includes `THIRD-PARTY-NOTICES.txt` and a `licenses` folder
  filled by `deploy.sh` from the MSYS2 package each bundled DLL belongs to.
- Icon: `resources/make_icon.py` writes `resources/app.png` (window icon via
  `app.qrc`) and `packaging/windows/app.ico` (exe via `app.rc`, installer).
- Open: installer is unsigned (SmartScreen warning). The version is read from
  `project(... VERSION ...)` in `CMakeLists.txt`.
- CI artifacts cannot be downloaded from the Claude cloud sandbox (blob host
  blocked), so the Windows screenshot artifact has never been looked at.

## Architecture

```
src/engine/   no widgets; QtGui types only
  Types.h            SeparatedImage, PageBoxes, PageRender, DocumentInfo
  Document           MuPDF wrapper: open, metadata, render page to ink channels
  Compositor         ink channels -> QImage (plate visibility, ICC, coverage); inkValues()
  ProfileLibrary     ICC profile folders, import/remove/system install (lcms2)
src/ui/
  RenderWorker       owns the Document on a worker thread; all MuPDF calls happen there
                     (page renders, thumbnails, search, object inspection)
  MainWindow         glue: schedules tiles, keeps their ink channels, menus, status bar
  PageView           QAbstractScrollArea: all pages in one column, painted from tiles;
                     overlays (boxes, search, selection); hover/click/pan/zoom input
  TileKey.h          (page, column, row) of a 512 x 512 pixel tile
  OutputPreviewPanel profile, plates, ink readout, coverage. Shown in a floating,
                     non-dockable QDockWidget ("outputPreview") owned by MainWindow:
                     the owner wants it as a separate draggable window
  ProfileDialog      profile management
  CompositeWorker    ink channels -> screen image per tile, on its own thread
  PreflightPanel     standard selector, Check/Stop, findings tree, report export
src/preflight/       compiled into the engine library
  Types.h, Report.cpp  Standard, Finding, Report; HTML and JSON export
  Preflight.cpp        the checker (see "Preflight")
src/engine/MuPdfUtil.h  private MuPDF helpers shared by Document.cpp and Preflight.cpp
  Customization      user-editable theme.qss + layout.json, file watcher (see below)
tests/
  test_engine.cpp    ink values, overprint/knockout, boxes, PDF/X detection, profiles
  test_preflight.cpp one clean file, one file per violation: exact rule ids and bounds
  test_ui.cpp        drives the real MainWindow
  test_hidpi.cpp     the window at scale factor 2: sharpness and readout per device pixel
```

### Rendering model (the core idea)

`Document::renderTile(page, zoom, rotation, area)` renders one rectangle of a
page into a pixmap of CMYK + one channel per spot (`fz_page_separations`,
every separation set to `FZ_SEPARATION_SPOT`). The pixmap colourspace is the
document's CMYK output intent if it has one, otherwise DeviceCMYK, so
DeviceCMYK numbers in the file arrive unchanged. The last 6 pages used are kept
prepared (`Document::Prepared`: page, display list, separations, boxes), and a
tile is drawn by playing the display list with the tile as scissor. Note: the
transform goes to `fz_run_display_list`, the draw device gets identity, so the
scissor is in pixels (the other way round only the first tile is right).
`render()` is `renderTile` with an unlimited area (thumbnails, tests).

Pipeline, all keyed by `TileKey` and the view's **generation** (bumped on every
zoom, rotation or document change; anything from an older generation is dropped):

1. `PageView::viewChanged` -> `MainWindow::scheduleTiles` (coalesced with a
   0 ms timer) -> one batch of all missing wanted tiles, nearest the middle
   first, to `RenderWorker::renderTiles`. A newer batch id stops the older
   batch between tiles. Tile arrivals never trigger scheduling (no loops).
2. `tileRendered` -> `MainWindow::m_store` (ink channels, at most 160 tiles,
   evicting tiles that are not wanted) -> `CompositeWorker::composeTile` with
   the current display settings (`m_options`, id `m_optionsId`).
3. `tileComposed` -> `PageView::setTile`. The view keeps at most 160 pictures
   and, after a zoom, the previous zoom's pictures stretched as placeholders
   until the page's visible tiles are sharp again.

- HiDPI (v0.5.1): `PageView::zoom()` is logical pixels per point;
  `renderZoom()` = zoom x `deviceScale()` (the screen's devicePixelRatio) is
  what tiles are rendered at. **`TileKey` and tile areas count device
  pixels**; painting divides by the scale. A move to a screen with another
  scale retires the tiles (`checkDeviceScale`, from `QEvent::DevicePixelRatioChange`
  on Qt >= 6.6 and from every paint as a fallback). Mouse positions keep
  their fraction (`pageAt(QPointF)`) so readout resolves single device pixels.
  Thumbnails are rendered at the list's scale too. Tested by
  `tests/test_hidpi.cpp` (QT_SCALE_FACTOR=2, 0.5 pt stripes must alternate
  black/white per device pixel); checked by eye at 1.5 for tile seams.
- Ink readout: `MainWindow::showInkValues` looks the hovered pixel up in
  `m_store` and maps the tile's inks onto the document-wide list `m_allInks`
  (CMYK + every spot in the document, from `PageInfo::spots`).
- Toggling plates, coverage or profiles: `recomposite()` re-composites every
  stored tile (on-screen first); no re-render. Plates are hidden **by name**
  (`CompositeOptions::hiddenInks`) because spot channels differ per page.
- Display = spots folded into CMYK via their CMYK equivalents (multiply),
  then lcms2 CMYK -> monitor RGB using the simulation profile.
- Cost: about 20 ms to render and composite a full 1920 x 1080 window at any
  zoom (`./build/bench`), against 540 ms for one 60 MP page before.

Consequence: the simulation profile changes appearance only, never ink numbers.
Overprint cannot be switched off (MuPDF applies it when the target has
separations). Both are deliberate; the owner has been told.

## Preflight

`preflight::run(Document&, Standard, Options, progress)` in
`src/preflight/Preflight.cpp`. It must run on the thread that owns the
Document (`RenderWorker::runPreflight`). Two sources of facts:

1. **PDF object structure** (`pdf_*` API): identification, version, encryption,
   output intent, metadata, catalog features, page boxes, annotations, font
   embedding (`collectUnembeddedFonts` walks resources through forms and
   patterns), and one pass over every object in the xref (`checkAllObjects`)
   for filters, PostScript, OPI, soft-mask images, transfer functions.
2. **A recording device** (`PageDevice`, same technique as the inspector) run
   over each page's contents: colour space of every fill, stroke, text, image
   and gradient; opacity, blend modes, soft masks, groups; image resolution;
   where non-embedded fonts are used. This is what gives findings a location.

Finding bounds are view points **at rotation 0**; `MainWindow::toCurrentRotation`
maps them to the current view. At most 10 findings per rule per page are
listed; the last one says how many more there are (`Checker::closePage`).

PDF names are compared as text (`isName`, `pdf_dict_gets`) because the set of
built-in `PDF_NAME(...)` constants differs between MuPDF versions.

### Rules

| Rule id | Severity | Applies to |
|---|---|---|
| `id.missing`, `id.mismatch`, `id.conformance` | Error | all (X-1a/X-3: Info dict; X-4: XMP) |
| `file.encrypted`, `file.damaged`, `file.damagedPage`, `file.notPdf` | Error | all |
| `file.version` | Warning | header version above 1.3 (2001/2002), 1.4 (2003), 1.6 (X-4) |
| `intent.missing`, `intent.multiple`, `intent.identifier`, `intent.profile`, `intent.profileInvalid`, `intent.profileClass` | Error | all (embedded profile mandatory for X-4; registry name accepted otherwise) |
| `intent.profileSpace` | Error | X-1a (profile must be CMYK) |
| `meta.title`, `meta.trapped` | Error | X-1a, X-3: Info dict or XMP. X-4: XMP only (`dc:title`, `pdf:Trapped`) |
| `meta.creationDate`, `meta.modDate` | Error | all (Info dict or XMP) |
| `meta.versionId`, `meta.renditionClass` | Error | X-4 (`xmpMM:VersionID`, `xmpMM:RenditionClass`) |
| `meta.xmpPrefix` | Error for `xmp`, `xmpMM`; Warning for `dc`, `pdf`, `pdfxid` | X-4 |
| `meta.infoXmpMismatch` | Error | X-4: Info entries (Title, Author, Subject, Keywords, Creator, Producer, dates, Trapped) must be in and equal to XMP |
| `meta.xmpInvalid` | Error | X-4: XMP is not well-formed XML |
| `meta.xmp` | Error | X-4 |
| `box.mediaMissing`, `box.trimMissing`, `box.trimAndArt`, `box.nesting` | Error | all |
| `font.notEmbedded` | Error | all |
| `color.rgb`, `color.lab`, `color.icc` | Error | X-1a |
| `color.spotAlternate` | Warning | X-1a |
| `color.deviceRgb`, `color.deviceCmyk` | Error | X-3, X-4 (device colour not matching the intent) |
| `transparency.opacity`, `.blendMode`, `.softMask`, `.group`, `.softMaskImage` | Error | X-1a, X-3 |
| `annot.inside` | Error | all (TrapNet and PrinterMark exempt) |
| `forbidden.javascript`, `.postscript`, `.opi`, `.transferFunction` | Error | all |
| `forbidden.layers`, `forbidden.jpeg2000` | Error | X-1a, X-3 |
| `forbidden.action`, `.alternates`, `.embeddedFiles`, `.lzw`, `.jbig2` | Warning | see code |
| `image.lowResolution` | Warning | all; not a PDF/X rule (`Options::minImagePpi`, default 150) |

### Not confirmed against ISO 15930 (the texts were not available)

Everything above was written from memory of the standards. Treat these as the
least certain, which is why most are warnings:

- `file.version`: whether a higher header version alone is a violation.
- `forbidden.lzw`, `forbidden.jbig2`: which editions forbid which filters.
- `forbidden.action`, `forbidden.embeddedFiles`, `forbidden.alternates`: exact scope.
- `color.spotAlternate`: restrictions on alternate spaces.
- `meta.*` for X-4: **confirmed by Acrobat Preflight** (owner's comparison, a
  14-page file, PDF/X-4 profile): `meta.title` (XMP), `meta.trapped` (XMP),
  `meta.versionId`, `meta.renditionClass`, `meta.xmpPrefix` for `xmp` and
  `xmpMM`, `meta.infoXmpMismatch` for Creator and Producer. Rebuilt as
  `tests/data/x4_acrobat_comparison.pdf`; our report on it equals Acrobat's
  list. Not confirmed: the prefix rule for `dc`/`pdf`/`pdfxid` (warnings), the
  other Info/XMP pairs, `xmpMM:DocumentID` (not checked; Acrobat did not
  report it, the file may have had one).
- XMP is read with `preflight::parseXmp` (`src/preflight/Xmp.cpp`), by
  namespace URI, so values are found whatever prefix a file uses.
- `transparency.group`: reported as an error for any group MuPDF opens.
- Not checked at all: rendering intents, halftones (`HTP`), DeviceN details,
  overprint mode rules, X-4 layer (OCG) configuration rules, trap networks,
  `OutputCondition` vs registry contents, X-4p/X-5 external profiles, X-6.

Validation so far: the owner's Acrobat comparison above, the committed test
files, and a PDF/X-3:2002 file made by Ghostscript (`gs -dPDFX` with a customised `PDFX_def.ps`) which passes with no
findings. The owner has Acrobat Pro and PitStop and was asked to compare
results on real files; adjust rules from that feedback.

Not covered by a committed test: output intents with an embedded profile
(`intent.profileClass`, `intent.profileSpace`), because no freely
redistributable CMYK profile is in the repo. Checked by hand with the
Ghostscript file only.

## Developer mode (GUI customisation)

Owner's request: a way to change how the GUI looks and is laid out without
programming. Scope agreed: **look/theme** and **layout/menus/shortcuts**.
Scripting and a debug inspector were offered and not chosen.

- Files live in `Customization::dir()` (AppConfigLocation + `/customise`, or
  `PREPRESSPDF_CONFIG_DIR`): `theme.qss` (app-wide Qt style sheet),
  `layout.json` (toolbar, menus, shortcuts), `HOW-TO.txt` (generated guide
  with all item ids, rewritten whenever dev mode writes files).
- Every placeable command is registered in `MainWindow::buildActions` via
  `addItem(id, text, help, shortcut)` into `m_items`. **New commands must be
  registered there** and added to `kDefaultLayout` in `Customization.cpp`,
  otherwise they appear nowhere. Special items: `file.recent` (menus only),
  `pageNumber` (toolbar only), `-` (separator).
- `MainWindow::applyLayout` rebuilds toolbar and menu bar from a `Layout`;
  unknown ids and bad shortcuts are reported in the status bar, a JSON syntax
  error falls back to the default layout.
- The Customise menu is appended by the app and is not configurable, so the
  user cannot lock themselves out. `--safe-mode` ignores both files.
- Files apply whether or not dev mode is on; dev mode adds the editing menu
  entries and live reload (`QFileSystemWatcher`, 150 ms debounce, re-armed
  after every change because editors replace files on save).
- Not customisable yet: Output Preview contents, page box overlay colours
  (hard-coded in `PageCanvas::paintEvent`), toolbar icons (text only).

## Responsiveness (v0.4.1)

The owner reported the app as "really slow and unresponsive" in v0.4.0. Measured
cause (`./build/bench [cmyk.icc]`, 4 cores): `engine::composite` ran on the UI
thread at about 17 ms per megapixel with an ICC profile, i.e. 0.5-1 s per
screen update when zoomed in, repeated for every plate toggle; the colour
transform was rebuilt and the profile files re-read on every update and every
thumbnail. What changed:

- `CompositeWorker` (own thread). Since v0.5.0 it works per tile; see
  "Rendering model".
- `composite()`: transform cached (`transformFor`, shared across threads, built
  with `cmsFLAGS_NOCACHE`), rows split across cores, whole-number arithmetic,
  and each run of equal pixels in a row converted once.
  60 MP flat artwork with a profile: about 1000 ms -> 110 ms. Photographic
  pages gain only the multi-core factor (about 550 ms at 60 MP on 4 cores).
- Thumbnails are composited on the render thread; profile bytes and the system
  monitor profile are cached in `MainWindow`; fit-on-resize is debounced.
- v0.5.0 replaced whole-page rendering by tiles, which removed the pixel
  limit (`kMaxPixels` is gone).

Not verified on the owner's machine or files. If it is still slow, ask which
action is slow and for the file's page size and page count.

## Coordinates and rotation

Everything the UI receives from the engine (page boxes, search rectangles,
inspector bounds) is in **view points**: 1/72 inch, origin top-left of the
rendered area, y down, view rotation already applied (`viewMatrix` in
`Document.cpp`). Pixel = view point x zoom. The UI passes view points back
(`objectsAt`). Never mix in raw PDF coordinates on the UI side.

## Gotchas

- **`fz_try` / `fz_catch` are setjmp/longjmp.** Do not construct C++ objects
  with destructors inside `fz_try` if a MuPDF call after them can throw. Use
  `fz_var` for pointers changed inside the try. Follow the pattern in
  `Document.cpp`.
- **One `fz_context` per Document, no locks.** A Document must be used from
  one thread only (the RenderWorker thread). Multi-threaded rendering needs
  `fz_clone_context` and lock callbacks.
- **MuPDF's pkg-config file on Ubuntu is wrong** (old version, missing libs).
  `CMakeLists.txt` finds the static libs and their dependencies by hand. The
  dependency `-dev` packages listed above are all required for linking.
- `test_ui.cpp` assumes 300 pt test pages and gets positions from
  `PageView::widgetPos`; wait for `PageView::isComplete()` before reading pixels.
- Stale work is dropped by id: tile batches (`RenderWorker::setLatestBatch`),
  composites (`CompositeWorker::setLatestOptions`) and the view generation.
- Zoom is capped at 64x, and lower when the whole column of pages would exceed what a scroll bar can address (`MainWindow::setZoom`).
- QSettings keys: `recentFiles`, `simulationProfile`, `monitorProfile`,
  `coverageLimit`, `showBox/trim|bleed|crop|art|media` (`showBoxes` is the old single switch), `outputPreviewGeometry`, `outputPreviewVisible`,
  `devMode`.
- `pdfx1a_marked.pdf` and `pdfx4_marked.pdf` only carry the markers; they are
  not compliant files. Real conformance tests need the Ghent Output Suite and
  Altona Test Suite (not downloaded; check their licences before committing).
- The MuPDF API differs between releases. Check the headers of the version in
  use before relying on a function.

## Next steps, in order

1. Finish phase 1 (list above). Suggested first: the continuous-scroll /
   visible-region rendering redesign.
2. Phase 2 remainder — fix-ups with before/after preview (output intent,
   RGB/Lab -> CMYK, spot remap, font embedding, page boxes, transparency
   flattening, PDF/X metadata). These write files, so they need the safe-save
   rules listed under phase 3 first. Also PDF/X-6.
3. Phase 3 — `src/edit/`: page operations and object tools. Safety rules:
   every edit on MuPDF's undo journal; rewrite only touched pages' content
   streams; never save in place silently; after save re-open, run a qpdf
   structural check, re-render and pixel-compare (untouched pages identical,
   touched pages differ only in the edited region); abort and keep the
   original on failure; re-run preflight if the file was PDF/X.
4. Phase 4 — in-place text editing with embedded fonts; never substitute a
   font silently when a subset lacks glyphs.

## Working conventions

- Work on a feature branch; do not open pull requests unless the owner asks.
- Add a test with known expected values for every rendering or colour feature.
- Update `README.md` (user guide) and this file when behaviour changes.

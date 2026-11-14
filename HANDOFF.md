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

- **Continuous scrolling and large-sheet rendering.** These are one redesign:
  `PageView` shows exactly one fully rendered page (`MainWindow::m_render`),
  and ink readout, the inspector overlay and search highlights all assume
  that. Continuous scrolling needs a multi-page layout with a render per
  visible page; large sheets need rendering only the visible region instead
  of the whole page (today capped at 60 megapixels by `kMaxPixels`).
- Crop and media box overlays, black-ink simulation, HiDPI rendering.
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
  MainWindow         glue: requests renders, recomposites, menus, status bar
  PageView           scroll area + PageCanvas (image, box overlays, hover)
  OutputPreviewPanel profile, plates, ink readout, coverage. Shown in a floating,
                     non-dockable QDockWidget ("outputPreview") owned by MainWindow:
                     the owner wants it as a separate draggable window
  ProfileDialog      profile management
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
```

### Rendering model (the core idea)

`Document::render` renders once into a pixmap of CMYK + one channel per spot
(`fz_page_separations`, every separation set to `FZ_SEPARATION_SPOT`). The
pixmap colourspace is the document's CMYK output intent if it has one,
otherwise DeviceCMYK, so DeviceCMYK numbers in the file arrive unchanged.
Everything else derives from that buffer on the UI thread:

- ink readout reads the channels (`inkValues`);
- toggling plates or coverage calls `composite` again, no re-render;
- display = spots folded into CMYK via their CMYK equivalents (multiply),
  then lcms2 CMYK -> monitor RGB using the simulation profile.

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
| `meta.title`, `meta.creationDate`, `meta.modDate`, `meta.trapped` | Error | all (Info dict or XMP) |
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
- `meta.*` for X-4: which XMP properties are mandatory.
- `transparency.group`: reported as an error for any group MuPDF opens.
- Not checked at all: rendering intents, halftones (`HTP`), DeviceN details,
  overprint mode rules, X-4 layer (OCG) configuration rules, trap networks,
  `OutputCondition` vs registry contents, X-4p/X-5 external profiles, X-6.

Validation so far: the committed test files, and a PDF/X-3:2002 file made by
Ghostscript (`gs -dPDFX` with a customised `PDFX_def.ps`) which passes with no
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
- `test_ui.cpp` assumes `PageCanvas::margin == 12` and a 300 pt test page.
- Stale renders are dropped by request id (`RenderWorker::setLatestRequest`,
  `MainWindow::m_requestId`).
- Rendered size is capped at 60 megapixels (`kMaxPixels` in `MainWindow.cpp`).
- QSettings keys: `recentFiles`, `simulationProfile`, `monitorProfile`,
  `coverageLimit`, `showBoxes`, `outputPreviewGeometry`, `outputPreviewVisible`,
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

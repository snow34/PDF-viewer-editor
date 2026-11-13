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

Phase 1 (viewer + output preview) is partly done. Phases 2-4 are not started.
Built and tested on Ubuntu 24.04 only (Qt 6.4.2, MuPDF 1.23.10, lcms2 2.14).
The GitHub Actions workflow has never run. Windows/macOS code paths
(`InstallColorProfileW`, ColorSync folders) have never been compiled.

### Done

- Open PDF, page navigation, zoom, fit page, drag-and-drop, recent files.
- Rendering to CMYK + spot channels with overprint simulated.
- Separations list with per-plate show/hide; single plate shown as grey.
- Ink % under cursor, total area coverage, 1/3x3/5x5 sampling.
- Coverage highlight above a limit.
- ICC: simulation profile (output intent or library profile), monitor profile,
  relative / absolute colorimetric display.
- Profile library: import, remove, system-wide install.
- PDF/X version detection (Info dict and XMP), output intent listing.
- Trim/bleed/art overlays, trim size in mm.

### Phase 1 still open

Thumbnails, continuous scrolling, search, rotate, object inspector, crop and
media box overlays, black-ink simulation, automatic monitor profile detection,
bundled press profiles (licence must be confirmed per profile), tile cache and
thread pool for very large sheets, HiDPI rendering, Windows and macOS builds,
vendored pinned MuPDF.

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

## Architecture

```
src/engine/   no widgets; QtGui types only
  Types.h            SeparatedImage, PageBoxes, PageRender, DocumentInfo
  Document           MuPDF wrapper: open, metadata, render page to ink channels
  Compositor         ink channels -> QImage (plate visibility, ICC, coverage); inkValues()
  ProfileLibrary     ICC profile folders, import/remove/system install (lcms2)
src/ui/
  RenderWorker       owns the Document on a worker thread; all MuPDF calls happen there
  MainWindow         glue: requests renders, recomposites, menus, status bar
  PageView           scroll area + PageCanvas (image, box overlays, hover)
  OutputPreviewPanel profile, plates, ink readout, coverage
  ProfileDialog      profile management
tests/
  test_engine.cpp    ink values, overprint/knockout, boxes, PDF/X detection, profiles
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
  `coverageLimit`, `showBoxes`.
- `pdfx1a_marked.pdf` and `pdfx4_marked.pdf` only carry the markers; they are
  not compliant files. Real conformance tests need the Ghent Output Suite and
  Altona Test Suite (not downloaded; check their licences before committing).
- The MuPDF API differs between releases. Check the headers of the version in
  use before relying on a function.

## Next steps, in order

1. Finish phase 1 (list above). Suggested first: thumbnails, object inspector,
   vendored MuPDF + Windows/macOS CI.
2. Phase 2 — `src/preflight/`: rule engine, one check per ISO clause, profiles
   for PDF/X-1a:2001/2003, X-3:2002/2003, X-4; report panel with jump-to-object;
   fix-ups with before/after preview (output intent, RGB/Lab -> CMYK, spot
   remap, font embedding, page boxes, transparency flattening, PDF/X metadata).
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

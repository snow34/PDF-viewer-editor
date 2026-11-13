# Prepress PDF

An open-source PDF viewer for print production: accurate separations, ink
readout and ICC-managed display today, with PDF/X preflight and safe editing
planned. Built with C++, Qt 6 and MuPDF. Licensed under AGPL-3.0.

## What works now

- Open PDF files (File > Open, drag and drop, recent files), page navigation, zoom, fit page.
- **Output Preview**
  - Separations list: Cyan, Magenta, Yellow, Black and every spot colour on the page; show or hide each plate. A single plate is shown in grey.
  - Ink percentage under the cursor for each separation, plus total area coverage. Point, 3 × 3 or 5 × 5 sample.
  - Highlight of every pixel above a total area coverage limit.
  - Overprint is always simulated, as in Acrobat's Output Preview.
- **Colour management**
  - Simulation profile: the document's output intent, or any CMYK profile from the library.
  - Relative colorimetric or absolute colorimetric (paper white) display.
  - Profile library (Tools > Colour profiles): import ICC profiles into the app, remove them, install them for other applications, choose the monitor profile.
- **PDF/X awareness**: the status bar shows which PDF/X version the file claims and its output intent. File > Properties lists all output intents.
- Page box overlay: trim, bleed and art boxes; trim size in millimetres.

Ink values are always the file's own numbers. The simulation profile only
changes how those numbers are shown on screen.

## Not done yet

- Phase 1 leftovers: thumbnails, continuous scrolling, search, rotate, object
  inspector, automatic monitor profile detection, bundled press profiles,
  macOS and Windows builds (the code is portable but only Linux is built and tested).
- Phase 2: PDF/X verification (the claimed version is displayed, not checked) and fix-ups.
- Phase 3: object and page editing with safe save.
- Phase 4: text editing.

## Build (Ubuntu 24.04)

```bash
sudo apt-get install cmake g++ qt6-base-dev libmupdf-dev liblcms2-dev libharfbuzz-dev libopenjp2-7-dev libjbig2dec0-dev libmujs-dev libgumbo-dev libfreetype-dev libjpeg-dev zlib1g-dev
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

```bash
./build/prepresspdf tests/data/ink_patches.pdf
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

`tests/data/make_test_pdf.py` generates the test files, which contain patches
with known ink values, an overprint and a knockout case, and PDF/X markers.

## Layout

- `src/engine/` — document loading, separation rendering, compositing, ICC profile library. No widgets.
- `src/ui/` — main window, page view, Output Preview panel, profile dialog.
- `tests/` — engine tests and a test that drives the real window.

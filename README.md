# Prepress PDF

A free PDF viewer for people who prepare files for print. It shows what a
printing press will actually put on paper: every ink plate, how much ink is
on each spot of the page, and how the colours will look with a given press
profile.

It is open source (AGPL-3.0) and built with C++, Qt 6 and MuPDF.

> **Status:** early version. Viewing and Output Preview work. Checking files
> against the PDF/X standard and editing are planned but not built yet.
> Available for Windows (installer) and Linux (build it yourself). No macOS
> version yet.

## What you can do with it

| You want to… | How |
|---|---|
| Open a PDF | **File > Open**, or drag the file onto the window. Recent files are under **File > Open recent**. |
| Move between pages | **Previous page** / **Next page** in the toolbar, Page Up / Page Down, or type a page number. |
| Zoom | **Zoom in** / **Zoom out**, or hold Ctrl and scroll. **Fit page** shows the whole page. |
| See which inks a page uses | Look at the **Separation** list in the **Output Preview** window. Cyan, Magenta, Yellow, Black and every spot colour are listed. |
| Look at one plate alone | Untick every ink except the one you want. A single plate is shown in grey: darker means more ink. |
| Hide a plate | Untick it in the list. |
| Read the ink percentage at a spot | Move the mouse over the page. The **Ink** column shows the percentage for each ink, and **Total area coverage** shows their sum. |
| Average over a small area | Change **Sample size** to 3 × 3 or 5 × 5. |
| Find areas with too much ink | Tick **Highlight total area coverage above** and set your limit (for example 300 %). Everything above it turns bright green. |
| Preview colours for a press | Pick a profile under **Simulation profile**. If the file has its own output intent, that is used by default. |
| Preview the paper colour | Set **Display** to *Absolute (simulate paper white)*. |
| See trim and bleed | Tick **Show page boxes**. Trim is green, bleed is blue, art is red. The trim size in mm is shown bottom right. |
| Check whether a file says it is PDF/X | Look at the bottom left of the window, or open **File > Properties**. |

### The Output Preview window

Output Preview is a separate window that floats above the main window. Drag
it by its title bar to wherever you like, including a second screen; the app
remembers where you left it. Close it with its ✕ and bring it back with
**Window > Output Preview** (Ctrl+Shift+Y).

### Good to know

- The ink percentages are always the real numbers in your file. Choosing a
  different simulation profile only changes the colours on screen.
- Overprint is always simulated, like Output Preview in Acrobat.
- "Claims PDF/X-4 (not verified)" means the file says it is PDF/X-4. The app
  does not check yet whether that is true.
- If no CMYK profile is selected, colours on screen are only approximate and
  the Output Preview window says so. Ink percentages are still correct.

## How to add ICC profiles

1. Open **Tools > Colour profiles**.
2. Click **Import…** and choose one or more `.icc` or `.icm` files. They are
   copied into the app's own library, so no administrator rights are needed.
3. Close the window. CMYK profiles now appear under **Simulation profile**.

In the same window you can also:

- **Remove from app** — deletes a profile from the app's library. Profiles
  that belong to the operating system are never deleted.
- **Install for other applications** — also copies the profile into your
  system's colour profile folder so other programs can use it.
- **Monitor profile** — choose the profile of your screen. If you don't, sRGB
  is used.

No press profiles are included with the app. Free ones (for example the
FOGRA/PSO profiles) can be downloaded from eci.org and imported.

## How to install on Windows

1. Download `PrepressPDF-…-setup.exe`. Until there is a Releases page, it is
   under **Actions** on GitHub: open the newest run with a green tick, scroll
   to **Artifacts** and download **windows-installer** (you must be signed in
   to GitHub). Unzip it.
2. Double-click the setup file. Windows may show "Windows protected your PC"
   because the installer is not digitally signed: click **More info**, then
   **Run anyway**.
3. Follow the steps. The app is installed in `C:\Program Files\Prepress PDF`
   and appears in the Start menu as **Prepress PDF**.

To remove it, use **Settings > Apps > Installed apps** in Windows.

The installer does not make the app your default PDF program. To open a PDF
with it, start the app and use **File > Open**, or right-click a PDF and
choose **Open with**.

## How to install on Linux (Ubuntu 24.04)

There is no installer yet; you build the app yourself. Run these three
commands in a terminal, in the project folder.

1. Install what the app needs:

```bash
sudo apt-get install cmake g++ qt6-base-dev libmupdf-dev liblcms2-dev libharfbuzz-dev libopenjp2-7-dev libjbig2dec0-dev libmujs-dev libgumbo-dev libfreetype-dev libjpeg-dev zlib1g-dev
```

2. Build it:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

3. Start it (this opens the included example file):

```bash
./build/prepresspdf tests/data/ink_patches.pdf
```

## How to check that it works (Linux)

```bash
ctest --test-dir build --output-on-failure
```

This opens example PDFs with known ink values and checks that the app reads
them correctly.

## What is planned

1. **Rest of the viewer:** thumbnails, continuous scrolling, search, rotate,
   object inspector, a macOS version.
2. **Preflight:** check files against PDF/X-1a, X-3 and X-4, list the
   problems, and fix them (convert colours, embed fonts, set page boxes,
   set the output intent).
3. **Editing:** move, delete and recolour objects; reorder and rotate pages —
   with checks after every save so the file is never damaged.
4. **Text editing.**

## For developers

See [HANDOFF.md](HANDOFF.md) for the architecture, decisions and next steps.

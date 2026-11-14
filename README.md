# Prepress PDF

A free PDF viewer for people who prepare files for print. It shows what a
printing press will actually put on paper: every ink plate, how much ink is
on each spot of the page, and how the colours will look with a given press
profile.

It is open source (AGPL-3.0) and built with C++, Qt 6 and MuPDF.

> **Status:** early version. Viewing, Output Preview and PDF/X checking work.
> Fixing problems automatically and editing are planned but not built yet.
> Available for Windows (installer) and Linux (build it yourself). No macOS
> version yet.

## What you can do with it

| You want to… | How |
|---|---|
| Open a PDF | **File > Open**, or drag the file onto the window. Recent files are under **File > Open recent**. |
| Move between pages | **Previous page** / **Next page** in the toolbar, Page Up / Page Down, or type a page number. |
| Jump to a page | Click its thumbnail in the **Pages** panel on the left. |
| Zoom | **Zoom in** / **Zoom out**, or hold Ctrl and scroll. **Fit page** shows the whole page. |
| Turn the page | **Rotate left** / **Rotate right**. This only turns the view; the file is not changed. |
| Find text | **Edit > Find** (Ctrl+F), type the text and press Enter. Results are highlighted; Enter or **Find next** / **Find previous** moves between them, across all pages. |
| See what an object is made of | Click it on the page. The **Object Inspector** on the right lists everything under that point, topmost first: colour space and colour values, overprint, opacity, the font and size for text, pixel size and resolution (ppi) for images. The top object gets a pink frame. |
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
| Check whether a file really is PDF/X | **Tools > Preflight** (Ctrl+Shift+X). See below. |

### The Output Preview window

Output Preview is a separate window that floats above the main window. Drag
it by its title bar to wherever you like, including a second screen; the app
remembers where you left it. Close it with its ✕ and bring it back with
**Window > Output Preview** (Ctrl+Shift+Y).

### Panels

**Pages**, **Object Inspector** and **Preflight** are panels at the sides of the
window (the last two share the right side as tabs). You
can close them, bring them back from the **Window** menu, or drag them by
their title to another side or out into their own window.

### Good to know

- The ink percentages are always the real numbers in your file. Choosing a
  different simulation profile only changes the colours on screen.
- Overprint is always simulated, like Output Preview in Acrobat.
- "Claims PDF/X-4 (not verified)" means the file says it is PDF/X-4. The app
  does not check yet whether that is true.
- The Object Inspector goes by each object's bounding box and does not take
  clipping into account, so it can list an object that is hidden at that spot.
- "substituted, not embedded" next to a font means the PDF does not contain
  the font and a stand-in is shown. A printer may reject such a file.
- If no CMYK profile is selected, colours on screen are only approximate and
  the Output Preview window says so. Ink percentages are still correct.

## How to check a file against PDF/X (Preflight)

1. Open the PDF.
2. Choose **Tools > Preflight**, or open the **Preflight** tab on the right.
3. The standard the file claims is already selected. Pick another one if you
   want: PDF/X-1a:2001, PDF/X-1a:2003, PDF/X-3:2002, PDF/X-3:2003 or PDF/X-4.
4. Press **Check**. You get **Passed** or **Failed** and a list of problems,
   grouped by kind, each with its page number.
5. Click a problem. The app goes to that page and puts a pink frame around
   the object. The full explanation appears under the list.
6. **Save report…** writes the result as a PDF, a web page, or a JSON file
   for other programs.

What is checked:

| Group | Examples |
|---|---|
| Identification | Is the file marked as the standard you chose? |
| Output intent | Is there exactly one, does it name the printing condition, is the profile a printer profile? |
| Metadata | Title, creation and change date, "Trapped" set to True or False |
| Page boxes | TrimBox or ArtBox on every page, boxes inside one another correctly |
| Fonts | Every font embedded |
| Colour | PDF/X-1a: only CMYK, grey and spot colours. PDF/X-3 and X-4: no plain (device) RGB when printing in CMYK |
| Transparency | Not allowed in PDF/X-1a and PDF/X-3; allowed in PDF/X-4 |
| Annotations | No comments, links or form fields inside the printed area |
| Forbidden features | JavaScript, embedded PostScript, layers (before X-4), JPEG 2000 (before X-4) and others |
| Image resolution | A warning for images below 150 ppi (not a PDF/X rule, but worth knowing) |

Errors make the file fail. Warnings do not: they are things to look at.

**Please read:** this is the app's own check, not a certified validation. It
was written without the official ISO standard documents, and a few rules are
reported only as warnings because their exact wording is not confirmed. A
file that passes here has no problem this app knows how to find; your printer
may still check more. If you also have Acrobat Preflight or PitStop, compare
the results and report differences.

The check can also run without the window, for use in scripts:

```bash
prepresspdf --preflight X-4 --report result.json file.pdf
```

It ends with exit code 0 if the file passed, 1 if it failed, 2 if it could not
be checked.

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
- **Monitor profile** — the profile of your screen. *Automatic* uses the
  profile Windows has assigned to your main screen; on Linux, or if Windows
  has none, sRGB is used. You can pick a specific profile instead.

No press profiles are included with the app. Free ones (for example the
FOGRA/PSO profiles) can be downloaded from eci.org and imported.

## How to change the look and layout (Developer mode)

You can change colours and fonts, decide what is in the toolbar and menus,
and set your own keyboard shortcuts. No programming is needed: you edit two
text files and the running app updates as soon as you save.

1. Switch on **Customise > Developer mode**.
2. Choose **Customise > Open customisation folder**. It contains:
   - `theme.qss` — colours, fonts and spacing. It includes a ready-made dark
     theme: remove the `/*` and `*/` around that block and save.
   - `layout.json` — the toolbar buttons, the menus and the shortcuts.
   - `HOW-TO.txt` — explains both files and lists every item you can place.
3. Edit a file in any text editor and save. The app changes straight away.

Example: a toolbar with only three buttons, and a different shortcut for Open:

```json
{
  "toolbar": ["file.open", "-", "view.zoomOut", "view.zoomIn"],
  "menus": [
    { "title": "&File", "items": ["file.open", "file.recent", "-", "file.quit"] }
  ],
  "shortcuts": { "file.open": "Ctrl+Shift+O" }
}
```

If something goes wrong:

- A mistake in `layout.json` is reported at the bottom of the window with the
  line number, and the standard layout is used until you fix it.
- The **Customise** menu can never be removed, so you can always get back.
- **Customise > Reset to defaults** deletes your two files.
- Starting the app with `--safe-mode` ignores your files once.

Your changes stay in effect when Developer mode is switched off; the switch
only controls the editing tools and the automatic reload.

What you cannot change this way (yet): what the tools do, the contents of the
Output Preview window, and the colours of the page box lines.

## How to install on Windows

1. Go to the [Releases page](https://github.com/snow34/PDF-viewer-editor/releases)
   and download `PrepressPDF-…-setup.exe` from the newest release.
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

1. **Rest of the viewer:** continuous scrolling through all pages, faster
   display of very large sheets, a macOS version.
2. **Preflight fix-ups:** repair what the check finds (convert colours,
   embed fonts, set page boxes, set the output intent), and PDF/X-6.
3. **Editing:** move, delete and recolour objects; reorder and rotate pages —
   with checks after every save so the file is never damaged.
4. **Text editing.**

## For developers

See [HANDOFF.md](HANDOFF.md) for the architecture, decisions and next steps.

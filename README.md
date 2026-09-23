# rr

> Lightweight, distraction-free, aesthetic terminal EPUB reader written in pure C.

```
Il danno                                               Capitolo 3

        Anna  e  sua  madre,  Elizabeth, sedevano fianco a
        fianco  sul sofà nella nostra stanza di soggiorno.
        Anna era tranquilla e controllata come sempre. Sua
        madre  era  piccola,  quasi simile a un uccellino.
        Gli  occhi  e  i capelli neri che aveva passato ad
        Anna  formavano  uno  sconcertante  contrappunto a
        tutto ciò che di diverso c’era in loro.
        Si  vedeva che c’era in lei una vulnerabilità, una
        dolcezza   ancora   attraente.  Due  uomini  molto
        intelligenti  l’avevano  sposata. La sua vivacità,
        la  sua  bellezza,  l’elegante  corpicino dovevano
        aver  avuto,  in passato, il potere di abbagliare.
        Il  suo  viso doveva essere passato dalla bellezza
        della  gioventù  alla  sua sbiadita versione degli
        anni più tardi senza l’autocoscienza o la saggezza
        che avrebbe potuto farla bella nella sua maturità.
        Era,  pensai, una donna non molto intelligente che
        non  era  mai  stata  all’altezza  dei suoi figli.
        Concepii  una  brusca  avversione per Aston, e non
        trovai molto simpatico nemmeno il ritratto dipinto
        da Elizabeth di Anna da bambina. Forse è quella la
        grande  forza di sua madre, pensai: suscita pietà.
        Mentre   continuava   a   chiacchierare,  scavando
        allegramente  sotto  i  piedi  di  sua figlia, era
        Anna,  seduta in silenzio accanto a lei, che delle
        due sembrava la più malevola.

← Pag. 130/344 →                    [ 38% ]                2:34am
```

## Features

- **Paperback Typography**:
  - Full-justified text layout with alternating whitespace distribution to prevent vertical rivers.
  - Centered reading column (adjustable margins and width).
  - True Unicode/UTF-8 awareness using `wcwidth` (proper display of Italian/accented characters, smart quotes, em-dashes, ellipses).
  - Formatting support: Headings, Bold, Italic, Underline, Code, Blockquotes, and scene break dividers (`─── ✦ ───`).
  - Toggle between classic first-line indent and modern paragraph spacing (`p`).
- **Smooth Navigation with Arrows**:
  - `←` / `→` or `↑` / `↓` arrow keys to turn pages.
  - Classic e-reader keybindings (`Space`, `Backspace`, `Enter`, `h`, `j`, `k`, `l`, `Page Up`, `Page Down`).
  - Chapter jumping with `[` and `]`.
- **Interactive Modals**:
  - **Table of Contents (`t` / `Tab`)**: Interactive hierarchical chapter selector with page numbers and anchor navigation.
  - **Search (`/`)**: Full-book case-insensitive search with highlighted occurrences and next/prev match (`n` / `N`).
  - **Bookmarks (`b` / `B`)**: Toggle bookmarks on current page and manage them in a dedicated list.
  - **Jump to Page (`g`)**: Instant navigation to any global page number.
  - **Cheatsheet (`?`)**: In-app keyboard reference overlay.
- **5 Aesthetic Color Schemes (`c`)**:
  - **Default / Clean**: Minimalist terminal colors.
  - **Warm Sepia**: Soothing amber/cream on warm dark background (like Kindle/E-Ink).
  - **OLED Black**: High contrast true-black theme.
  - **Forest Green**: Restful vintage phosphor green.
  - **Nordic Slate**: Deep slate blue with arctic white text.
- **Persistent Reading State**:
  - Automatically remembers the exact page, chapter, column width, justification preference, color theme, and bookmarks for every opened book (`~/.config/rr/state`).
- **High Performance & Portability**:
  - Written in clean, modular C99 with zero memory leaks.
  - Blazing fast: parses entire books in milliseconds.
  - Responsive terminal resizing (`KEY_RESIZE` / `SIGWINCH`).

## Dependencies

- A C99 compiler (`gcc` or `clang`)
- `libzip`
- `libxml-2.0`
- `ncursesw`

On Arch Linux:
```bash
sudo pacman -S libzip libxml2 ncurses
```

On Debian / Ubuntu:
```bash
sudo apt install libzip-dev libxml2-dev libncursesw5-dev
```

## Building & Installation

```bash
# Build the release binary
make

# Install to /usr/local/bin (optional)
sudo make install
```

To build in debug mode with AddressSanitizer and UndefinedBehaviorSanitizer:
```bash
make debug
```

## Usage

```bash
# Open an EPUB book
rr book.epub

# Open directly at a specific page
rr book.epub -p 42

# Open directly at a specific chapter (1-based)
rr book.epub -c 3

# View metadata and Table of Contents without launching TUI
rr --info book.epub

# Show help
rr --help
```

## Controls

| Key | Action |
| --- | --- |
| `→` / `↓` / `Space` / `Enter` / `l` / `j` | Next page |
| `←` / `↑` / `Backspace` / `h` / `k` | Previous page |
| `]` / `[` | Next / Previous chapter |
| `Home` / `End` (or `G`) | First / Last page of book |
| `t` / `Tab` | Table of Contents modal |
| `/` | Search in book |
| `n` / `N` | Next / Previous search match |
| `b` | Toggle bookmark on current page |
| `B` | View bookmarks modal |
| `g` | Jump to page number |
| `w` | Cycle column width (50, 60, 66, 76, 86, Auto) |
| `F` / `J` | Toggle full justification vs left-aligned |
| `p` | Toggle paragraph indent vs blank line spacing |
| `c` | Cycle color theme |
| `?` / `F1` | Help cheatsheet |
| `q` / `Esc` | Quit and save position |
| `Mouse Wheel` / `Click` | Turn pages backward / forward |

## License

Apache License 2.0. See [LICENSE](LICENSE) for details.

<div align="center">

# rr

**A terminal EPUB reader that reads the book, not the archive.**

`rr` is a distraction-free reader for EPUB, written in C99.
It has no runtime, no plugin system, and no opinion about your terminal
font.

</div>

---

## The problem it solves

Almost every EPUB you can download was produced by a conversion tool, and those
tools do not mark chapter titles with `<h1>`. They emit an ordinary `<p>` and
record the intent — *this one is a heading* — in the book's embedded CSS, then
throw away a great deal of debris along the way: the book title stamped in the
margin of all 300 pages, and a printed page number after every paragraph.

Readers that only look at markup get those books wrong. No chapter titles, an
empty table of contents, and the running head repeating until you stop reading.

`rr` resolves the stylesheets. That single decision is what most of this
program is about.

## What it does with that

- **Recovers chapter titles** from the typography the book itself specifies, and
  gives each one an anchor so navigation can reach it.
- **Builds a table of contents** when the book's own navigation is too thin to
  be useful, merging in the original entries rather than discarding them. A book
  with a good navigation file is left completely alone.
- **Removes conversion debris** — folios and running heads — judged across the
  whole book, not one chapter at a time.
- **Caches what it parses.** Chapters and stylesheets are read once. A
  780-page novel opens in about 30 ms; resizing after that is single-digit.

Selector matching is deliberately conservative: anything more ambiguous than a
single tag, class, or id is left alone rather than guessed at. A missed heading
is a small gap. A false bold match on body text is wrong on every page.

## Reading

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
        aver  avuto,  in passato, il potore di abbagliare.
        Il  suo  viso doveva essere passato dalla bellezza
        della  gioventù  alla  sua sbiadita versione degli
        anni più tardi senza l’autocoscienza o la saggezza
        che avrebbe potuto farla bella nella sua maturità.
        Era,  pensai, una donna non molto intelligente che
        non  era  mai  stata  all’altezza  dei suoi figli.
        Concepii  una  brusca  avversione per Aston, e non
        trovai  molto simpatico  nemmeno  il ritratto dipinto
        da Elizabeth  di Anna da bambina. Forse è quella la
        grande  forza di sua madre, pensai: suscita pietà.
        Mentre   continuava   a   chiacchierare,  scavando
        allegramente  sotto  i  piedi  di  sua figlia, era
        Anna,  seduta in silenzio  accanto a lei, che delle
        due sembrava la più malevola.

← Pag. 130/344 →                    [ 38% ]                2:34am
```

## Details worth knowing

- **Justified text** with alternating whitespace distribution, so the column
  has no vertical rivers running down it.
- **Title pages.** Every chapter opens on its own screen carrying only its
  title, with the prose beginning on the next. Duplicate title lines are lifted
  out of the text rather than interrupting it.
- **Unicode.** Cell widths come from `wcwidth`, so accented letters, smart
  quotes, and em-dashes occupy the columns they actually occupy.
- **Two paragraph styles.** Classic first-line indent (`p` toggles), and scene
  breaks rendered as `─── ✦ ───`.
- **Five themes** (`c`): Minimal, Warm Sepia, Pure Black, Forest Green, Nordic
  Slate.
- **Mouse.** Scroll and click work.
- **Your place is kept** per book — page, column width, justification, theme,
  and bookmarks — in `$XDG_CONFIG_HOME/rr/state`, or `~/.config/rr/state`.
- **Responds to resizing** via `SIGWINCH` and reflows in place.

## Install

Needs a C99 compiler and three libraries: `libzip`, `libxml2`, `ncursesw`.

```sh
# Arch
sudo pacman -S libzip libxml2 ncurses

# Debian / Ubuntu
sudo apt install libzip-dev libxml2-dev libncursesw5-dev
```

```sh
make
sudo make install      # optional, /usr/local/bin
```

`make debug` builds with AddressSanitizer and UndefinedBehaviorSanitizer.

## Use

```sh
rr book.epub              # read
rr book.epub -p 42        # open at a page
rr book.epub -c 3         # open at a chapter
rr --info book.epub       # metadata and contents, no TUI
```

## Keys

| | |
| --- | --- |
| `→` `↓` `Space` `Enter` `l` `j` | next page |
| `←` `↑` `Backspace` `h` `k` | previous page |
| `[` `]` | previous / next chapter |
| `t` `Tab` | table of contents |
| `/` `n` `N` | search, then step through matches |
| `b` `B` | bookmark this page, list bookmarks |
| `g` | jump to page |
| `w` | column width |
| `F` | justification |
| `p` | indent or spacing |
| `c` | theme |
| `?` | keys |
| `q` | quit |

`?` inside the reader is always the shortest answer to this table.

## Known limitations

- Headings are inferred from styling, so a body paragraph that is both centred
  and bold may be promoted to a heading. The error is visible but harmless; a
  missed heading is not.
- Extraction is not flow-aware, so a footnote interrupting a sentence reads as a
  paragraph break.

---

<div align="center">

Apache License 2.0 — see [LICENSE](LICENSE).

</div>

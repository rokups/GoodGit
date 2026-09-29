# First-class conflict markers — formal specification (M1, K1)

Status: **draft for review** (P0-04). Implements REBUILD_PLAN §4.10 and §5 M1/K1.
Implementation: `Source/libgg/Markers.cpp` (parser, writer, term algebra).

A first-class conflict is **only file content**. Whether a blob is conflicted, and what its
sides and bases are, is a pure function of the blob bytes plus the path's attributes. There is
no other storage.

---

## 1. Lines

A file is split into lines. A *line* is a maximal byte sequence ending in `\n` (the `\n` is part
of the line), or the final bytes of the file when they do not end in `\n` (an *unterminated*
line). `\r\n` is a line ending in `\n` whose content ends in `\r`; section content keeps its bytes
verbatim, so CRLF and LF lines can be mixed freely and round-trip exactly.

For marker recognition only, a trailing `\r` before the `\n` is ignored.

## 2. Marker lines

Let `L ≥ 7` be a marker length and `c` one of `< | = > + -`.
A line is a *marker line of kind `c` and length `L`* when, after removing the line ending
(`\n` or `\r\n`):

```
marker-line = c{L} [ " " label ]        ; exactly L copies of c, then end or a space
label       = *( any byte except "\n" )
```

`c{L}` followed by another `c` is **not** a marker of length `L` (it may be one of length
`L+1`). `=` markers never carry a label (`=======` followed by a space is not a separator).

Flags inside labels: a label that ends with the token ` [no newline]` (or equals `[no newline]`)
sets the *no-eol flag* of the section the marker introduces (see §5); the token is not part of
the label.

## 3. Regions

### 3.1 Two-sided region (Git diff3, exactly)

```
region2 = open  side-A
          base  side-base
          sep   side-B
          close
open  = marker-line('<', L)          ; label = label of side A
base  = marker-line('|', L)          ; label = label of the base  (MANDATORY)
sep   = marker-line('=', L)          ; no label
close = marker-line('>', L)          ; label = label of side B
side-* = *content-line
content-line = any line that is not a marker line of length L of kind < | = >
```

The base section is mandatory. Git's two-way `merge` style (no `|||||||`) is **not** a
first-class region and is ordinary text.

### 3.2 N-sided region (extended form)

```
regionN = openN side-section *( base-section side-section ) close
openN   = marker-line('<', L) with label "gg " N "-sided conflict" [ " " free-text ]
side-section = marker-line('+', L) *content-lineN      ; label optional
base-section = marker-line('-', L) *content-lineN      ; label optional
close   = marker-line('>', L)
content-lineN = any line that is not a marker line of length L of kind < > + -
```

`N` (decimal, ≥ 2) must equal the number of side sections; there are `N-1` base sections, and
sections strictly alternate side, base, side, …, side. A region with a wrong count is
malformed. Writers use the extended form only for `N ≥ 3`; readers accept `N = 2` too.

### 3.3 Marker length

The opening marker fixes `L` for the whole region: inner markers must have exactly the same
length. `L` is at least 7; when the path has the `conflict-marker-size` attribute, writers use
at least that size (readers accept any `L ≥ 7`).

**Writer rule.** Let `m` be the longest leading run of a single marker character
(`< | = > + -`) found at the start of any content line of any section of the region. The writer
chooses `L = max(7, conflict-marker-size, m + 1)`. Therefore no content line of a written
region is a marker line of length `L`, and parsing is unambiguous.

## 4. Parsing a file

The parser scans lines top to bottom:

1. A line that is an opening marker (`<` run of length `L ≥ 7`, followed by end of line or a
   space) starts a *candidate* region with that `L`. The form is decided by the first
   structural marker of length `L` that follows: `|` → two-sided; `+` with an opening label
   matching `gg N-sided conflict` → extended. Anything else → malformed.
2. The candidate is followed line by line. It is **well-formed** when the structural markers of
   length `L` appear exactly in the grammar order and the closing marker is reached.
3. If another opening marker of the **same** length `L` appears before the candidate
   completes, the candidate is abandoned and scanning restarts at that new opening line.
   (This is what lets a ggui region sitting inside a region plain Git added be recognised.)
4. A malformed or abandoned candidate contributes its opening line as ordinary text; scanning
   continues on the next line. Partial markers, wrong lengths, missing bases, wrong side counts
   and unterminated regions are all ordinary text.
5. Text outside regions is kept verbatim.

A file is **conflicted exactly when it contains at least one well-formed region.** Any edit
that leaves no well-formed region resolves the file.

## 5. In-band edge cases

| Case | Encoding |
|---|---|
| A side (or base) whose content, at the end of the file, has no final newline | The writer adds `\n` so the next marker starts a line, and marks the section with the ` [no newline]` label token on the marker that **introduces** the section: `<` for side A, `\|` for the base, `>` (closing) for side B in the two-sided form; the section's own `+`/`-` marker in the extended form. Readers remove that one `\n` when rebuilding the section. |
| CRLF vs LF sides | Section content is stored byte for byte; nothing is normalised. Marker lines use `\r\n` when the majority of the region's content lines end in `\r\n`, else `\n`. Readers accept both. |
| Empty side / base | The section has zero content lines: its marker is immediately followed by the next marker. An empty section differs from a section containing one empty line (`\n`). |
| Region at the very end of a file | The closing marker line always ends with a line ending. |
| Text after the last region without final newline | Stored as an unterminated last line, as in any file. |

## 6. Eligibility (K1) and the opt-out attribute

A path may hold a first-class conflict only if **all** hold:

- all versions being merged are text: no NUL byte in the first 8000 bytes (Git's heuristic);
- the path has no `filter` attribute (LFS or any other clean/smudge filter), no `binary`/`-diff`/
  `-text` attribute, and is not a symlink or submodule;
- all sides have the same mode;
- the path does not have the attribute `gg-conflicts` set to false (`gg-conflicts=false` or
  `-gg-conflicts` in `.gitattributes`).

For an ineligible path the parser reports "not conflicted" regardless of content, and a rewrite
that would produce a text conflict there treats it as a **non-text conflict** (pre-flight
dialog, §4.10 of the plan).

## 7. Term algebra (N-way merge)

A *merge value* `M = a₀ − r₁ + a₁ − r₂ + … − rₙ + aₙ` has `n+1` adds and `n` removes (terms
are full file contents, or hunks). A plain file is `M = a₀` (`n = 0`).

### 7.1 From a file to terms
A conflicted file with regions `R₁…R_k` becomes a file-level merge value:

1. Let `N` be the largest side count of any region. Pad every region with fewer sides by
   appending (base, side) pairs that are both equal to that region's first base. (Padding is
   `+b − b`, which cancels.)
2. Side `i` of the file = every text outside regions, interleaved with side `i` of each region
   in order. Base `j` of the file likewise.
3. `M = side₁ − base₁ + side₂ − … + side_N`.

A file without regions is `M = content`.

### 7.2 Combining
Merging `theirs` onto `ours` with common ancestor `base`, where each input may itself be a
merge value:

```
merge(base, ours, theirs) = ours + theirs − base
  adds    = ours.adds ⧺ theirs.adds ⧺ base.removes
  removes = ours.removes ⧺ theirs.removes ⧺ base.adds
```

### 7.3 Simplification
1. **Cancel**: while some add equals some remove (byte-equal), delete one of each.
2. **Resolved** when one add remains (and no removes).
3. **All sides agree**: when every remaining add is equal, the result is that add.
4. **Collapse**: while two adds `aᵢ`, `aⱼ` and a remove `rₖ` merge cleanly *by cancellation
   alone* (§7.4 with rule 3 left out: in every hunk `aᵢ` or `aⱼ` equals `rₖ`), replace the three
   by the merged file, then apply rules 1–3 again. This is the same value in fewer terms. It lets
   terms cancel that differ as whole files but cancel hunk by hunk (the same change made in other
   surroundings), which rule 1 alone cannot see: without it a conflict rebased away and back
   keeps such terms and grows each time.

### 7.4 Materialising (writing back)
If §7.3 resolves the value, the plain content is written. Otherwise the file is merged line by
line:

1. Reference = the first remove `r₁`. Every add and remove is diffed against `r₁` (line diff).
2. Reference lines unchanged in **every** term are anchors; anchors are copied as plain text.
3. Between anchors, each term contributes its lines for that span, giving a hunk-level merge
   value. The hunk is resolved when §7.3 rules 1–3 resolve it.
4. A resolved hunk is written as plain text. An unresolved hunk becomes a region with **every
   term of the file**, not the hunk's simplified terms: two sides (`n = 1`) → the two-sided diff3
   form (side A = `a₀`, base = `r₁`, side B = `a₁`); more sides → the extended form, sections in
   term order `a₀, r₁, a₁, …, rₙ, aₙ`. Every region of a file thus has the same sides in the same
   order, and §7.1 rebuilds each term from its own sections. (Simplifying each hunk on its own
   gave regions with different sides, and reading the file back paired one term's side in one
   region with another term's in the next: a conflict whose sides no commit had.)
5. Adjacent changes (no anchor between them) belong to the same hunk.

### 7.5 No nesting
Regions are built only from terms, and terms never contain regions of their own (§7.1
flattens them), so files written by ggui never contain nested markers.

### 7.6 Resolving one pair of sides with a merge tool
A two-sided file (`M = a₀ − r₁ + a₁`) is resolved directly by an external merge tool: index
stages 1–3 are `r₁` (base), `a₀` (ours), `a₁` (theirs). For an N-sided file (`N ≥ 3`), the
"Resolve with merge tool" action instead resolves one adjacent pair of sides at a time. For
pair `k` (0-based, sides `k` and `k+1`, `0 ≤ k < N−1`), stages 1–3 are `rₖ₊₁` (base), `aₖ`
(ours), `aₖ₊₁` (theirs).

If the tool succeeds, its result `R` replaces the two terms `aₖ` and `aₖ₊₁` by the single term
`R` at position `k`, and the base `rₖ₊₁` between them is dropped: `M` loses one add and one
remove. Materialising the new value (§7.4) writes the file back with one side fewer; it stays
a first-class conflict when more than one add remains. For example, resolving pair 0 of a
3-sided file `M = a₀ − r₁ + a₁ − r₂ + a₂` leaves a 2-sided file `M' = R − r₂ + a₂`: terms
`{R, a₂}` with base `r₂`.

If the tool gives up, the working tree file is left byte-identical and the index is reset to
HEAD for the path (stages 1–3 removed).

## 8. Plain Git regions next to ggui regions

- While a path has index stages 1–3 (a native Git conflict), it is a **native** conflict and the
  parser is not used for it. The Changes panel shows it under Conflicted from the index.
- After the user resolves Git's conflict and the stages are gone, the file content is parsed
  with §4. Any ggui region that is still well-formed is a first-class conflict again; Git
  markers left in the file are text unless they happen to form a well-formed diff3 region
  (then they are a first-class conflict too, which is what another ggui user would see).
- Parser rule §4.3 handles a ggui region nested inside Git's own markers: the inner region is
  recognised, Git's lines are text.

## 9. Worked examples

`⏎` marks `\n`, `␍` marks `\r`. Labels are illustrative.

**E1 – two sides.** ours `x=1⏎`, base `x=0⏎`, theirs `x=2⏎`:
```
<<<<<<< side 1⏎
x=1⏎
||||||| base⏎
x=0⏎
=======⏎
x=2⏎
>>>>>>> side 2⏎
```
Terms: `x=1⏎ − x=0⏎ + x=2⏎`.

**E2 – marker-like content.** Side 1 contains the line `=======⏎` and `<<<<<<<<⏎` (8). Then
`m = 8`, `L = 9`:
```
<<<<<<<<< side 1⏎
=======⏎
<<<<<<<<⏎
||||||||| base⏎
=========⏎
>>>>>>>>> side 2⏎
```
(base and side 2 empty). `=======` and `<<<<<<<<` are content: they are not markers of length 9.

**E3 – missing final newline.** At end of file: ours `a` (no newline), base `b⏎`, theirs `c`:
```
<<<<<<< side 1 [no newline]⏎
a⏎
||||||| base⏎
b⏎
=======⏎
c⏎
>>>>>>> side 2 [no newline]⏎
```
Taking side 1 yields `a` without newline.

**E4 – CRLF side.** ours `v=1␍⏎`, base `v=0⏎`, theirs `v=2⏎`: content bytes are kept; one of
three content lines is CRLF, so marker lines use `⏎`.

**E5 – empty side (deletion vs edit).** ours deleted the hunk, base `k⏎`, theirs `k2⏎`:
```
<<<<<<< side 1⏎
||||||| base⏎
k⏎
=======⏎
k2⏎
>>>>>>> side 2⏎
```

**E6 – three sides.** Rebasing E1's commit onto a parent where `x=0⏎` became `x=3⏎`:
`merge(base=x=0⏎, ours=x=3⏎, theirs=E1)` = adds `[x=3⏎, x=1⏎, x=2⏎]`, removes
`[x=0⏎, x=0⏎]`. Nothing cancels, so the extended form is written:
```
<<<<<<< gg 3-sided conflict⏎
+++++++ side 1⏎
x=3⏎
------- base 1⏎
x=0⏎
+++++++ side 2⏎
x=1⏎
------- base 2⏎
x=0⏎
+++++++ side 3⏎
x=2⏎
>>>>>>> end of conflict⏎
```

**E7 – cancellation (reorder back).** Rebasing the E6 commit back onto the parent with `x=0⏎`:
`merge(base=x=3⏎, ours=x=0⏎, theirs=E6)` adds `[x=0⏎, x=3⏎, x=1⏎, x=2⏎]`, removes
`[x=3⏎, x=0⏎, x=0⏎]`; cancelling `x=0⏎` and `x=3⏎` leaves `x=1⏎ − x=0⏎ + x=2⏎` → E1 again.

**E8 – resolution by dropping the cause.** If side 2 of E1 is `x=0⏎` (the change is dropped),
`x=1⏎ − x=0⏎ + x=0⏎` cancels to `x=1⏎`: the file is resolved.

**E9 – malformed.** A region with `<<<<<<<`, `=======`, `>>>>>>>` but no `|||||||` is text; so
is one that never closes.

**E10 – opt-out.** `.gitattributes` has `docs/*.md gg-conflicts=false`. `docs/a.md` containing E1
is not conflicted; a rewrite that would conflict on it stops in the pre-flight dialog.

**E11 – Git region around a ggui region.** Plain `git merge` wrapped Git's two-way markers
around part of E1: `<<<<<<< HEAD⏎` + E1 + `=======⏎ … >>>>>>> other⏎`. While the index has
stages, it is a native conflict. If the user commits that text as-is, the parser abandons Git's
opening (same `L`, a new opening follows) and recognises E1; Git's remaining lines are text.

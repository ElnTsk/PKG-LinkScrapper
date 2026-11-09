# PKG-LinkScrapper (C++)

PS4/PS5 game link hunter. Takes game names, searches engines and release sites,
crawls the pages those results point at, pulls file-host / `.pkg` links, probes
every one of them, and prints a single list.

C++20, WinHTTP, no runtime dependencies. **The deliverable is one file:**
`PKG-LinkScrapper.exe` - a windowed app with the manifest, the source list and the network
defaults compiled in. Nothing else needs to sit beside it.

## the window

Double-click `PKG-LinkScrapper.exe`.

| control | what it does |
| --- | --- |
| Games box | one title per line, or several separated by `;` |
| Ctrl+Enter | start the hunt (the Hunt button does the same) |
| console dropdown | `Both`, `PS4`, `PS5` |
| `links/game` | rows to keep per game - a part-set counts as one row |
| `match` | name-match strictness, 0.5 loose to 1.0 exact |
| verify links | probe every link for liveness and size |
| recommended only | show only links from a trusted host/source, or one already proven alive |
| dark | dark titlebar, header, grid, fields and details pane; remembered between runs |
| parts | a set row is a tree node - click the cell, or `Right` / `Left`, to drop its members underneath |
| Hunt / Stop | start a run / abandon it |
| Re-check | probe only the rows still `unverified`, without crawling again |
| Download | pull the selected row - asks for a folder first, then shows MB/s |
| Export | save the rows currently visible; format comes from the file extension |
| Sources | open the sources window - every source, the urls it crawls, what it found last run, park and restore |
| filter box | live filter across every column |

Nothing pops up when you hover - the balloons were taken out on request. What
each thing does is said in the window itself: the two numeric fields carry
labels because "25" and "0.72" explained nothing on their own, the hint line
states the names-box rules, the status bar narrates every action
(`parts shown - 10 files`, `recommended only`, `18 unverified - press Re-check`,
`light mode`), and the details pane spells out the selected row.

Row text is coloured by state: green `alive`, amber `stub`, `partial` or
`gated`, red `dead`/`unreachable`/`failed`, violet `blocked`, grey for pages,
containers and anything unverified.

### dark mode

`dark` flips the whole window, not just the list:

| surface | how |
| --- | --- |
| titlebar and caption | `DWMWA_USE_IMMERSIVE_DARK_MODE` plus a `SWP_FRAMECHANGED` so it repaints |
| window behind the controls | `WM_ERASEBKGND` fills with the palette brush - the stock class brush is light grey and showed through as white bands |
| statics, edits, buttons, listbox | `WM_CTLCOLOR*` returning the palette brushes, on top of `SetPreferredAppMode` |
| combo and checkboxes | owner-drawn - Win10 has no dark check glyph and no dark combo field |
| column header and rows | the header control is subclassed and painted directly; `NM_CUSTOMDRAW` on the list view for the rows |
| details, status bar | field brush + `SB_SETBKCOLOR` |

Two things stay light on Windows 10 and are not fixable per window: the
scrollbar and the right-click menu follow the system colours. Windows 11 picks
both up from the same theme call.

The choice is written to `%APPDATA%\PKG-LinkScrapper\theme.txt`, and a first run with no
file there follows `AppsUseLightTheme` from the registry.

Results land in the grid as they are found, not when the run ends. Each link
appears the moment it is recorded with state `unverified`, and the same row is
updated in place once its probe comes back as `alive`, `stub`, `blocked` or
`dead`. The grid repaints every 300 ms so a fast crawl does not flicker, and the
live count is capped at the limit you set so it never shrinks at the end. Click a
column header to sort by it, again to reverse. Recommended rows lead the list.
Double-click a row opens the link;
right-click gives Open link / Copy URL / Copy row, plus Open all parts / Copy all
part URLs on a row that is a split set (see *part sets*). The status bar shows the
current step, the row count and the loaded sources.

Stop closes every in-flight request and sets the abort flag that each crawl loop,
engine page loop and redirect hop checks. Measured 0.12s from the click to the
Hunt button re-enabling, then the process sits idle (0.08s CPU over 5s) instead
of working through the rest of the queue.

Titles run three at a time and each is verified as it lands, so a three-title
run finishes in about two minutes rather than five.

Arguments are optional: `PKG-LinkScrapper.exe "elden ring" "horizon zero dawn" --console ps5`
fills the box, one title per line, and starts straight away; an unquoted phrase is
treated as one title, not several.

## console build

`PKG-LinkScrapper-cli.exe` is the same engine with a console front end, for pipes
and scheduled jobs. It comes out of the same build; the windowed exe takes game
names from its arguments but none of the flags. Run the cli with no arguments
and it opens a prompt that waits for input.

```
PKG-LinkScrapper-cli.exe "elden ring" "god of war ragnarok" --console ps4
PKG-LinkScrapper-cli.exe -i games.txt --console both --format json -o found.json
type games.txt | PKG-LinkScrapper-cli.exe --stdin --console ps5
PKG-LinkScrapper-cli.exe --sources
```

Names work on one line too, separated by `;`. A shortcut that passes arguments
should add `--pause` so its window does not vanish when the run ends.

## what it does with each title

1. Builds engine queries from every enabled `search` source and fetches every
   enabled `index` source (site search or fixed URL).
2. Crawls the result pages, extracting anchors, paste links and bare `.pkg` /
   `.exfat` URLs.
3. Follows shorteners, and in-site redirect handlers when the source enables
   `resolve_redirects`.
4. Scores every candidate title against the title you asked for, drops other
   platforms (PS2/PS3/PSP/Xbox/Switch) and the console you did not ask for.
   Compound spellings count as the same title: `Astrobot` finds `Astro Bot`,
   `spiderman 2` finds `Spider-Man 2`, `gtav` finds `GTA V`.
5. Probes every kept link (`HEAD` with `Range: bytes=0-0`, `GET` fallback) and
   records alive / dead / blocked / container with size where the host reports it.

## options

| flag | meaning |
| --- | --- |
| `--console ps4\|ps5\|both` | console filter, default `both` |
| `--threshold 0..1` | name match strictness, default `0.72` |
| `--limit n` | max hits per game, default `40` |
| `--max-fetch n` | max pages crawled per game, default `14` |
| `--no-verify` | skip liveness probing |
| `--refresh` | probe every url, including ones already remembered as good |
| `--trusted-only`, `--recommended` | only list rows carrying the `recommended` label |
| `--forget-good` | delete the learned good-link store |
| `--pause` | wait for enter before exiting (for shortcuts that pass arguments) |
| `--timeout ms` | request timeout, default `15000` |
| `--concurrency n` | parallel requests, default `10` |
| `--format table\|json\|csv\|tsv` | output format |
| `-o, --out file` | write the listing to a file as well |
| `--config file\|dir` | extra config layer, merged last |
| `--sources` | list loaded sources and which file each came from |
| `-v, --verbose` | per-source detail on stderr |

Progress and errors go to stderr; the listing goes to stdout, so `> out.txt` works.

## states

| state | meaning |
| --- | --- |
| `alive` | 2xx/3xx on a ranged probe, or read from `good.tsv` when that entry saw at least a megabyte of bytes |
| `stub` | the url names a file (`.pkg`, `.rar`, `.zip`…) but the host answers with a small html page - parked or removed |
| `gated` | the share page is real and names the file and its size, but the download url only appears after a challenge - Cloudflare Turnstile or similar. The page is not the file, so the row is not downloadable and is not recommended |
| `shell` | the host answered `200` with **exactly its own front page** - the same bytes as `https://host/`. javascript builds the file list after the page loads, so nothing in the response can prove the link exists. `gofile.io` is the usual one |
| `dead` | 404/410 |
| `blocked` | 401/403/451 — host refuses bots or region |
| `rate-limited` | 429 |
| `container` | filecrypt-style container page, alive, needs its captcha to reveal files |
| `partial` | a part set with no live member left - some parts are dead or unreachable |
| `unreachable` | no response, timeout, DNS failure - and the ping could not reach the host either |
| `page` | release page kept because no direct host link was exposed on it |

## config

Sources are JSON. Layers merge in this order, later wins by source name:

1. built-in defaults compiled into the exe - always applied first
2. `<exe dir>\config\sources.json`
3. `<exe dir>\config\sources.d\*.json`
4. `%APPDATA%\PKG-LinkScrapper\sources.json`
5. `%APPDATA%\PKG-LinkScrapper\sources.d\*.json`
6. `--config <file|dir>` or `LINKSCRAPPER_CONFIG`

A layer may be an object with `sources`, an array of sources, or a single source.
`"disable": ["psxhax"]` turns a built-in off; `"enabled": false` inside a source
does the same. Saving a source with a built-in's name replaces it, and a source
the file does not mention keeps its built-in definition - so a file written by an
older build still picks up sources added since.

```json
{
  "defaults": { "match_threshold": 0.7, "max_fetch_per_game": 20, "good_ttl_days": 7 },
  "disable": ["pkg-zone"],
  "trusted_hosts": ["dlget.ir", "1fichier.com", "archive.org"],
  "sources": [
    { "name": "my-list", "type": "index", "urls": ["https://rentry.co/abc123"], "depth": 1 },
    { "name": "my-search", "type": "search", "engine": "bing",
      "query": "\"{q}\" {console} pkg", "pages": 1, "depth": 1 },
    { "name": "my-trusted-index", "type": "index", "trusted": true,
      "urls": ["https://example.com/ps5/"], "depth": 1 }
  ]
}
```

**search** sources run a query template through `duckduckgo`, `bing` or `searx`
(`searx` needs `"instances": [...]`). **index** sources fetch `urls` and/or
`search_url` — with `paged_url` for pagination. **catalog** sources fetch a JSON
document of game entries and answer from it directly - see *catalogs* below.

Placeholders: `{q}` (URL-encoded title), `{q_raw}`, `{console}` (`PS4`/`PS5`),
`{page}`. With `--console both`, every search template runs once per console.

Per-source knobs: `pages`, `depth` (0 listing only, 1 fetch matching result
pages, 2 one hop deeper), `resolve_redirects`, `keep_pages`, `enabled`.

## shipped sources

| source | kind | what it gives |
| --- | --- | --- |
| `ddg` | search | duckduckgo lite, html fallback |
| `bing` | search | bing html, RSS fallback, `ck/a` wrappers decoded |
| `ddg-pastebins` | search | rentry / pastebin / controlc lists |
| `dlpsgame` | index | site search, 3 pages; links often JS-decoded so the release page is kept |
| `superpsx` | index | site search, 2 pages, `/dll-…` redirect handlers followed |
| `psxhax` | index | tag page browse |
| `pkg-zone` | index | retail and fake pkg categories - `https://pkg-zone.com/?category=pkg` and `?category=fpkg` |
| `p30day` | index | site search, 3 pages - PS5 posts carry plain anchors to PSN `.pkg` parts on `dlget.ir` and multi-part `.rar` sets on `thesuperboffers.com` |
| `p30day-telegram` | index | `t.me/s/p30day_ps5_info2?q=` channel search, two hops: post -> article -> files |
| `archive-org` | index | `advancedsearch.php?q={q} pkg&output=json` -> identifiers -> `archive.org/download/<id>/` file listings |
| `archive-collections` | index | the seven public `ps4-fpkg-collection-english-*` directories listed directly (c, j, q, v, w, x, z) |
| `pegasus-dlps` | catalog | `pegasus-dl.dev/catalogs/dlps.json` - 1710 PS5 titles, 31k mirror links (akirabox, vikingfile, rootz, 1fichier, mediafire…) |
| `pegasus-pippo` | catalog | exFAT dumps and fpkgs with backports, ~920 PS5 titles |
| `pegasus-pfs` | catalog | ffpfs/pkg builds, ~888 PS5 titles; link names carry the part number |
| `pegasus-zer0` | catalog | zer0game releases, ~57 PS5 titles |
| `github-ps4db` | catalog | `huggingfacer04/ps4-games-database` - 6443 PS4 titles with mirror links (1fichier, filecrypt, mediafire, romsfun), dlpsgame-derived |
| `pegasus-ps4` | catalog | `M3hmetSa1t/pegasus-ps4-collection-catalog` - 866 PS4 fpkgs on archive.org, direct `.pkg` urls; most items need an archive.org login and verify as `blocked` |
| `prosperopatches` | index | `prosperopatches.com` search API -> titleid -> tracker page: patch versions, sizes, required firmware. The patch download url only appears after a Turnstile check, so the row opens the tracker rather than pretending to hold the file |

`index` sources that return JSON identifiers use `item_url` to turn each one into
a page worth crawling: `"item_url": "https://archive.org/download/{id}/"`. The
identifier and the `title` field are both scored against your game name.

Disabled but kept in the file, with the reason in `_comment`:

- `psxhax-forum` - XenForo search answers 403 without a session cookie
- `arabicps4games` - titles sit in headings rather than anchors and the links are captcha link-lockers

`p30day` PS4 posts often say the download links were removed; those come back as
`page` rows rather than file rows, which is the honest answer.

## catalogs

A catalog is a JSON document of named entries that already carry mirror links -
no crawling, no search page. The type exists because the PS5 scene keeps its
lists in exactly this shape: Pegasus DL ships four of them as its default
sources, and they are the four `pegasus-*` entries above.

```json
{
  "name": "my-catalog",
  "type": "catalog",
  "console": "PS5",
  "urls": ["https://example.com/catalog.json"],
  "depth": 1,
  "keep_pages": false
}
```

Both common shapes are read:

```json
{"packages": [
  {"title": "Ultros", "titleId": "PPSA16280",
   "downloadSource": "https://dlpsgame.com/ultros-ps5/",
   "downloadLinks": [{"name": "USA Base - Akirabox", "url": "https://akirabox.to/…"}]}
]}
```

```json
[{"name": "#KILLALLZOMBIES", "page_url": "https://dlpsgame.com/…",
  "download_links": {"mediafire": ["https://filecrypt.cc/Container/…"],
                     "1file": ["https://1fichier.com/?…"]}}]
```

- entries are scored on `title` plus `titleId`, so `PPSA16280` finds the game too
- a link's own `name` is kept as a tag: `Ultros [USA Base - Akirabox]`, and
  `Alan Wake II [LZ4 · Viki - pt.1]` - the variant and part number are the only
  place that information exists
- `posterUrl` and `downloadSource` are not links; the source page is only used to
  put the row in front of the right console
- the document is parsed once per process and kept, so a second hunt in the same
  window does not download 6 MB again; restart the app to see a refreshed catalog
- `console` labels the entries `PS4`/`PS5` and is filtered by the console picker
- everything a catalog emits is still probed - a catalog entry is a claim, not a
  verdict

`github-ps4db` and `pegasus-ps4` are GitHub raw files, so they answer without the
site's HTML and without Cloudflare. That matters on the PS4 side: dlpsgame's own
buttons point at `downloadgameps3.net/archives/…`, which answers `403` to
everything that is not a full browser, so the site crawl can only keep the
release page. The database carries the same releases with their real 1fichier /
mediafire / filecrypt mirrors instead.

Most `pegasus-ps4` items are `access-restricted-item` on archive.org - the file
is real but needs a logged-in archive.org session to download, so those rows
verify as `blocked` with the true size. The seven unrestricted items are also in
`archive-collections`, which is why the same game can show one blocked catalog
row and one alive directory row.

## the sources window

**Sources** on the toolbar opens a modeless window that lists every source the
tool knows about - live or parked - with the urls it will crawl and what each
one contributed to the hunt on screen:

```
20 sources  -  18 live  -  2 parked  -  32 urls ready  -  26 rows from the last hunt

Source          Type    State   Pages  Depth  Trust     Yield  Links it crawls
ddg             search  live        1      1              0    duckduckgo: "{q}" {console} pkg download
p30day          index   live        3      1             20    https://www.p30day.ir/?s={q}   …/page/{page}/?s={q}
pegasus-dlps    catalog live        1      1              7    https://pegasus-dl.dev/catalogs/dlps.json
psxhax-forum    index  parked       1      2              -    https://www.psxhax.com/search/?keywords={q}…
```

| button | what it does |
| --- | --- |
| Park / restore | takes the selected source out of the hunt, or puts it back. Writes the whole list to `%APPDATA%\PKG-LinkScrapper\sources.json`, so it is still that way next run |
| Copy all links | every source name and every url it crawls, grouped and on the clipboard |
| Write config | the same file, without changing any setting - the ready-made list dropped where the loader looks |
| Open folder | `%APPDATA%\PKG-LinkScrapper` in Explorer |
| Reload | re-read the file, the trusted lists and `good.tsv`, then rebuild the grid |
| Restore shipped | asks first, then rewrites the file from the built-in defaults - parked sources come back and hand edits are replaced |

Double-click a row opens that source's first link in the browser. `Yield` is
counted from the rows currently on screen, so a source that has gone quiet shows
`0` and gets parked without guessing.

`renderConfig` writes the file in exactly the shape `loadConfig` reads: a parked
source carries `"enabled": false` and there is no `disable` list, so one file
describes the whole state. The built-in list is applied before any file, so a
file that omits a source leaves the built-in definition in place; parking writes
the full list because `Restore shipped` has to be able to put every one back.

Nothing else is shipped as a source because nothing else answers. Probed on the
same day: `downloadgameps3.net` sits behind Cloudflare and is blocked in the
config, `psx-place` and `gamehag` return `403`, `ps4iso`, `ps5games.work`,
`psx-trend`, `storeps4`, `thegamespk` and `darkombat` do not resolve,
`gamever5`/`pspbox`/`romulation` serve pages with no file links in them, and
`arabicps4games.github.io/indexps4page1.html` is a `404` - which is why those
last two sources stay parked in the list rather than deleted from it.

The hosts the catalogs point at were probed one by one, not assumed: `akirabox`,
`vikingfile`, `rootz`, `1fichier`, `mediafire`, `buzzheavier` and `archive.org`
answer and are already classified; `datavaults.co` and `1024terabox.com` answer
`200` with their own POST download form (so they read as `gated`, and the form
is walked only by hand in a browser); `datanodes.to` and `filekeeper.net` answer
`404` on even the freshest entries, so their rows settle as `stub`; `fuckingfast`
and `link-vault` answer `403` to bots, so their rows settle as `blocked`.

## hosts that hand out pages instead of files

`shareServesPage` in [src/hosts.cpp](src/hosts.cpp) lists the domains whose
share url is a web page rather than bytes - `gofile.io`, `vikingfile.com` and
its redirect `vik1ngfile.site`, `1fichier.com`, `rootz.so`, `mocha.my`,
`mediafire.com`, `buzzheavier.com`, `akirabox.*`, `mixdrop.*`,
`drive.google.com`, `uploadhaven.com`, `ddownload.com`, `qiwi.gg`,
`thesuperboffers.com`. Two things follow from being on that list:

1. **The good store never vouches for them.** A url recorded as alive is
   normally trusted for `good_ttl_days` without a second probe; a page-style
   host is re-read every run, because what it records is the size the *page*
   claims, not bytes anyone received.
2. **The body is read whatever the header claims.** `vikingfile.com` heads a
   658 KB share page with `Content-Length: 162 GB` - the size of the file
   behind it - so a length test alone cannot decide. The read looks for the
   size in the markup, a "file is gone" notice, the front-page shell, and the
   captcha markers (`turnstile`, `challenges.cloudflare`, `recaptcha`,
   `hcaptcha`), which sit at the foot of the document, so a suffix range asks
   for the last 16 KB when the first read did not reach them. Once per domain,
   remembered for the run.

The verdicts come out as `gated` (a challenge stands in the way - `vikingfile`)
or `shell` (the response is the site's own front page - `gofile`). Neither is
recommended, neither is offered to the downloader, and the url is dropped from
`good.tsv` the moment either is decided, so the cache cannot bless it again.

The downloader refuses the same thing from its own side
[PKG-LinkScrapper-cpp/src/download.cpp](src/download.cpp): if the response is
`text/html` and the length is under a megabyte, the transfer is marked `failed`
with *the host served its web page, not the file - open the link and use its
download button*, and **nothing is written to disk** - no `.part`, no
600 KB file named after the share id. A large body with a wrong content type
still streams, because that is how plenty of hosts serve real bytes.

```
1 file(s)  -  0 active  -    -  0.0 MB/s  -  1 failed  -  saving to …\Temp\PKG-LinkScrapper-test

File      State    Done  Total  MB/s  Note
2GYRai    failed         ?            the host served its web page, not the file - o…
```

## walking the host's own download page

One host in that list can be finished properly. `1fichier.com` uses no
challenge: its share page carries a form, and posting that form is how the site
hands over the file -

```
POST https://1fichier.com/?<code>        op=download2&dl_no_ssl=on
     -> <a href="http://c-4.1fichier.com/<token>">Start your download</a>
     -> GET that: 206 application/octet-stream
        Content-Disposition: attachment; filename="… PPSA21567 FIXED.rar"
        Content-Range: bytes 0-2048/39843319025          (37.1 GB)
```

[src/resolve.cpp](src/resolve.cpp) does that walk, and only when **Download** is
pressed. Never during a hunt: the link is single use and 1fichier queues repeat
pulls on the free tier, so spending a token on every row would leave nothing for
the download she actually wants.

| | |
| --- | --- |
| `resolveDirect(url)` | posts the form, takes the mirror link - a host shaped `c<n>.1fichier.com` or `d<n>.1fichier.com`, never the site itself - and reads the filename the page prints |
| `resolvableHost(url)` | which hosts have a walk implemented. `1fichier.com` today |
| on success | the queue gets the mirror url and the real name, so the file lands as `[DLPSGAME.COM] - 01.018 PPSA21567 FIXED.rar` instead of `7tvp6hgmj42plxlm056y` |
| on failure | nothing is queued and the status bar carries the host's own reason - *the host is holding this link in its free-tier queue - try again in a few minutes* |

A `gated` row is offered to Download when its host has a walk and refused when it
does not, which is the difference between `1fichier` (one form away) and
`vikingfile` (a captcha away). `rootz.so` and `mocha.my` serve shells with no
form and no link in the document - nothing to walk, so the page guard is what
you get.

## what a row is called

A link's title used to be whatever the anchor said, which on a release page
means the shop's button text: `Gofile`, `Mirror`, `1File (Multi)`. That is not
a title, so `titleFromLink` in [src/match.cpp](src/match.cpp) decides between
three cases:

| anchor text | row title |
| --- | --- |
| describes the file - `Elden Ring Update v1.21` | kept as it is |
| names the shop - `Gofile`, `Mirror 3`, `Download via Mega`, `1File (Multi)` | the page's own title, which names the game |
| names the shop *and* the kind - `1File (Multi)`, `Update v1.21` on a host label | page title plus the label as a tag: `Call of Duty Black Ops … [1File (Multi)]` |

`isHostLabel` only fires on short labels (40 characters or less) and on a known
brand, so a real release title can never be swallowed by the rule - that case is
a named test. The Host and Source columns already say where the file lives, so
the shop name does not need to be in the title as well.

## remembered dead

Every link that probes as `stub`, `dead` or `unreachable` is appended to

```
%APPDATA%\PKG-LinkScrapper\dead.tsv
```

one line each: url, state, host, title, timestamp. The list is loaded before a
hunt starts and any url in it is dropped at record time, so a parked mirror or a
removed file is never crawled, never probed and never shown again. Right-click a
row for **Never show this link again** to add one by hand, whatever its state.

The status bar shows how many are remembered. To start over:

```
PKG-LinkScrapper-cli.exe --clear-dead          # or delete the file
```

Suppression pays for itself. Same two titles, same settings, second run:

| run | hits | alive | stub | dead |
| --- | --- | --- | --- | --- |
| first, empty store | 80 | 52 | 21 | 3 |
| second, 24 remembered | 80 | 76 | 0 | 0 |

The mirror image of this file is `good.tsv`, which remembers what *does* work -
see *recommended links*. `%APPDATA%\PKG-LinkScrapper\theme.txt` is the third file there
and holds one word, `dark` or `light`.

## the popup

Once a hunt has produced rows, the window shows a message before you can click
anything else:

> To download double click to open link, If its blocked you can still access
> them by double clicking.

It appears once per program run, not once per hunt.

## getting files, not pages

Four things stand between a release page and a direct link, and the tool does
all of them before it shows you a row:

1. **base64 payload attributes.** WordPress link-hider plugins put the real
   anchors in `data-payload="..."` and swap them in with javascript. The
   extractor decodes those and harvests the anchors inside, two rounds deep.
   This is how dlpsgame's `akirabox` / `vikingfile` / `buzzheavier` / `gofile`
   links surface.
2. **redirect handlers.** In-site download buttons (`/dll-…`, `/go/`, `/out/`)
   are followed when a source sets `resolve_redirects`, and link lockers are
   resolved to their target before classification.
3. **share-path test.** Being on a file host is not proof of a file - cover art,
   banners and guide pages live on the same domains. A URL counts as a file only
   if it ends in a file extension, carries a share marker (`/file/`, `/f/`,
   `/users/`, `/archives/`), is a query-id share (`1fichier.com/?abc`), or is a
   bare id segment (`buzzheavier.com/eqfjuyld9vxm`). Anything else is crawled as
   a page, and images, css and video are dropped outright.
4. **companion link pages.** SuperPSX renders its download buttons on a second
   page - the game post only carries `https://superpsx.com/dll-<code>/` under a
   generic label, and the buttons themselves are javascript. [src/crawl.cpp](src/crawl.cpp)
   `harvestLinkPages` spots same-site `/dll-` links, fetches up to three of them
   and keeps every file-bearing anchor inside, tagged with the button it came
   from:

   ```
   PS5  alive  recommended  102GB   vikingfile.com   God of War Ragnarök PS5 - … [Viki]
   PS5  alive  recommended           mocha.my         God of War Ragnarök PS5 - … [MoCha]
   PS5  alive  recommended           1fichier.com     God of War Ragnarök PS5 - … [OneFile]
   PS5  alive  recommended           rootz.so         God of War Ragnarök PS5 - … [Rootz]
   PS5  blocked recommended  6.0KB   itsfileditchfiles.st  … [FileD]
   ```

   The page row itself is dropped once its links are in, so a SuperPSX hit is
   never "a website" - the widget's hosts (`mocha.my`, `itsfileditchfiles.st`)
   are in the file-host list alongside `vikingfile`, `rootz` and `1fichier`.

What cannot be reached is said plainly: `downloadgameps3.net` sits behind
Cloudflare and is blocked in the config, `filecrypt` containers need their
captcha, and a mirror that answers a `.part01.rar` with a 9 KB web page is
reported as `stub` rather than `alive`.

Page-style hosts (`1fichier`, `mega`, `gofile`) legitimately serve html at the
share url, so a status code is not enough and a body is read instead. Two
answers come out of that: the size hidden in the markup, and - when the body is
byte-for-byte the host's own front page - `shell`. A javascript-only host
renders the file list after load, so every share it has ever carried, live or
deleted years ago, returns the same 3 KB. `gofile.io/d/5JpwHw` and
`gofile.io/d/anything` are identical documents; the browser then calls
`api.gofile.io/contents/<id>` and shows *This content does not exist*. That
endpoint needs a `wt` token generated by `wt.obf.js`, which is a hash
computation rather than a constant, so the tool does not chase it: it says
`shell`, keeps the row out of the recommended sort and out of the downloader,
and drops the url from `good.tsv` so the cache cannot bless it again. A `403` on
the HEAD probe is retried as a ranged GET, because several hosts refuse HEAD
and serve bytes to GET.

## part sets

Split archives come back as twenty-one urls that are one download. [src/parts.cpp](src/parts.cpp)
detects the sequence, and every view - grid, table, csv, tsv, json - shows one
row per set:

| form | example |
| --- | --- |
| rar parts | `ELDEN RING NIGHTREIGN 01.000 PPSA25381 -www.P30DAY.ir.part01.rar` … `part21` |
| pkg parts | `HP0700-PPSA25379_00-ERSL000000000000_0.pkg` … `_6.pkg` |
| numeric splits | `game.001`, `game.002` |
| zip splits | `game.z01`, `game.zip` |

The set key is the folder plus the filename with the index removed, so parts
only merge with parts from the same folder and the same name - a lone `.part07`
stays a single row, and `EldenRing_4xx[PPSA25381].rar` next to the 21-part set
of the same game stays separate. The row carries the first part as its url, the
summed size of every member, `N parts` in the Parts column, and the best state
any member managed: `alive` if one part answers, `stub` if the survivors are
pages, `partial` if none do.

Right-click a set for **Open all parts**, **Download all parts (21)** and
*Copy all part URLs*; *Never show this set again* remembers every member, not
just the first. `tsv` and `csv` export add a `part_urls` column with the whole
sequence, so a set survives into the file. The limit counts rows, and trimming
never splits a set - either all of part01..21 is kept or none of it is.

### opening a set in the grid

A set row is a tree node. Click its **Parts** cell, press `Right` to open and
`Left` to close, or use *Show the part list* in the right-click menu, and the
members drop down underneath as indented sub-rows:

```
PS5  alive  recommended  37.9GB  10 parts  dlget.ir  …                     [set]
     alive  recommended   4.0GB   1 of 10  dlget.ir  └─ ASTRO.BOT-…part01.rar
     alive  recommended   4.0GB   2 of 10  dlget.ir  └─ ASTRO.BOT-…part02.rar
     alive  recommended   1.9GB  10 of 10  dlget.ir  └─ ASTRO.BOT-…part10.rar
```

Each child is a real row: its own state, size, http code, url and note, so you
can see which part of a ten-part dump is dead without opening the details pane.
Double-click one opens just that file; *Never show this link again* on a child
remembers only that part. The open/closed state is keyed to the set's first
part url, so a live re-sort during a hunt cannot move the expansion onto a
different game. Children appear in `Export` only while their set is open, and
the status bar counts them separately - `33 of 50 links … - parts shown - 10 files`.

Windows 10 ignores `LVITEM.iIndent` on a themed list view, so the branch is
drawn with a `└─` glyph in the title cell instead of a real indent.

## the downloader

[src/download.cpp](src/download.cpp) is a small queue that pulls the direct
links instead of opening them in a browser. **Download** on the toolbar takes
the selected row; the right-click menu says exactly what it will grab:

| row | menu entry | what it queues |
| --- | --- | --- |
| single file | *Download this file* | that url |
| part set | *Download all parts (21)* | every member that is not dead |
| page, container, `gated` or `shell` | nothing offered | the row is not a file |

**The folder is asked for first.** Before a byte moves, the shell's folder
picker opens (`IFileOpenDialog` with `FOS_PICKFOLDERS`, and `SHBrowseForFolder`
underneath it if the shell object is unavailable) starting at the last place
you used. The choice is written to `%APPDATA%\PKG-LinkScrapper\downloads.txt`, so the
next pull defaults to it, and **Open folder** in the transfers window takes you
there. Cancel the picker and nothing downloads - the status bar says so.

Transfers run three at a time on their own WinHTTP session, so a hunt in
progress is never starved by a download and a download is never cut off by
**Stop**. Each file lands as `<name>.part` and is renamed when complete:

- a restart resumes with `Range: bytes=<size>-`, so a 40 GB pull continues
- a host that answers `200` instead of `206` gets its partial deleted and
  restarted - appending a whole body to a half file is corruption
- a `416` deletes the partial and starts from zero
- redirects are followed by hand, up to six, so the `Range` header survives
- a name already in the folder becomes `thing (2).pkg` rather than being
  overwritten
- the row is only `done` when the byte count matches what the server claimed

The transfers window is modeless and stays open while you keep hunting. Its
head line is the whole picture -

```
16 file(s)  -  16 active  -  77.1MB of 12.0GB  -  7.3 MB/s  -  saving to D:\Games\PS5
```

and every row carries its own MB/s.
The live number is a 250 ms sample smoothed over the last few; once a file
lands the column keeps the average for the whole transfer, so a two second
pull still shows what it moved. **Pause** holds every worker where it is and
keeps the partials, **Cancel** stops the selected rows (or everything, if none
are selected), **Clear done** drops finished rows, and double-clicking a
completed row opens the file.

## recommended links

Two things decide whether a row is worth your time, and the tool keeps both:

1. **A curated list in the config.** Hosts and sources with a standing record of
   serving bytes. The shipped defaults come out of a measured sweep - five
   titles, 32 rows, alive rate per host:

   | host | alive in the sweep |
   | --- | --- |
   | `1fichier.com`, `mediafire.com`, `gofile.io`, `superpsx.com` | 100% |
   | `dlget.ir` | 71% |
   | `thesuperboffers.com`, `vikingfile.com` | 50% |
   | `filecrypt.cc`, `buzzheavier.com` | 0% - not recommended |

   Sources carry `"trusted": true` the same way; `superpsx`, `archive-org` and
   `archive-collections` ship with it.
2. **What this machine has personally seen work.** Every link that answers with
   bytes is upserted into `%APPDATA%\PKG-LinkScrapper\good.tsv` with its size and the
   time it was seen. On the next run a url in that file, inside the freshness
   window, is reported `alive` with its remembered size and is not probed again.
   Entries nobody has seen work in three windows are dropped.

A row carrying `recommended` sorts above one that doesn't, inside each game.
The label is taken away when a probe says `stub`, `dead` or `unreachable` - a
good host can still host a dead file - and `blocked` and `page` rows keep it.

| control | what it does |
| --- | --- |
| `recommended only` checkbox | hides everything without the label |
| `--trusted-only` / `--recommended` | the same for the console build and the launch arguments |
| `--refresh` | ignore the cache and probe every url, including known-good ones |
| `--resolve <url>` | walk a share page to the link behind it (`1fichier` today) and print what the mirror says. Spends the one-time link, so it is a check, not a way to grab a file |
| `--forget-good` | delete `good.tsv` and start the learning over |
| `rec` at the interactive prompt | toggle recommended-only between runs |

```json
{ "defaults": { "good_ttl_days": 7, "trusted_hosts": ["dlget.ir", "1fichier.com"] } }
```

Drop that in `%APPDATA%\PKG-LinkScrapper\sources.json` to add your own; the built-in list
stays unless a config file is present, in which case that file owns the whole
config. Press **Sources** to re-read the lists without restarting.

### the look

The window is painted, not themed-by-default. What that means in practice:

| surface | technique |
| --- | --- |
| whole app | `UxThemeDll_SetPreferredAppMode` (uxtheme ordinal 135) with `ForceDark` - the call Explorer uses, which reaches common controls `SetWindowTheme` cannot |
| titlebar | `DWMWA_USE_IMMERSIVE_DARK_MODE`, plus `DWMWA_WINDOW_CORNER_PREFERENCE = ROUND` on Windows 11 |
| command buttons | `BS_OWNERDRAW`, rounded 11x9 rect, hover and pressed states from the palette, Hunt filled with the system accent (`DwmGetColorizationColor`) |
| checkboxes | `BS_OWNERDRAW`, flat box with a two-stroke accent tick |
| console combo | `CBS_OWNERDRAWFIXED`, flat field with the accent on the picked row |
| column header | the header control is **subclassed** and painted in its own `WM_PAINT` / `WM_PRINTCLIENT`: muted labels, hairline dividers, accent underline and a drawn triangle on the sorted column |
| rows | `NM_CUSTOMDRAW` returning `CDRF_SKIPDEFAULT` - the whole row is drawn: alternating tint, accent selection instead of the OS blue, a status dot in the state colour, hairline under each row |
| row height | a 1x26 empty image list on `LVSIL_SMALL`, the only way to make report rows taller than the font |
| fields | no border style at all - `WS_EX_STATICEDGE` and `WS_EX_CLIENTEDGE` are both drawn light grey by the shell on Windows 10, so the frame is painted in `WM_NCPAINT` from a subclass in the palette's border colour, and the edits get a 5 px `EM_SETMARGINS` instead of the groove they lost |
| focus | drawn as a 1 px palette-coloured rectangle (rounded on buttons) - `DrawFocusRect` is black dots on an xor white and reads as a stray outline on a dark row or a dark button |
| spacing | 14 px margins, 10 px gutters, 28 px controls, 26 px rows |

Cell text is laid out by summing `LVM_GETCOLUMNWIDTH` rather than asking for
subitem rects - `LVM_GETSUBITEMRECT` returns the whole row for subitem 0 and
drifted everything else under double buffering.

The header is subclassed because its `NM_CUSTOMDRAW` goes to its parent, which
is the list view - not the window - so a `WM_NOTIFY` handler for it never fires
and the strip stayed white in the middle of an otherwise dark grid. The same
`paintHeaderCells` serves both windows; the sorted column's accent underline is
drawn only when the header belongs to the results grid.

Row colour is a dot, not coloured text, so a green `alive` stays readable on
the alternating background and on the selection.

The check state lives in the app (`checkVerify` / `checkRecommended` /
`checkDark`) - an owner-draw button stores nothing, and reading `BM_GETCHECK`
off one silently reported everything unchecked, which turned `verify links`
off without showing it.

Two things stay system-coloured on Windows 10 and are not fixable per window:
the scrollbars and the right-click menu. Windows 11 takes both from the same
theme call.

## details pane

The box under the grid is the full record of the selected row, in monospace:

```
elden ring nightreign  PS5  alive  (recommended)
size 25.1GB (26948009984 bytes)  http 200  kind direct
source p30day  host dlget.ir  match 0.95  parts 7  first part 0
https://dl2.dlget.ir/users/alireza/ps5/Elden%20Ring%20NIGHTREIGN/PPSA23083%20v01.003.004/..._0.pkg
all parts:
  https://dl2.dlget.ir/.../_0.pkg
  https://dl2.dlget.ir/.../_1.pkg
```

Exact bytes, the http code, the match score, every member url of a part-set and
whatever the probe said in `note`. Right-click gives **Copy details** for the
whole block.

## never leave a row unverified

`unverified` means the probe has not run for that url yet - no state, no size.
Four things keep that from being the final answer:

1. each title is probed as soon as its crawl lands, so rows fill in during the run;
2. when a run finishes and anything is still unverified, the tool re-probes
   those rows on its own - the status bar says `re-checking N leftover links`.
   The console build does the same pass before it prints;
3. **Re-check** does it by hand, at any time, without crawling anything again;
4. whatever the http probe cannot settle gets a **ping**.

### ping

A TCP connect to the host - `443`, falling back to `80` - timed in
[src/ping.cpp](src/ping.cpp). ICMP is not used:
most file hosts drop echo requests, so a silent ping would call living mirrors
dead. Results are cached per host for the run, so a 21-part set costs one probe.

| grid | meaning |
| --- | --- |
| `14ms` | the host answered the handshake that fast |
| `no route` | nothing answered on 443 or 80 within 1.5 s |
| blank | the http probe already settled the row, no ping needed |

The ping only runs when the http answer was inconclusive - `unverified`,
`skipped`, `unreachable`, `stub`, or an `alive` with no size. It then splits the
two failures that used to look the same:

```
PS5  dead                                     vikingfile.com   file gone, host still up
PS5  unreachable  no route                    some-mirror.net  host itself is down
PS5  alive       recommended  3.3KB     14ms  1fichier.com     host up, page-style share
```

A host that answers while the file does not stays `dead`/`stub` with the reason
in `note`, and the details pane carries the ping line:
`ping 14ms (answered from 104.26.14.7)`.

Sizes that the ranged probe cannot see are read out of the share page itself:
the head of the body is fetched and a `Size: 12.3 GB` marker is parsed from it.
The same read looks for `file not found`, `no longer available`, `link has
expired` and friends, and calls those `dead` with the reason in `note` instead
of leaving them optimistically `alive`.

## performance

- **probe memo** - the same url reached through two different titles is probed
  once per run, keyed by url and kind, valid for ten minutes.
- **response cache** - page bodies are cached per run (24 documents, 3 MB cap),
  so a listing crawled twice costs one fetch.
- **known-good cache** - urls in `good.tsv` inside their window are reported
  alive without any request at all.
- **incremental repaint** - the grid updates the cells that changed instead of
  deleting and retyping every row every 300 ms; the item count is read from the
  control itself so a desync can only ever force a clean rebuild.
- **trimming whole sets** - `trimByGroup` keeps or drops a part-set intact, so
  the limit is rows, not files.
- Titles still run three at a time. A two-title ps5 hunt finishes in 25-35 s,
  a six-title hunt in about 90 s.

## matching

`scoreTitle` in [src/match.cpp](src/match.cpp):

- exact title → `1.0`
- query is the leading words of the candidate → `0.95`
- one leading word in front → `0.9` (`Marvels Spider Man 2` for `spider-man 2`)
- two or more leading words → similarity only, so `The Amazing Spider Man 2`
  does not match `spider-man 2`
- token-coverage and bigram fallbacks sit below the default threshold

Platform detection prefers the page slug over the `<title>`, because dlpsgame
puts every console in the title. `PPSA…` is PS5, `CUSA…` is PS4; a title naming
both consoles is ambiguous; pages whose slug says PS2/PS3/PSP/Xbox/Switch are
dropped.

## build

Windows exes are cross-built on the Linux box with zig (clang + bundled
mingw headers and CRT, no root needed):

```
./build.sh    # selftest, then the one-file build/PKG-LinkScrapper.exe (plus an optional PKG-LinkScrapper-cli.exe)
```

`build.sh` uses `$ZIG` or `~/tools/zig-x86_64-linux-0.17.1/zig`. It compiles
`test/selftest.cpp` against the platform-neutral modules and runs it natively
first (138 checks: url handling, json, html extraction, title scoring, platform
detection, bing/ddg link unwrapping, host classification, part detection and
grouping, trusted-host matching), then builds the PE. `src/ping.cpp` is
winsock-only, so it sits in `CORE` for the Windows targets and is left out of
the native selftest:

```
zig rc res/app.rc build/app.res

zig c++ -std=c++20 -O2 -target x86_64-windows-gnu -static \
    src/util.cpp src/json.cpp src/hosts.cpp src/html.cpp src/match.cpp src/config.cpp \
    src/http.cpp src/crawl.cpp src/verify.cpp src/output.cpp src/gui.cpp build/app.res \
    -Wl,--subsystem,windows \
    -lwinhttp -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -luser32 \
    -o build/PKG-LinkScrapper.exe
```

The `.res` holds the application manifest and is passed to the link as an object
file, so common controls v6 (themed list view and buttons) and per-monitor DPI
travel inside the exe - no sidecar `.manifest`. `-Wl,--subsystem,windows` is what
makes it windowed; plain `-mwindows` is ignored by zig's driver and would leave a
console flashing behind the app.

`PKG-LinkScrapper-cli.exe` is the same engine with a console front end, for pipes and
scheduled jobs. It is not part of the single-file deliverable and lives only in
the build directory.

`tools/` holds the verification scripts: `crop-window.ps1` screenshots the
window at native size, `drive-ui.ps1` fills the games box and clicks Hunt
through window messages, `type-keys.ps1` sends real keystrokes.

Source tree: `util` (strings, urls, thread pool, log sink, abort flag),
`json` (parser), `http`
(WinHTTP client with per-host throttle, retries, body cache), `html` (anchor and
bare-url extraction), `match` (scoring, platform rules), `config` (layered
source loading), `crawl` (engines, listing crawl, redirect resolution),
`verify` (liveness), `output` (table/json/csv/tsv), `main` (CLI).

## known edges

- DuckDuckGo answers `202` after a burst; the client retries once and Bing
  (HTML, then RSS fallback) carries the run.
- filecrypt and similar containers gate their links behind a captcha; they are
  listed as `container`, not cracked.
- mega.nz links probe as pages, not files — `alive` means the page exists.
- google drive links from old comments are usually dead; that is reported.
- dlpsgame hides some host URLs behind a JS decoder, so the release page itself
  is kept when no host link is exposed.
- some hosts answer a ranged probe with a small stub page instead of the file.
  A size under a megabyte next to `alive` means the bytes need a browser or a
  cookie, not that the dump is that small.
- catalog JSON is parsed for `url` / `link` / `download` / `href` keys, with
  `name` / `title` / `label` used as the entry name, so any ps5-library style
  catalog can be added as an `index` source.
- responses are capped at 3 MB per page; the page cache holds 24 documents.
- Stop drops every queued job and refuses every new request, so a one or two
  title hunt gives the button back immediately. With six titles crawling there
  are tens of requests already inside WinHTTP, and a synchronous call that has
  not returned yet unwinds on its own - measured 7-11 s in that case, bounded by
  one 8 s timeout. The `WINHTTP_OPTION_CANCEL` calls and the abort check in the
  read loop do not shorten it.

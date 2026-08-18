# ehentai_crawler

A command line downloader for e-hentai / exhentai galleries, written in C++20.
It talks to the site with **libcurl** and reads the markup with
**[gumbo-parser](https://github.com/google/gumbo-parser)**, Google's HTML5
parser, so the scraping follows the real parse tree instead of regexes over raw
markup.

## What it does

1. Takes a gallery url (`https://e-hentai.org/g/<id>/<token>/`).
2. Walks every listing page of the thumbnail grid and collects the viewer page
   of each image, along with the file name the uploader used.
3. Writes `metadata.json` with the titles, tags, rating, the `#gdd` detail rows
   and the full page list.
4. Fetches every viewer page, pulls the image url out of it and downloads the
   image, several pages at a time.

It also handles the things the site does in practice:

* **Dead Hath nodes.** When an image fails to download, the viewer page is
  reloaded with the `nl` token so the site hands out a different host.
* **HTML pretending to be an image.** The response content type is checked, so a
  login or throttling page never lands on disk wearing a `.png` suffix.
* **Quota and bans.** A 509 answer or one of the site's interstitial pages stops
  the crawl instead of burning through retries.
* **Resume.** Images already on disk are skipped unless `--overwrite` is given,
  and downloads land on a `.part` file that is renamed only once complete.

## Building

The only dependencies are libcurl and gumbo.

### Visual Studio

`vcpkg.json` declares both dependencies, so with
[vcpkg](https://vcpkg.io) integrated (`vcpkg integrate install`) opening
`ehentai_crawler.slnx` and building is enough — the packages are restored and
linked automatically.

### CMake (Linux, macOS, or Windows)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

On Debian/Ubuntu the dependencies are `libcurl4-openssl-dev` and `libgumbo-dev`;
on macOS, `brew install curl gumbo-parser`.

## Usage

```
ehentai_crawler <gallery-url> [options]

  -o, --output <dir>    where to put the files (default: downloads)
  -c, --cookies <str>   cookie header, needed for exhentai and member settings
  -j, --jobs <n>        parallel workers (default: 3)
  -d, --delay <ms>      minimum delay between requests (default: 250)
  -r, --retries <n>     retries per request (default: 3)
      --from <n>        first page to fetch, 1 based (default: 1)
      --to <n>          last page to fetch (default: the last one)
      --original        grab the original upload instead of the resample
      --metadata-only   write metadata.json and stop
      --overwrite       refetch images that are already on disk
      --flat            write into the output dir without a per gallery folder
      --proxy <url>     route every request through this proxy
      --user-agent <s>  override the user agent
  -q, --quiet           only print errors
  -h, --help            show this help
```

Images are saved as `<zero padded page>_<uploader file name>.<ext>` inside a
folder named after the gallery, next to `metadata.json`.

Exit code `0` means everything asked for is on disk, `1` that something failed,
and `2` that the site cut the crawl short (quota, ban, or a removed gallery).

### Examples

```sh
# whole gallery
ehentai_crawler https://e-hentai.org/g/3264912/be4872163f/

# just the metadata
ehentai_crawler https://e-hentai.org/g/3264912/be4872163f/ --metadata-only

# a page range into a specific folder, gently
ehentai_crawler https://e-hentai.org/g/3264912/be4872163f/ \
    -o ~/galleries --from 1 --to 40 -j 2 -d 500

# exhentai, which needs a logged in session
ehentai_crawler https://exhentai.org/g/3264912/be4872163f/ \
    -c "ipb_member_id=...; ipb_pass_hash=...; igneous=..."
```

`--original` only works with a logged in account; without one the site answers
with a login page and the crawler falls back to the resampled image.

## Notes on being a good citizen

The site enforces per account image quotas and bans IPs that hammer it. The
defaults (3 workers, 250 ms between requests) are deliberately unhurried — raise
`--delay` and lower `--jobs` rather than the other way round.

## Layout

| file | what's in it |
| --- | --- |
| `entry.cpp` | argument parsing and `main` |
| `crawler.{h,cpp}` | the crawl itself: listing walk, worker pool, file naming |
| `ehentai.{h,cpp}` | the gallery/viewer page parsers and the data model |
| `html.{h,cpp}` | gumbo helpers: by id, by tag, by class, text extraction |
| `http.{h,cpp}` | libcurl wrapper with retries and a shared rate limiter |
| `macros.h` | user agent and the default settings |

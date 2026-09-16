# Vendored giflib

This directory is a **vendored, modified** copy of [giflib](https://giflib.sourceforge.net/).
It is not pristine upstream — the changes under "Local modifications" are applied
on top of it so the library builds inside SCI Companion. Preserve them when you
update giflib.

## Version

giflib **5.2.2**, taken from the giflib git repository at tag `5.2.2`
(`https://git.code.sf.net/p/giflib/code`).

The version is recorded in `gif_lib.h`: `GIFLIB_MAJOR 5`, `GIFLIB_MINOR 2`,
`GIFLIB_RELEASE 2`.

## Vendored files

Library sources only. The giflib command-line utilities, `quantize.c`, and the
upstream build system are intentionally not vendored.

| Vendored file | Upstream file | Purpose |
| --- | --- | --- |
| `dgif_lib.cpp` | `dgif_lib.c` | GIF decoding |
| `egif_lib.cpp` | `egif_lib.c` | GIF encoding |
| `gifalloc.cpp` | `gifalloc.c` | colour-map / saved-image / extension allocation |
| `gif_err.cpp` | `gif_err.c` | error strings |
| `gif_hash.cpp` | `gif_hash.c` | encoder hash table |
| `openbsd-reallocarray.cpp` | `openbsd-reallocarray.c` | overflow-checked `reallocarray` (added upstream in 5.2.0) |
| `gif_lib.h` | `gif_lib.h` | public header |
| `gif_lib_private.h` | `gif_lib_private.h` | private header |
| `gif_hash.h` | `gif_hash.h` | encoder hash header |

`COPYING` is the upstream MIT licence. `AUTHORS` is retained from 5.1.1 (5.2.2
ships no `AUTHORS` file).

## Local modifications

1. **`.c` files renamed to `.cpp`** and built as C++ (the project has no C
   compilation). This is what requires the C++ casts in item 5.
2. **`#include "stdafx.h"` as the first include** of every `.cpp`. These files
   are compiled with a *used* precompiled header (`stdafx.h`), so it must come
   first.
3. **Windows file open.** `dgif_lib.cpp` and `egif_lib.cpp` open files with the
   secure CRT (`_sopen_s`) and a share mode, and use `_close` / `_fdopen`,
   instead of POSIX `open` / `close` / `fdopen`. Each adds `#include <share.h>`
   (for `_SH_DENYWR` / `_SH_DENYRW`) under `#ifdef _WIN32`.
   - `DGifOpenFileName`: `_O_RDONLY`, `_SH_DENYWR`.
   - `EGifOpenFileName`: `_O_WRONLY | _O_CREAT | _O_EXCL` (or `_O_TRUNC`),
     `_SH_DENYRW`, `_S_IREAD | _S_IWRITE`.
4. **`#undef LOBYTE` / `#undef HIBYTE`** in `egif_lib.cpp`, before its own
   `#define`s of those macros — `windows.h` (via `stdafx.h`) already defines
   them, which would otherwise be a redefinition.
5. **C++ cast** for the `void *` extension data in the two `InternalWrite`
   calls in `egif_lib.cpp` (`static_cast<const unsigned char *>(Extension)`).
6. **Memory-leak fix in `EGifPutScreenDesc`** (`egif_lib.cpp`). `EGifSpew` calls
   `EGifPutScreenDesc` with `GifFile->SColorMap` as the colour-map argument, so
   rebuilding the map there leaks the caller's. The function records the incoming
   map and reuses it when the same pointer is passed back, instead of rebuilding.
   (Originally SCI Companion commit `23aafb28`; adapted for 5.2.2, which now sets
   `SColorMap` to null at the top of the function.)

Upstream 5.2.2 already guards `<unistd.h>` behind `#ifdef _WIN32` and sets binary
mode with `_setmode`, so those earlier local ports are no longer needed.

## How the application uses giflib

`SCICompanionLib/Src/Util/ImageUtil.cpp` uses a small, stable slice of the public
API to import and export animation cels as GIFs: `DGifOpenFileName`, `DGifSlurp`,
`DGifSavedExtensionToGCB`, `DGifCloseFile`, `EGifOpenFileName`, `GifMakeMapObject`,
`GifAddExtensionBlock`, `EGifGCBToSavedExtension`, `EGifSpew`, `EGifCloseFile`,
`GifFreeExtensions`, plus fields of `GifFileType`, `SavedImage`, `ColorMapObject`,
and `GraphicsControlBlock`. This surface did not change between 5.1.1 and 5.2.2.

Tests are in `UnitTests/TestUtilHardening.cpp`.

## Updating giflib

1. `git clone --branch <tag> https://git.code.sf.net/p/giflib/code` and copy the
   library sources over the files above, renaming `.c` to `.cpp`.
2. Re-apply every local modification listed above. Diff the current vendored
   files against the matching pristine upstream tag first, so no local change is
   lost.
3. If upstream adds or removes a library source, update
   `SCICompanionLib.vcxproj` and `SCICompanionLib.vcxproj.filters` to match
   (for example `openbsd-reallocarray.cpp`, added in 5.2.0).
4. Build `SCICompanion.sln` (Release|Win32) and run the GIF tests.

# Third-Party Notices

This repository includes third-party components under `external/`.
Those files remain separate from project-owned source code.
The build copies the runtime DLLs and this file next to `BaseballSimulatorVR.exe`.

## OpenXR loader and headers

- Component: OpenXR loader import library/runtime DLL and generated OpenXR headers
- Version: 1.1.57 (`XR_CURRENT_API_VERSION` in `openxr.h`; the DLL's version resource reports
  "OpenXR Loader" 1.1.57.0)
- Upstream: Khronos OpenXR SDK, <https://github.com/KhronosGroup/OpenXR-SDK>, tag
  [`release-1.1.57`](https://github.com/KhronosGroup/OpenXR-SDK/releases/tag/release-1.1.57)
- Included paths:
  - `external/openxr/include/openxr/*` (identical to `include/openxr/` at the upstream tag)
  - `external/openxr/lib/x64/openxr_loader.lib`
  - `external/openxr/bin/x64/openxr_loader.dll`
- Copyright: Copyright 2017-2026 The Khronos Group Inc. The loader sources also carry
  Copyright 2017-2019 Valve Corporation and Copyright 2017-2019 LunarG, Inc.
- License: `Apache-2.0 OR MIT`
- Source notice reference: `external/openxr/include/openxr/openxr.h`
- Contains: JsonCpp, statically linked into `openxr_loader.dll` (see below)

## JsonCpp (inside `openxr_loader.dll`)

- Component: JsonCpp, which the OpenXR loader uses to read runtime and API layer manifest files.
  It is compiled into `openxr_loader.dll` (the binary contains the `Json::Value` and
  `Json::Reader` code); there is no separate JsonCpp file in this repository.
- Upstream: <https://github.com/open-source-parsers/jsoncpp>, as vendored in the OpenXR SDK under
  `src/external/jsoncpp` at tag `release-1.1.57`
- License: MIT, or Public Domain where that is recognized. Full text from
  `src/external/jsoncpp/LICENSE` of the OpenXR SDK:

```text
The JsonCpp library's source code, including accompanying documentation,
tests and demonstration applications, are licensed under the following
conditions...

Baptiste Lepilleur and The JsonCpp Authors explicitly disclaim copyright in all
jurisdictions which recognize such a disclaimer. In such jurisdictions,
this software is released into the Public Domain.

In jurisdictions which do not recognize Public Domain property (e.g. Germany as of
2010), this software is Copyright (c) 2007-2010 by Baptiste Lepilleur and
The JsonCpp Authors, and is released under the terms of the MIT License (see below).

In jurisdictions which recognize Public Domain property, the user of this
software may choose to accept it either as 1) Public Domain, 2) under the
conditions of the MIT License (see below), or 3) under the terms of dual
Public Domain/MIT License conditions described here, as they choose.

The MIT License is about as close to Public Domain as a license can get, and is
described in clear, concise terms at:

   http://en.wikipedia.org/wiki/MIT_License

The full text of the MIT License follows:

========================================================================
Copyright (c) 2007-2010 Baptiste Lepilleur and The JsonCpp Authors

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy,
modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
========================================================================
(END LICENSE TEXT)

The MIT license is compatible with both the GPL and commercial
software, affording one all of the rights of Public Domain with the
minor nuisance of being required to keep the above copyright notice
and license text in the source code. Note also that by accepting the
Public Domain "license" you can re-license your copy using whatever
license you like.
```

## SQLite

- Component: SQLite headers, import library, and runtime DLL
- Version: 3.51.3 (`SQLITE_VERSION_NUMBER` 3051003, `SQLITE_SOURCE_ID`
  `2026-03-13 10:38:09 737ae4a34738ffa0c3ff7f9bb18df914dd1cad163f28fd6b6e114a344fe6d618`)
- Upstream: <https://www.sqlite.org/>
  - `sqlite3.dll` is byte-identical to the DLL in
    <https://www.sqlite.org/2026/sqlite-dll-win-x64-3510300.zip>
  - `sqlite3.h` and `sqlite3ext.h` are identical to those in
    <https://www.sqlite.org/2026/sqlite-amalgamation-3510300.zip>
  - `sqlite3.lib` is the x64 import library for that DLL (built from the `sqlite3.def` in the
    same archive)
- Included paths:
  - `external/sqlite/include/sqlite3.h`
  - `external/sqlite/include/sqlite3ext.h`
  - `external/sqlite/lib/x64/sqlite3.lib`
  - `external/sqlite/bin/x64/sqlite3.dll`
- Source notice reference: `external/sqlite/include/sqlite3.h`
- Upstream notice summary: SQLite is distributed in the public domain. The
  vendored header includes the upstream blessing text and public-domain notice.

## Checksums of the vendored binaries

SHA-256 of the files as committed. Verify with `certutil -hashfile <file> SHA256` on Windows
or `sha256sum <file>` elsewhere.

| File | SHA-256 |
| --- | --- |
| `external/openxr/bin/x64/openxr_loader.dll` | `274053147b9ab1055bbf48cc0fc6654f2a089b499eb0d882d2567e1e77205b32` |
| `external/openxr/lib/x64/openxr_loader.lib` | `34f516fa51c3b32f5fa2a0900ca2237bd2e22fcb89c614aa4d6df8f1ea1fa4a9` |
| `external/sqlite/bin/x64/sqlite3.dll` | `0f5f59edc42c2138249d0386b6417125e4e4dd31706c4f4636441de45cb7dfa1` |
| `external/sqlite/lib/x64/sqlite3.lib` | `4a3cf64e054747b0e5af8cf3944551e0735ed9e77c74eb4e6ca0a1f8b7fd8f75` |

Update this table, and the versions above, whenever a vendored binary is replaced.

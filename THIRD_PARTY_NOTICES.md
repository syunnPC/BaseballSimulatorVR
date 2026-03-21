# Third-Party Notices

This repository includes third-party components under `external/`.
Those files remain separate from project-owned source code.

## OpenXR loader and headers

- Component: OpenXR loader import library/runtime DLL and generated OpenXR headers
- Version: 1.1.57
- Included paths:
  - `external/openxr/include/openxr/*`
  - `external/openxr/lib/x64/openxr_loader.lib`
  - `external/openxr/bin/x64/openxr_loader.dll`
- Copyright: Copyright 2017-2026 The Khronos Group Inc.
- License: `Apache-2.0 OR MIT`
- Source notice reference: `external/openxr/include/openxr/openxr.h`

## SQLite

- Component: SQLite headers, import library, and runtime DLL
- Version: 3.51.3
- Included paths:
  - `external/sqlite/include/sqlite3.h`
  - `external/sqlite/include/sqlite3ext.h`
  - `external/sqlite/lib/x64/sqlite3.lib`
  - `external/sqlite/bin/x64/sqlite3.dll`
- Source notice reference: `external/sqlite/include/sqlite3.h`
- Upstream notice summary: SQLite is distributed in the public domain. The
  vendored header includes the upstream blessing text and public-domain notice.

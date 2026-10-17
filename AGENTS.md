# C++ style

Use the repository `.clang-format` for every C/C++ edit. Format changed
first-party files before finishing; avoid unrelated formatting changes.
Do not format vendored dependencies or generated build output.

- Types and functions: PascalCase.
- Variables, parameters, and public fields: lowerCamelCase.
- Private data members: lowerCamelCase with a trailing underscore.
- Files: snake_case.
- Retain `#pragma once` and exceptions where appropriate.
- Keep formatting settings synchronized between Iryven and Velos.

Use clang-format 19.1.7 for reproducible CI results. Run
`python tools/format_cpp.py` to check, or append `--fix` to format all
first-party C/C++ files. To format only an edited file, run
`clang-format -i --style=file path/to/file.cpp`.
The formatter enforces layout, not identifier naming or API design.

# C++ coding conventions

Application code uses C++17. These conventions adapt JUCE's coding standards
and the C++ Core Guidelines to this project. Formatting is defined by
[.clang-format](.clang-format), with editor defaults in the root `.editorconfig`.

## Layout

- Use four spaces for indentation and spaces instead of tabs.
- Put opening braces on a separate line (Allman style).
- Align access labels with the class braces; indent members by four spaces.
- Use braces around control-flow bodies, including single statements.
- Keep one variable declaration per statement.
- Separate function definitions and logical groups with blank lines.
- Use `Type* pointer` and `Type& reference` consistently.
- Prefer lines within 100 columns. Let the formatter align wrapped arguments.
- Use PascalCase for types and camelCase for functions and variables.
- Keep public declarations readable and name parameters in headers.
- Place substantial implementations in `.cpp` files; keep headers self-contained.
- Include the matching header first, then project, JUCE and standard headers.

These are project choices: the referenced guides differ on some surface syntax.
The formatter provides a single consistent result for our application sources.
Third-party code and the original kernel retain their own conventions.

## Ownership, errors and concurrency

Use scoped resource owners and `std::unique_ptr` for exclusive ownership.
Explicitly delete copy operations for resource owners that cannot be copied.
Raw pointers and references normally borrow objects; follow documented JUCE
ownership-transfer APIs when constructing component hierarchies.

Report initialization and diagnostic I/O failures with context. Destructors
must not throw. When closing a file can fail, provide an explicit operation
that reports that failure, with destructor cleanup as the fallback. An
exclusive file must stay open for the full write operation.

GUI components belong to the message thread. Workers publish status snapshots;
UI timers apply those snapshots to widgets. Network commands have timeouts and
must be checked for unsuccessful completion.

Prepare audio resources outside processing callbacks. Processing must avoid
allocation, blocking locks, file/network I/O and GUI calls. Check buffer
contracts and narrowing conversions at preparation boundaries. Audio parameter
atomics must be lock-free on the target. The current audio foundation still
requires a hardware adapter and timing measurements.

## Formatting and validation

Install clang-format 18 or newer, then run from the repository root:

```sh
python3 scripts/format_cpp.py
python3 scripts/format_cpp.py --check
```

Set `CLANG_FORMAT` to select an executable outside `PATH`. The script operates
on our C++ source directories, excluding generated and third-party code.
Run the CMake/CTest checks described in [README](README.md) after code changes.
Regression tests should cover observable contracts such as capture compatibility,
exclusive file creation and audio buffer handling.

## References

- [JUCE Coding Standards](https://juce.com/blog/coding-standards/): formatting,
  class layout, naming and framework conventions.
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines):
  particularly R.1 (RAII), E.16 (non-throwing cleanup), SF.11 (self-contained
  headers), ES.46 (narrowing conversions) and NL.4 (consistent indentation).
- [LLVM Coding Standards](https://llvm.org/docs/CodingStandards.html): readability,
  include organization and control flow.
- [Clang-format options](https://clang.llvm.org/docs/ClangFormatStyleOptions.html):
  the machine-enforced formatting configuration.

# Changelog

## Unreleased

### Fixed
- Serializer emitted tab, LF, and CR literally in attribute values and CR literally in text, so a conforming reader rewrote them on the way back in and `serialize()` -> `deserialize()` silently lost the bytes; they are now written as `&#9;`/`&#10;`/`&#13;`
- Element, attribute, and variant dispatch bound a document name to a field on an FNV-1a hash match alone. FNV-1a is invertible, so a colliding name is constructible rather than improbable; the declared name is now confirmed before the value is bound
- `StrictParser` duplicate-attribute detection degraded to O(n^2) once a start-tag exceeded the 64-bit filter's capacity, and the `'<'`-in-attribute-value check scanned to end of input per attribute rather than to the end of the value. A 40,000-attribute tag went from 393 ms to 2.5 ms
- `skipElement` counted nesting from zero instead of from the parser's current depth, so an unknown subtree could descend `MAX_DEPTH` again

### Added
- Non-UTF-8 input is rejected with `ErrorCode::UnsupportedEncoding` (UTF-16/32 BOM, BOM-less UTF-16, or an XML declaration naming a non-UTF-8 encoding) instead of being scanned as bytes and reported as a missing root
- libFuzzer target (`LIGHTNINGXML_BUILD_FUZZERS`) driving all three parser modes plus a serialize/re-parse differential, with a committed seed corpus and a bounded CI run
- CI matrix building and testing gcc/clang across Debug and Release, so the shipped `-O3` + LTO + unity configuration is exercised

### Changed
- clang-tidy CI job disables unity builds; with them on, `compile_commands.json` held only the generated unity sources and clang-tidy fell back to a default command that could not resolve the include path
- Reference expansion bounds its terminator search, so a stray `&` no longer costs a scan of the remaining document

## 1.1.1 - 2026-07-07

### Changed
- Public entry points (`deserialize`, `serialize`, `Serializer::write`, `valueField`, `Parser::pull`) constrained with C++20 concepts
- Compile-time field-kind queries consolidated into variable templates (matching `FIELD_COUNT`/`FIELD_SEQ`); `optionalsNotRequired` reuses `anyFieldSatisfies`.
- Serializer escape scanning

### Fixed
- date/dateTime year with 19+ digits overflowed a signed accumulator (undefined behavior)

### Added
- `StrictParser` rejects control bytes outside the XML `Char` production in character data, CDATA, attribute values, comments, and PIs (`ErrorCode::ForbiddenControlChar`)
- gcov coverage CI with 80% line floor
- Tests for parser guard paths, serializer escaping, multi-byte character references, and date/time

## 1.1.0 - 2026-07-02

### Changed
- `xmlight::validate()` recurses through it's own, nested objects, containers, optionals, unique_ptrs, and variants `XmlConstraints`.
- `NormalizingParser` expands references in `xs:list` values before splitting (`string_view` items stay raw).
- xsdgen resolves `xs:include` transitively.
- Consolidated parser and codegen internals.
- Attribute parsing improved
- Reduce allocations when parsing into `std::vector`

### Fixed
- xsdgen emitted uncompilable C++ for inline types in `xs:group` definitions, facets on `xs:choice` branches, multiple choices in one type, and enum-typed `default`/`fixed` values.
- Inline `<xs:simpleType>` on attributes (enums, facets) was silently dropped to `std::string`.
- `xmlight::validate()` did not compile when instantiated in certain cases.

### Added
- Install rules and CMake package config: `find_package(LightningXML)` with `LightningXML::lightningxml`.
- Build-time test that compiles and round-trips xsdgen output from a kitchen-sink schema.
- Unity and LTO build options
- Minimal LightningXML serializer benchmarking

### Removed
- Leftover TurboXML naming and duplicated CMake/README sections.

## 1.0.0 - 2026-07-01 

Initial Release
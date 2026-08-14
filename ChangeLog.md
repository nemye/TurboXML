# Changelog

## Unreleased

### Fixed
- Serializer emitted tab, LF, and CR literally in attribute values and CR literally in text, so a conforming reader rewrote them on the way back in and `serialize()` -> `deserialize()` silently lost the bytes; they are now written as `&#9;`/`&#10;`/`&#13;`
- Element, attribute, and variant dispatch bound a document name to a field on an FNV-1a hash match alone. FNV-1a is invertible, so a colliding name is constructible rather than improbable; the declared name is now confirmed before the value is bound
- `StrictParser` duplicate-attribute detection degraded to O(n^2) once a start-tag exceeded the 64-bit filter's capacity, and the `'<'`-in-attribute-value check scanned to end of input per attribute rather than to the end of the value. A 40,000-attribute tag went from 393 ms to 2.5 ms
- `skipElement` counted nesting from zero instead of from the parser's current depth, so an unknown subtree could descend `MAX_DEPTH` again
- Streamed attribute capture left its values live past the element they came from: an item beyond a fixed container's capacity is skipped rather than pulled, so nothing consumed the state, and the next element to read attributes by ordinal picked up the skipped element's values even with no attributes of its own
- A child element whose name matched an attribute field satisfied that field's `required` check while leaving the member unset; presence is now recorded only for the kinds actually matched as child elements
- Skipping a markup declaration (`<!` that is neither a comment nor CDATA) resumed by re-entering the tokenizer, costing one stack frame per declaration with nothing to bound it — `MAX_DEPTH` guards element nesting, not sibling markup. A few hundred KB of `<!>` exhausted the stack on any build that did not eliminate the tail call; the tokenizer now loops
- `Date` accepted any day up to 31, so `2026-02-31` parsed and `toSysDays()` silently reported March 3rd. The day is now checked against its month and leap year, and the year against `std::chrono::year`'s range, which `Date` cannot represent beyond
- A UTF-8 BOM returned from the encoding check before the XML declaration was examined, so a BOM followed by `encoding="UTF-16"` was accepted
- `xs:list` elements were escaped with attribute rules, which leave `>` alone: an item spelling `]]>` was written into character data verbatim and the serializer's own output then failed a strict re-parse

### Added
- Non-UTF-8 input is rejected with `ErrorCode::UnsupportedEncoding` (UTF-16/32 BOM, BOM-less UTF-16, or an XML declaration naming a non-UTF-8 encoding) instead of being scanned as bytes and reported as a missing root
- libFuzzer target (`LIGHTNINGXML_BUILD_FUZZERS`) driving all three parser modes plus a serialize/re-parse differential, with a committed seed corpus and a bounded CI run
- CI matrix building and testing gcc/clang across Debug and Release, so the shipped `-O3` + LTO + unity configuration is exercised
- README documents four behaviors that previously read as defects: mixed content keeps only the last text run on the raw string path, `arrField` serializes every slot, a malformed optional attribute is left disengaged, and an `xs:list` item cannot contain whitespace
- Fuzz model covers a fixed container of an attributed type and a list of owning strings, the two shapes whose round-trip invariants it could not previously reach

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
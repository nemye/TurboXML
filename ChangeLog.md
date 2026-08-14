# Changelog

## 1.3.0 - 2026-08-14

### Fixed
- Serializer emitted tab, LF, and CR literally, so `serialize()` -> `deserialize()` lost the bytes; now written as `&#9;`/`&#10;`/`&#13;`
- Element, attribute, and variant dispatch bound a document name on an FNV-1a hash match alone; the declared name is now confirmed
- `StrictParser` duplicate-attribute detection was O(n^2) past the 64-bit filter's capacity, and the `'<'`-in-attribute-value check scanned to end of input per attribute. A 40,000-attribute tag went from 393 ms to 2.5 ms
- `skipElement` counted nesting from zero instead of from the parser's current depth, so an unknown subtree could descend `MAX_DEPTH` again
- Streamed attribute values outlived their element when it was skipped rather than pulled, and were applied by ordinal to the next element to read attributes
- A child element whose name matched an attribute field satisfied that field's `required` check while leaving the member unset
- A run of markup declarations cost one stack frame each; the tokenizer now loops instead of re-entering itself
- `Date` accepted any day up to 31, so `2026-02-31` parsed and `toSysDays()` reported March 3rd. The day is now checked against its month, and the year against `std::chrono::year`'s range
- A UTF-8 BOM skipped the encoding-declaration check, so a BOM followed by `encoding="UTF-16"` was accepted
- `xs:list` elements were escaped with attribute rules, so an item spelling `]]>` failed a strict re-parse of the serializer's own output

### Added
- Non-UTF-8 input is rejected with `ErrorCode::UnsupportedEncoding` (UTF-16/32 BOM, BOM-less UTF-16, or a non-UTF-8 encoding declaration)
- libFuzzer target (`LIGHTNINGXML_BUILD_FUZZERS`) driving all three parser modes plus a serialize/re-parse differential, with a seed corpus and a bounded CI run
- CI matrix building and testing gcc/clang across Debug and Release
- Fuzz model covers a fixed container of an attributed type and a list of owning strings
- README documents mixed-content last-run-wins, `arrField` serializing every slot, disengaged optional attributes on a bad value, and whitespace in `xs:list` items

### Changed
- clang-tidy CI job disables unity builds so `compile_commands.json` resolves the include path
- Reference expansion bounds its terminator search

## 1.2.0 - 2026-07-28

Throughput improvements due to refactor of attribute parsing

### Changed
- Attributes are captured in one streamed typed pass when the element's target type declares no more than 32 of them
- The document-order hint matches a field's open tag and reads that field in one fused step
- Open- and close-tag matching
- Attribute values are scanned with a short inline probe before falling back to `memchr`
- Attribute assignment routes through a shared entry point taking the raw value rather than an `Attribute`, so the vector and streamed paths produce byte-identical normalization and error codes by construction

### Added
- Tests for the streamed attribute path

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
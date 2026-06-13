# Changelog

## 1.2.0 — 2026-06-12

Performance release. No API changes.

### Changed
- Document-order field hint: the opening-tag byte-compare fast path now covers all types, skipping tokenization and field lookup for schema-ordered XML (+18-47% throughput on element-heavy workloads).
- Document-order attribute cursor: attribute fields match at a running cursor instead of per-field linear scans (+8% on attribute-heavy workloads).
- Raw skip scanner: unmapped subtrees are skipped with a quote/comment/CDATA-aware byte scan instead of full tokenization (+19% on unknown-heavy content; new benchmark added).

## 1.1.0 — 2026-06-12

### Added
- `bool` field support: parsed from `true`/`false`/`1`/`0`, serialized as `true`/`false`.

### Changed
- Simplified internals: unified element dispatch (removed the N==1 special case and `handle_element`), merged duplicated serializer escape/number helpers and field factories.

### Fixed
- Skipped subtrees nested deeper than 65,535 levels no longer desync the parser (depth counter widened).
- CMake configure no longer fails when `TURBOXML_BUILD_TESTS` or `TURBOXML_BUILD_BENCHMARKS` is `OFF`.
- README corrections: CMake target is `TurboXML::turboxml`, Quick Start example now compiles.

## 1.0.0 — 2026-06-10

Initial release.

- Header-only C++20 XML pull-parser, deserializer, and serializer (`TurboXML.hh`).
- Declarative field mapping via `XmlMetadata<T>` with `field`, `attr_field`, `vec_field`, and `arr_field`.
- Zero-copy `std::string_view` or owning `std::string` fields; arithmetic types via `std::from_chars`.
- Compile-time FNV-1a field dispatch; extensible containers through `XmlContainerTraits`.
- Serializer with pretty/compact output and XML escaping.
- GoogleTest suite and Google Benchmark comparisons against pugixml.

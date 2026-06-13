# Changelog

## Unreleased

### Changed
- Parser: document-order field hint. The byte-compare fast path for opening tags (previously limited to single-field types) now applies to all types by tracking which field is expected next in document order. Schema-ordered XML skips tokenization, hashing, and field lookup on most elements; out-of-order documents fall back seamlessly. Measured (median of 5, back-to-back A/B): Flat +47%, Large +34%, Catalog +29%, comment-heavy +28%, Org +19%, Small +18% throughput; Deep/Tree/Attr unchanged.

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

# LightningXML

A high-performance, header-only XML pull-parser, deserializer, and serializer for C++20.

Define your structs, declare the field mapping, and deserialize or serialize without any dependencies. The intent of LightningXML is to trade compile-time work for runtime performance. The goal is not to be feature-rich, but to provide a simple API for getting XML-formatted data into your applications for processing as quickly as possible.

Three parser tiers trade performance for conformance to XML 1.0 (Fifth Edition). `xmlight::StrictParser` is the fully-conforming configuration. The default `xmlight::Parser` makes documented trade-offs for speed: it does not expand entities or normalize text, but does handle structural well-formedness including DOCTYPE internal subset skipping and UTF-8 BOM stripping. DTD processing and external entity resolution are intentionally omitted across all tiers (non-validating processor).

## Performance

Benchmarked against [pugixml](https://pugixml.org/), [RapidXML](https://rapidxml.sourceforge.net/) (via the copy bundled in Boost.PropertyTree), and [libxml2](https://gitlab.gnome.org/GNOME/libxml2) on identical workloads. Every parser populates the same output records from the same payloads, so the numbers reflect a fairly comparable performance spectrum. Throughput is measured as bytes of source per second (higher is better); measured on an AMD Ryzen 9 7950X3D, Ubuntu 24.04, Clang 19, `-O3`/Release, pinned to one core (median of 15 repetitions).

| Workload | LightningXML | LightningXML Strict | pugixml | RapidXML | RapidXML fast | libxml2 DOM | libxml2 reader |
|---|---|---|---|---|---|---|---|
| Flat (2K items, 4 fields + attr) | **3.82 GB/s** | 2.67 GB/s | 1.46 GB/s | 199 MB/s | 1.58 GB/s | 178 MB/s | 189 MB/s |
| Deep (2K chains, 5 levels) | **1.72 GB/s** | 1.54 GB/s | 977 MB/s | 792 MB/s | 1.05 GB/s | 101 MB/s | 124 MB/s |
| Attributes (2K items, 10 attrs) | **1.91 GB/s** | 1.08 GB/s | 417 MB/s | 719 MB/s | 886 MB/s | 34 MB/s | 101 MB/s |
| Small (1 element) | **1.95 GB/s** | 1.75 GB/s | 823 MB/s | 886 MB/s | 1.13 GB/s | 68 MB/s | 76 MB/s |
| Large (10K users) | **3.31 GB/s** | 2.43 GB/s | 311 MB/s | 194 MB/s | 340 MB/s | 60 MB/s | 172 MB/s |
| Org (nested, ~400 members) | **1.91 GB/s** | 1.64 GB/s | 785 MB/s | 821 MB/s | 966 MB/s | 135 MB/s | 158 MB/s |
| Tree (depth 14, binary) | **805 MB/s** | 763 MB/s | 300 MB/s | 113 MB/s | 112 MB/s | 115 MB/s | 118 MB/s |
| Comment-heavy (skipped bytes) | **13.22 GB/s** | 8.18 GB/s | 2.87 GB/s | 2.58 GB/s | 3.25 GB/s | 506 MB/s | 590 MB/s |
| Catalog (12 books, owning strings) | **2.71 GB/s** | 1.74 GB/s | 1.33 GB/s | 1.15 GB/s | 1.54 GB/s | 190 MB/s | 215 MB/s |

Column descriptions, ordered from lower to higher feature sets:

- **LightningXML** - zero-copy, non-normalizing, non-validating. `string_view` fields point straight into the source; no DOM, no entity decoding, no allocation for string fields.
- **LightningXML Strict** - `xmlight::StrictParser`: same extraction, but adds the three well-formedness scans (`]]>` in content, `<` in attribute values, duplicate attributes) and normalizes owning `std::string` fields. A fully-conforming configuration; the delta is the cost of validation.
- **pugixml** - builds an owning DOM (the source is copied into it), decodes entities, normalizes; we then walk the tree to fill the records.
- **RapidXML** - in-situ DOM with `parse_default`: parses destructively into a mutable copy of the source, decodes the predefined entities.
- **RapidXML fast** - the same, with `parse_fastest` (no data nodes, no entity translation, no string terminators): much faster, fewer features.
- **libxml2 DOM** - `xmlReadMemory`: copies every string into the tree, decodes entities, fully validates.
- **libxml2 reader** - `xmlTextReader` streaming pull parser. A streaming parser can't hand back views that outlive the cursor, so it copies each field into owning storage as it streams - the streaming-vs-DOM trade-off.

LightningXML leads on every workload. The depth-14 Tree row is allocation-bound and high-variance for the DOM parsers (pugixml/RapidXML), so treat those cells as approximate. All comparison benchmarks are opt-in CMake options and live in [test/bench_LightningXML.cc](test/bench_LightningXML.cc); see the per-section comments there for the exact fairness choices.

## Features

- **Header-only** - primary parsing support is a single file, drop `LightningXML.hh` into your project.
- **Flexibility** - `std::string_view` fields point directly into the source buffer or `std::string` fields materialize copies that outlive the source. Many STL types and containers are natively supported. 
- **Compile-time dispatch** - field lookup via hashing + constexpr dispatch tables result in the fastest parser across nearly every workload tested. 
- **Serializer** - round-trip to well-formed XML with compile-time pretty/compact control
- **Extensible containers** - specialize `XmlContainerTraits` to read into any container type
- **Code generator** - XSD 1.0 code generator covering complexTypes, inheritance, choices, groups, includes, and facet constraints.

## Quick Start

```cpp
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "LightningXML.hh"

struct Book {
  std::string_view id;
  std::string_view author;
  std::string_view title;
  double price{};
};

template <>
struct xmlight::XmlMetadata<Book> {
  static constexpr auto fields = std::make_tuple(
      xmlight::attrField("id",     &Book::id),
      xmlight::field("author",      &Book::author),
      xmlight::field("title",       &Book::title),
      xmlight::field("price",       &Book::price));
};

struct Catalog {
  std::vector<Book> books;
};

template <>
struct xmlight::XmlMetadata<Catalog> {
  static constexpr auto fields =
      std::make_tuple(xmlight::vecField("book", &Catalog::books));
};

int main() {
  std::string_view src = R"(
    <catalog>
      <book id="bk101">
        <author>Gambardella, Matthew</author>
        <title>XML Developer's Guide</title>
        <price>44.95</price>
      </book>
    </catalog>)";

  xmlight::Parser parser{src};
  Catalog catalog;
  if (xmlight::deserialize(parser, "catalog", catalog)) {
    std::cout << catalog.books[0].title << '\n';  // XML Developer's Guide
  }

  // Serialize back to XML
  std::string xml = xmlight::serialize("catalog", catalog);          // pretty
  std::string compact = xmlight::serialize<false>("catalog", catalog); // compact
}
```

## API Reference

### Field Types

| Factory | Description |
|---|---|
| `xmlight::field("name", &T::member)` | Child element: maps `<name>value</name>` to a member |
| `xmlight::attrField("name", &T::member)` | Attribute: maps `name="value"` on the parent tag |
| `xmlight::vecField("name", &T::member)` | Repeated element: appends each `<name>` to a dynamic container |
| `xmlight::arrField("name", &T::member)` | Repeated element: fills a fixed-capacity container sequentially; skips overflow |
| `xmlight::valueField(&T::member)` | The element's **own text** (XSD simpleContent); takes no name - see below |

All five factories accept an optional trailing `required` flag (default `false`,
i.e. fields are optional). When `true`, `deserialize()` fails with
`ErrorCode::MissingRequiredField` if the element/attribute is absent (for
containers, if no item is matched; for a value field, if the element has no
text):

```cpp
xmlight::field("title", &Book::title, true)   // field must be present
xmlight::attrField("id", &Book::id, false)   // optional. Note that the parameter is not needed (default = false) 
```

Types with no required fields pay nothing for the check: presence tracking
compiles away entirely. There is no practical limit on the number of fields a
type may declare.

### Supported Member Types
- **Primitives**: `int`, `unsigned`, `long`, `float`, `double`, and other arithmetic types
- **Booleans**: `bool`, parsed from `true`/`false`/`1`/`0`, serialized as `true`/`false`
- **Strings**: `std::string_view` (zero-copy, must outlive source), `std::string` (owning copy)
- **Enums**: any C++ `enum` with an `XmlEnumTraits` specialization (maps token spellings, e.g. `xs:enumeration`)
- **Dates & times**: `xmlight::Date`, `xmlight::Time`, `xmlight::DateTime` (XSD `date`/`time`/`dateTime`), with `std::chrono` accessors
- **Custom leaf types**: any type with an `XmlValueTraits` specialization (text <-> value)
- **Choices**: `std::variant<...>` (and `std::vector<std::variant<...>>`) via `variantField` (XSD `xs:choice`)
- **Nested objects**: any type with an `XmlMetadata` specialization
- **Optionals**: `std::optional<T>` of any of the above - empty when absent, engaged when present
- **Optional/recursive children**: `std::unique_ptr<T>` - null when absent, allocated when present
- **Dynamic containers**: `std::vector<T>` via `vecField`
- **Fixed containers**: `std::array<T, N>` via `arrField`

### Enumerations

Specialize `xmlight::XmlEnumTraits<E>` with a `values` table mapping each XML token
to its enumerator. Enum members then work as element or attribute fields and
round-trip through the serializer; an unrecognized token fails with
`ErrorCode::InvalidEnumValue`.

```cpp
enum class Priority { Low, Medium, High };

template <>
struct xmlight::XmlEnumTraits<Priority> {
  static constexpr auto values = xmlight::enumTable<Priority>({
      {"Low", Priority::Low}, {"Medium", Priority::Medium}, {"High", Priority::High}});
};

struct Task {
  Priority priority{};   // xmlight::attrField("priority", &Task::priority)
};
```

`enumTable<E>({...})` deduces the entry count for you; a plain
`static constexpr std::array<xmlight::EnumEntry<Priority>, N> values{{...}}` works too.

### Value Fields (element text + attributes)

`xmlight::valueField` binds an element's **own character data** to a member while
attribute fields on the same element still apply - XSD `simpleContent`, e.g.
`<price currency="USD">9.99</price>`. It takes no XML name (the name comes from
where the type is referenced) and the member must be a scalar
(string/number/enum). A type with a value field must not also declare child
element fields.

```cpp
struct Price {
  std::string_view currency;  // attribute
  double amount{};            // the element's text
};
template <>
struct xmlight::XmlMetadata<Price> {
  static constexpr auto fields = std::make_tuple(
      xmlight::attrField("currency", &Price::currency),
      xmlight::valueField(&Price::amount));
};
// used as: xmlight::field("price", &Order::price)  ->  <price currency="USD">9.99</price>
```

### Optional Fields

Wrap any element or attribute member in `std::optional<T>`: it is engaged when
the element/attribute is present and stays empty (`std::nullopt`) when absent.
The serializer omits a disengaged optional. Because an optional is inherently
optional, marking such a field `required` is a compile-time error.

```cpp
struct Person {
  std::optional<int> age;                // xmlight::attrField("age", &Person::age)
  std::optional<std::string_view> nick;  // xmlight::field("nick", &Person::nick)
  std::optional<Address> addr;           // xmlight::field("addr", &Person::addr)
};
```

### Recursive Types

A repeating recursive child is just a `std::vector<T>` of the enclosing type. A
single, optional recursive child uses `std::unique_ptr<T>`: it is null when the
element is absent and heap-allocated when present (and omitted on serialization
when null).

```cpp
struct Section {
  std::string_view title;
  std::unique_ptr<Section> subsection;  // xmlight::field("subsection", &Section::subsection)
};
```

### Choices / Variants

Map an XSD `xs:choice` to a `std::variant<...>` member with `variantField`,
binding each element name to an alternative via `alt<T>("name")` (alternatives
must be distinct types). The matched element selects the alternative; the
serializer emits the active one under its name.

```cpp
struct Circle { int r{}; };   // (each with its own XmlMetadata)
struct Square { int s{}; };

struct Shape {
  std::variant<Circle, Square> kind;
};
template <>
struct xmlight::XmlMetadata<Shape> {
  static constexpr auto fields = std::make_tuple(
      xmlight::variantField(&Shape::kind,
          xmlight::alt<Circle>("circle"), xmlight::alt<Square>("square")));
};
// <Shape><circle r="3"/></Shape>   or   <Shape><square s="7"/></Shape>
```

- A plain `std::variant` models *exactly one* branch. Use
  `requiredVariantField` to require that a branch be present (`minOccurs ≥ 1`,
  else `ErrorCode::MissingRequiredField`).
- For a repeated/interleaved choice (`maxOccurs > 1`), use a dynamic container of
  variant, e.g. `std::vector<std::variant<P, Img, Table>>`; an empty vector means
  the choice never occurred.

### Dates & Times

`xmlight::Date`, `xmlight::Time`, and `xmlight::DateTime` parse the XSD `date` / `time` /
`dateTime` lexical forms (optional `Z` / `±hh:mm` timezone, fractional seconds)
and round-trip through the serializer. Bad input fails with
`ErrorCode::InvalidValue`. Each exposes `std::chrono` accessors and supports all
comparison operators (`==`, `!=`, `<`, `>`, `<=`, `>=`), so they work directly
in `std::set`, `std::map`, and sorted ranges.

```cpp
struct Event {
  xmlight::Date     day;     // xmlight::attrField("day", &Event::day)
  xmlight::DateTime stamp;   // xmlight::field("stamp", &Event::stamp)
};
// ... after deserialize:
std::chrono::sys_days d = event.day.toSysDays();
std::chrono::sys_time<std::chrono::nanoseconds> t = event.stamp.toSysTime(); // UTC
std::set<xmlight::Date> seen;   // ordering works out of the box
```

Any other leaf type with a known text form can be supported by specializing
`xmlight::XmlValueTraits<T>` with `parse(std::string_view, T&)` and
`format(std::string&, const T&)` - the same hook the date types use.

### Deserializer

```cpp
xmlight::Parser parser{xml_string_view};
bool ok = xmlight::deserialize(parser, "root_tag", object);
parser.reset();  // reuse on the same source
```

### Serializer

```cpp
// Returns std::string; kPretty=true adds indentation and newlines
std::string xml = xmlight::serialize("root_tag", object);
std::string xml = xmlight::serialize<false>("root_tag", object);  // compact

// Or drive the serializer directly for repeated writes to one buffer
std::string buf;
xmlight::Serializer<true> s{buf};
s.write("root_tag", object);
```

Attribute values and text content are escaped (`&amp;`, `&lt;`, `&gt;`, `&quot;`). Types whose fields are all attributes serialize as self-closing tags.

An enum value with no entry in its `XmlEnumTraits` table serializes as empty text; keep the table exhaustive (xsdgen always emits every `xs:enumeration` facet).

### Metadata Declaration

Specialize `xmlight::XmlMetadata<T>` for each type. Field order in the tuple does not need to match the XML element order.

```cpp
template <>
struct xmlight::XmlMetadata<MyType> {
  static constexpr auto fields = std::make_tuple(
      xmlight::attrField("id",   &MyType::id),
      xmlight::field("name",      &MyType::name),
      xmlight::vecField("item",  &MyType::items),
      xmlight::arrField("score", &MyType::scores));
};
```

### Custom Containers

Specialize `xmlight::XmlContainerTraits<C>` to teach the deserializer and serializer how to work with any container.

**Dynamic container** (e.g. a custom list, `boost::container::small_vector`):
```cpp
template <typename T>
struct xmlight::XmlContainerTraits<MyList<T>> {
  using value_type = T;
  static T& emplace(MyList<T>& c) { return c.push_back(T{}), c.back(); }
  static void pop(MyList<T>& c)   { c.pop_back(); }
};
```

**Fixed-size container** (e.g. `Eigen::Vector`):
```cpp
template <typename T, int N>
struct xmlight::XmlContainerTraits<Eigen::Matrix<T, N, 1>> {
  using value_type = T;
  static constexpr size_t capacity = N;
  static T& at(Eigen::Matrix<T, N, 1>& c, size_t i)       { return c.coeffRef(i); }
  static const T& at(const Eigen::Matrix<T, N, 1>& c, size_t i) { return c.coeff(i); }
};
```

Then use the field factory that matches the container's semantics:
```cpp
xmlight::vecField("point", &MyStruct::my_eigen_vec)  // or arrField, both work
```

### Owning vs Zero-Copy

Use `std::string_view` for maximum performance when the source buffer outlives the parsed objects. Use `std::string` when you need the data to persist independently. Both use the same `xmlight::field(...)` declaration and can be mixed in the same struct.

### Normalization & Entity Expansion (opt-in)

By default `xmlight::Parser` is zero-copy and **non-normalizing**: it does not expand entities or character references, normalize line endings, or normalize attribute values.

For XML-conformant text, use `xmlight::NormalizingParser` (an alias for `xmlight::BasicParser<ParserOptions{.normalize = true}>`). On this parser, **owning `std::string` fields** receive normalized, reference-expanded text:

- The five predefined entities (`&amp; &lt; &gt; &apos; &quot;`) and decimal/hex character references (`&#65;`, `&#x41;`) are expanded (UTF-8 encoded).
- Line endings (`\r\n`, `\r`) are normalized to `\n`.
- Attribute whitespace (literal tab/newline) is normalized to spaces (XML §3.3.3); whitespace introduced via a reference is preserved.
- CDATA content is copied literally (never reference-expanded) and concatenated with surrounding text.
- `xs:list` values (list elements and list-valued attributes) are reference-expanded before whitespace splitting, except into `std::string_view` items, which stay raw.
- An undefined entity (none of the five predefined; no DTD is processed) fails with `ErrorCode::UndefinedEntity`; a malformed or out-of-range character reference fails with `ErrorCode::InvalidCharRef`.

```cpp
xmlight::NormalizingParser p{src};
xmlight::deserialize(p, "root", obj);   // std::string fields are normalized
```

`std::string_view` fields are **always** raw zero-copy and ignore this setting (a view cannot hold transformed bytes). The default `xmlight::Parser` compiles the normalization paths away entirely, meaning you pay nothing unless you opt in.

### Strict Well-Formedness (opt-in)

The default `xmlight::Parser` is deliberately **non-validating**: for speed it skips three scans the spec requires. `xmlight::StrictParser` (fully conforming - it both normalizes *and* enforces these constraints) rejects:

- `]]>` appearing in character data (Production [14]) → `ErrorCode::CDataEndInContent`
- `<` appearing in an attribute value (Production [10]) → `ErrorCode::LtInAttributeValue`
- duplicate attribute names on one element (WFC: Unique Att Spec) → `ErrorCode::DuplicateAttribute`

```cpp
xmlight::StrictParser p{src};
xmlight::deserialize(p, "root", obj);   // rejects ill-formed input; also normalizes
```

These checks each add a scan to the hot path, so they are **opt-in**: the default `xmlight::Parser` compiles them away entirely (zero cost). Parser policy is selected via the `xmlight::ParserOptions` flags (`normalize`, `strict`); the `Parser` / `NormalizingParser` / `StrictParser` aliases cover the common combinations, or use `xmlight::BasicParser<ParserOptions{...}>` directly.

## Generating from XSD

The `xsdgen` tool turns an XSD schema into the matching `XmlMetadata` definitions,
so you don't hand-write the mapping. The schema is parsed with LightningXML itself.

```bash
./build/lightningxml_xsdgen schema.xsd -o schema_generated.hh   # stdout if no -o
```

Supported XSD constructs:

| XSD | Generated C++ |
|---|---|
| `xs:complexType` | `struct` + `XmlMetadata<T>` |
| `xs:element` / `xs:attribute` | `field` / `attrField` (required, optional, or vector by `minOccurs`/`maxOccurs`/`use`) |
| `xs:simpleType` enumeration | `enum class` + `XmlEnumTraits<E>` |
| `xs:simpleContent` extension | `valueField` + attribute fields |
| `xs:complexContent` extension | `struct Child : Parent` with merged `XmlMetadata<Child>` |
| `xs:choice` | `std::variant<...>` + `variantField` |
| `xs:attributeGroup` / `xs:group` | Expanded inline into the referencing type |
| `xs:include` | Loaded relative to the input schema's directory (CLI) or a loader callback (`xsd::Options::loader`) |
| XSD facets (`minLength`, `maxLength`, `length`, `pattern`, `minInclusive`, `maxInclusive`, …) | `XmlConstraints<T>` specialization checked by `xmlight::validate()` |
| Attribute `default` | C++ default member initializer |
| Attribute `fixed` | Default initializer + `XmlConstraints<T>` equality check |
| Finite `maxOccurs=N` | `std::vector<T>` member + `XmlConstraints<T>` size check |
| Built-in types | `std::string`, `int`, `double`, `bool`, `xmlight::Date`, `xmlight::Time`, `xmlight::DateTime`, … |
| Recursive types | `std::unique_ptr<T>` (optional self-reference) or `std::vector<T>` (repeated) |

Unsupported constructs are reported as notes on `stderr` rather than causing a failure; the generator produces the best output it can for the rest of the schema.

**Not supported (out of scope):**
- `xs:union` - no C++ type mapping without boxing
- `xs:import` - cross-namespace schema merging requires a resolver not in scope
- `xs:complexContent restriction` - high complexity, rare in practice
- `xs:any` / `xs:anyAttribute` - wildcards have no static type
- External entity resolution - requires file I/O, incompatible with the zero-copy design

Built with `LIGHTNINGXML_BUILD_CODEGEN` (on by default).

### Constraint Validation

When types carry XSD constraints, `xsdgen` emits `xmlight::XmlConstraints<T>` specializations alongside the metadata. Call `xmlight::validate()` after deserialization:

```cpp
MyType obj;
xmlight::deserialize(parser, "root", obj);
if (auto err = xmlight::validate(obj)) {
  std::cerr << "constraint violation: " << err->message << '\n';
}
```

`xmlight::validate()` returns `std::optional<xmlight::ValidationError>` - empty when all constraints pass, or the first violation in `err->message`. Validation is **deep**: after checking the object's own `XmlConstraints`, it recurses through the declared fields into nested objects and the contents of containers, optionals, `unique_ptr`s, and variants, so validating the root covers the whole tree. This is intentionally distinct from `xmlight::ErrorCode` (parser errors) so the two failure modes can be handled independently. Types with no constraints use the default no-op specialization, which compiles away entirely.

Facets on an `xs:choice` branch cannot be checked per-member (the branch lives in a `std::variant`); the generator reports these as notes instead of emitting checks.

## Building

### Requirements

- C++20 compiler (Tested with Clang 19)
- CMake 3.16+ (for tests/benchmarks)

### Header-Only Usage

Copy `include/LightningXML.hh` into your project. No build step required.

### CMake Integration

```bash
./build.sh         # build and install tests and benchmarks
./bin/lightningxml_tests
./bin/lightningxml_bench
```

### CMake Options

| Option | Default | Description |
|---|---|---|
| `LIGHTNINGXML_BUILD_TESTS` | `ON` | Build unit tests (fetches GTest if not found) |
| `LIGHTNINGXML_BUILD_BENCHMARKS` | `ON` | Build benchmarks (fetches Google Benchmark if not found) |
| `LIGHTNINGXML_BUILD_CODEGEN` | `ON` | Build the `xsdgen` XSD → `XmlMetadata` code generator |
| `LIGHTNINGXML_WITH_PUGIXML` | `OFF` | Build pugixml comparison benchmarks (fetches pugixml if not found) |
| `LIGHTNINGXML_WITH_RAPIDXML` | `OFF` | Build RapidXML comparison benchmarks (fetches Boost, uses its bundled RapidXML) |
| `LIGHTNINGXML_WITH_LIBXML2` | `OFF` | Build libxml2 comparison benchmarks (fetches libxml2 if not found) |
| `LIGHTNINGXML_ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer |
| `LIGHTNINGXML_ENABLE_UNITY_BUILD` | `ON` | Compile executables as unity builds |
| `LIGHTNINGXML_ENABLE_LTO` | `ON` | Enable link-time optimization (IPO) for executables |


### As a CMake Subdirectory

```cmake
add_subdirectory(LightningXML)
target_link_libraries(my_target PRIVATE LightningXML::lightningxml)
```

### As an Installed Package

```bash
cmake --install build --prefix /your/prefix   # header, xsdgen, CMake package config
```

```cmake
find_package(LightningXML 1.0 REQUIRED)
target_link_libraries(my_target PRIVATE LightningXML::lightningxml)
```

## Project Layout

```
├── CMakeLists.txt
├── LICENSE
├── README.md
├── build.sh
├── clean.sh
├── include/
│   └── LightningXML.hh
├── tools/
│   ├── XSDCodegen.hh        # XSD -> XmlMetadata generator (library)
│   └── XSDGen.cc            # CLI front-end
├── test/
│   ├── bench_LightningXML.cc
│   ├── test_Helpers.hh
│   ├── test_LightningXML.cc
│   └── test_XSDCodegen.cc
```

## License

MIT License

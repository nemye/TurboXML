# TurboXML

A high-performance, header-only XML pull-parser, deserializer, and serializer for C++20.

Define your structs, declare the field mapping, and deserialize or serialize without any code generation or required dependencies. The intent of TurboXML is to trade compile-time work for runtime performance. The goal is not to be feature-rich, but to provide a simple API for getting XML-formatted data into your applications for processing as quickly as possible.

## Performance

Benchmarked against [pugixml](https://pugixml.org/), [RapidXML](https://rapidxml.sourceforge.net/) (via the copy bundled in Boost.PropertyTree), and [libxml2](https://gitlab.gnome.org/GNOME/libxml2) on identical workloads. Every parser populates the same output records from the same payloads, so the numbers reflect a **feature/performance spectrum** rather than differing work. Throughput is bytes of source per second (higher is better); measured on an AMD Ryzen 9 7950X3D, Ubuntu 24.04, Clang 19, `-O2`/Release, pinned to one core.

| Workload | TurboXML | TurboXML Strict | pugixml | RapidXML | RapidXML fast | libxml2 DOM | libxml2 reader |
|---|---|---|---|---|---|---|---|
| Flat (2K items, 4 fields + attr) | **2.83 GB/s** | 2.41 GB/s | 1.53 GB/s | 202 MB/s | 1.58 GB/s | 177 MB/s | 191 MB/s |
| Deep (2K chains, 5 levels) | **1.63 GB/s** | 1.58 GB/s | 1.07 GB/s | 777 MB/s | 1.05 GB/s | 103 MB/s | 131 MB/s |
| Attributes (2K items, 10 attrs) | **1.13 GB/s** | 839 MB/s | 435 MB/s | 710 MB/s | 904 MB/s | 34 MB/s | 101 MB/s |
| Small (1 element) | **1.63 GB/s** | 1.54 GB/s | 811 MB/s | 891 MB/s | 1.14 GB/s | 68 MB/s | 78 MB/s |
| Large (10K users) | **2.67 GB/s** | 2.27 GB/s | 326 MB/s | 214 MB/s | 355 MB/s | 66 MB/s | 175 MB/s |
| Org (nested, ~400 members) | **1.61 GB/s** | 1.46 GB/s | 818 MB/s | 842 MB/s | 1.02 GB/s | 137 MB/s | 163 MB/s |
| Tree (depth 14, binary) | 526 MB/s | **536 MB/s** | 123 MB/s | 119 MB/s | 115 MB/s | 115 MB/s | 122 MB/s |
| Comment-heavy (skipped bytes) | **11.16 GB/s** | 9.89 GB/s | 3.29 GB/s | 2.86 GB/s | 3.32 GB/s | 517 MB/s | 600 MB/s |
| Catalog (12 books, owning strings) | **2.57 GB/s** | 1.67 GB/s | 1.35 GB/s | 1.16 GB/s | 1.58 GB/s | 189 MB/s | 217 MB/s |

What each column does, lowest to highest feature set:

- **TurboXML** - zero-copy, non-normalizing, non-validating. `string_view` fields point straight into the source; no DOM, no entity decoding, no allocation for string fields.
- **TurboXML Strict** - `xml::StrictParser`: same extraction, but adds the three well-formedness scans (`]]>` in content, `<` in attribute values, duplicate attributes) and normalizes owning `std::string` fields. A fully-conforming configuration; the delta is the cost of validation.
- **pugixml** - builds an owning DOM (the source is copied into it), decodes entities, normalizes; we then walk the tree to fill the records.
- **RapidXML** - in-situ DOM with `parse_default`: parses destructively into a mutable copy of the source (copy timed, like pugixml's), decodes the predefined entities. The whitespace-heavy payloads (Flat, Large) are pathological for its default data-node creation.
- **RapidXML fast** - the same, with `parse_fastest` (no data nodes, no entity translation, no string terminators): much faster, fewer features.
- **libxml2 DOM** - `xmlReadMemory`: the feature-rich end - copies every string into the tree, decodes entities, fully validates.
- **libxml2 reader** - `xmlTextReader` streaming pull parser. A streaming parser can't hand back views that outlive the cursor, so it copies each field into owning storage as it streams - the streaming-vs-DOM trade-off.

TurboXML leads on every workload (1.4–8× over pugixml; the Tree case is a near-tie with its own strict mode). All comparison benchmarks are opt-in CMake options and live in [test/bench_TurboXML.cc](test/bench_TurboXML.cc); see the per-section comments there for the exact fairness choices.

## Features

- **Header-only** - single file, drop `TurboXML.hh` into your project
- **Flexibility** - `std::string_view` fields point directly into the source buffer or `std::string` fields materialize copies that outlive the source
- **Compile-time dispatch** - field lookup via FNV-1a hash + constexpr dispatch tables
- **Serializer** - round-trip to well-formed XML with compile-time pretty/compact control
- **Extensible containers** - specialize `XmlContainerTraits` to read into any container type

## Quick Start

```cpp
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "TurboXML.hh"

struct Book {
  std::string_view id;
  std::string_view author;
  std::string_view title;
  double price{};
};

template <>
struct xml::XmlMetadata<Book> {
  static constexpr auto fields = std::make_tuple(
      xml::attr_field("id",     &Book::id),
      xml::field("author",      &Book::author),
      xml::field("title",       &Book::title),
      xml::field("price",       &Book::price));
};

struct Catalog {
  std::vector<Book> books;
};

template <>
struct xml::XmlMetadata<Catalog> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("book", &Catalog::books));
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

  xml::Parser parser{src};
  Catalog catalog;
  if (xml::deserialize(parser, "catalog", catalog)) {
    std::cout << catalog.books[0].title << '\n';  // XML Developer's Guide
  }

  // Serialize back to XML
  std::string xml = xml::serialize("catalog", catalog);          // pretty
  std::string compact = xml::serialize<false>("catalog", catalog); // compact
}
```

## API Reference

### Field Types

| Factory | Description |
|---|---|
| `xml::field("name", &T::member)` | Child element: maps `<name>value</name>` to a member |
| `xml::attr_field("name", &T::member)` | Attribute: maps `name="value"` on the parent tag |
| `xml::vec_field("name", &T::member)` | Repeated element: appends each `<name>` to a dynamic container |
| `xml::arr_field("name", &T::member)` | Repeated element: fills a fixed-capacity container sequentially; skips overflow |
| `xml::value_field(&T::member)` | The element's **own text** (XSD simpleContent); takes no name - see below |

All five factories accept an optional trailing `required` flag (default `false`,
i.e. fields are optional). When `true`, `deserialize()` fails with
`ErrorCode::MissingRequiredField` if the element/attribute is absent (for
containers, if no item is matched; for a value field, if the element has no
text):

```cpp
xml::field("title", &Book::title, true)   // field must be present
xml::attr_field("id", &Book::id, false)   // optional. Note that the parameter is not needed (default = false) 
```

Types with no required fields pay nothing for the check: presence tracking
compiles away entirely. There is no practical limit on the number of fields a
type may declare.

### Supported Member Types
- **Primitives**: `int`, `unsigned`, `long`, `float`, `double`, and other arithmetic types
- **Booleans**: `bool`, parsed from `true`/`false`/`1`/`0`, serialized as `true`/`false`
- **Strings**: `std::string_view` (zero-copy, must outlive source), `std::string` (owning copy)
- **Enums**: any C++ `enum` with an `XmlEnumTraits` specialization (maps token spellings, e.g. `xs:enumeration`)
- **Dates & times**: `xml::Date`, `xml::Time`, `xml::DateTime` (XSD `date`/`time`/`dateTime`), with `std::chrono` accessors
- **Custom leaf types**: any type with an `XmlValueTraits` specialization (text <-> value)
- **Choices**: `std::variant<...>` (and `std::vector<std::variant<...>>`) via `variant_field` (XSD `xs:choice`)
- **Nested objects**: any type with an `XmlMetadata` specialization
- **Optional/recursive children**: `std::unique_ptr<T>` of an `XmlObject` - null when absent, allocated when present
- **Dynamic containers**: `std::vector<T>` via `vec_field`
- **Fixed containers**: `std::array<T, N>` via `arr_field`

### Enumerations

Specialize `xml::XmlEnumTraits<E>` with a `values` table mapping each XML token
to its enumerator. Enum members then work as element or attribute fields and
round-trip through the serializer; an unrecognized token fails with
`ErrorCode::InvalidEnumValue`.

```cpp
enum class Priority { Low, Medium, High };

template <>
struct xml::XmlEnumTraits<Priority> {
  static constexpr auto values = xml::enum_table<Priority>({
      {"Low", Priority::Low}, {"Medium", Priority::Medium}, {"High", Priority::High}});
};

struct Task {
  Priority priority{};   // xml::attr_field("priority", &Task::priority)
};
```

`enum_table<E>({...})` deduces the entry count for you; a plain
`static constexpr std::array<xml::EnumEntry<Priority>, N> values{{...}}` works too.

### Value Fields (element text + attributes)

`xml::value_field` binds an element's **own character data** to a member while
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
struct xml::XmlMetadata<Price> {
  static constexpr auto fields = std::make_tuple(
      xml::attr_field("currency", &Price::currency),
      xml::value_field(&Price::amount));
};
// used as: xml::field("price", &Order::price)  ->  <price currency="USD">9.99</price>
```

### Recursive Types

A repeating recursive child is just a `std::vector<T>` of the enclosing type. A
single, optional recursive child uses `std::unique_ptr<T>`: it is null when the
element is absent and heap-allocated when present (and omitted on serialization
when null).

```cpp
struct Section {
  std::string_view title;
  std::unique_ptr<Section> subsection;  // xml::field("subsection", &Section::subsection)
};
```

### Choices / Variants

Map an XSD `xs:choice` to a `std::variant<...>` member with `variant_field`,
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
struct xml::XmlMetadata<Shape> {
  static constexpr auto fields = std::make_tuple(
      xml::variant_field(&Shape::kind,
          xml::alt<Circle>("circle"), xml::alt<Square>("square")));
};
// <Shape><circle r="3"/></Shape>   or   <Shape><square s="7"/></Shape>
```

- A plain `std::variant` models *exactly one* branch. Use
  `required_variant_field` to require that a branch be present (`minOccurs ≥ 1`,
  else `ErrorCode::MissingRequiredField`).
- For a repeated/interleaved choice (`maxOccurs > 1`), use a dynamic container of
  variant, e.g. `std::vector<std::variant<P, Img, Table>>`; an empty vector means
  the choice never occurred.

### Dates & Times

`xml::Date`, `xml::Time`, and `xml::DateTime` parse the XSD `date` / `time` /
`dateTime` lexical forms (optional `Z` / `±hh:mm` timezone, fractional seconds)
and round-trip through the serializer. Bad input fails with
`ErrorCode::InvalidValue`. Each exposes `std::chrono` accessors.

```cpp
struct Event {
  xml::Date     day;     // xml::attr_field("day", &Event::day)
  xml::DateTime stamp;   // xml::field("stamp", &Event::stamp)
};
// ... after deserialize:
std::chrono::sys_days d = event.day.to_sys_days();
std::chrono::sys_time<std::chrono::nanoseconds> t = event.stamp.to_sys_time(); // UTC
```

Any other leaf type with a known text form can be supported by specializing
`xml::XmlValueTraits<T>` with `parse(std::string_view, T&)` and
`format(std::string&, const T&)` - the same hook the date types use.

### Deserializer

```cpp
xml::Parser parser{xml_string_view};
bool ok = xml::deserialize(parser, "root_tag", object);
parser.reset();  // reuse on the same source
```

### Serializer

```cpp
// Returns std::string; kPretty=true adds indentation and newlines
std::string xml = xml::serialize("root_tag", object);
std::string xml = xml::serialize<false>("root_tag", object);  // compact

// Or drive the serializer directly for repeated writes to one buffer
std::string buf;
xml::Serializer<true> s{buf};
s.write("root_tag", object);
```

Attribute values and text content are escaped (`&amp;`, `&lt;`, `&gt;`, `&quot;`). Types whose fields are all attributes serialize as self-closing tags.

### Metadata Declaration

Specialize `xml::XmlMetadata<T>` for each type. Field order in the tuple does not need to match the XML element order.

```cpp
template <>
struct xml::XmlMetadata<MyType> {
  static constexpr auto fields = std::make_tuple(
      xml::attr_field("id",   &MyType::id),
      xml::field("name",      &MyType::name),
      xml::vec_field("item",  &MyType::items),
      xml::arr_field("score", &MyType::scores));
};
```

### Custom Containers

Specialize `xml::XmlContainerTraits<C>` to teach the deserializer and serializer how to work with any container.

**Dynamic container** (e.g. a custom list, `boost::container::small_vector`):
```cpp
template <typename T>
struct xml::XmlContainerTraits<MyList<T>> {
  using value_type = T;
  static T& emplace(MyList<T>& c) { return c.push_back(T{}), c.back(); }
  static void pop(MyList<T>& c)   { c.pop_back(); }
};
```

**Fixed-size container** (e.g. `Eigen::Vector`):
```cpp
template <typename T, int N>
struct xml::XmlContainerTraits<Eigen::Matrix<T, N, 1>> {
  using value_type = T;
  static constexpr size_t capacity = N;
  static T& at(Eigen::Matrix<T, N, 1>& c, size_t i)       { return c.coeffRef(i); }
  static const T& at(const Eigen::Matrix<T, N, 1>& c, size_t i) { return c.coeff(i); }
};
```

Then use the field factory that matches the container's semantics:
```cpp
xml::vec_field("point", &MyStruct::my_eigen_vec)  // or arr_field, both work
```

### Owning vs Zero-Copy

Use `std::string_view` for maximum performance when the source buffer outlives the parsed objects. Use `std::string` when you need the data to persist independently. Both use the same `xml::field(...)` declaration and can be mixed in the same struct.

### Normalization & Entity Expansion (opt-in)

By default `xml::Parser` is zero-copy and **non-normalizing**: it does not expand entities or character references, normalize line endings, or normalize attribute values.

For XML-conformant text, use `xml::NormalizingParser` (an alias for `xml::BasicParser<ParserOptions{.normalize = true}>`). On this parser, **owning `std::string` fields** receive normalized, reference-expanded text:

- The five predefined entities (`&amp; &lt; &gt; &apos; &quot;`) and decimal/hex character references (`&#65;`, `&#x41;`) are expanded (UTF-8 encoded).
- Line endings (`\r\n`, `\r`) are normalized to `\n`.
- Attribute whitespace (literal tab/newline) is normalized to spaces (XML §3.3.3); whitespace introduced via a reference is preserved.
- CDATA content is copied literally (never reference-expanded) and concatenated with surrounding text.
- An undefined entity (none of the five predefined; no DTD is processed) fails with `ErrorCode::UndefinedEntity`; a malformed or out-of-range character reference fails with `ErrorCode::InvalidCharRef`.

```cpp
xml::NormalizingParser p{src};
xml::deserialize(p, "root", obj);   // std::string fields are normalized
```

`std::string_view` fields are **always** raw zero-copy and ignore this setting (a view cannot hold transformed bytes). The default `xml::Parser` compiles the normalization paths away entirely, meaning you pay nothing unless you opt in.

### Strict Well-Formedness (opt-in)

The default `xml::Parser` is deliberately **non-validating**: for speed it skips three scans the spec requires. `xml::StrictParser` (fully conforming - it both normalizes *and* enforces these constraints) rejects:

- `]]>` appearing in character data (Production [14]) → `ErrorCode::CDataEndInContent`
- `<` appearing in an attribute value (Production [10]) → `ErrorCode::LtInAttributeValue`
- duplicate attribute names on one element (WFC: Unique Att Spec) → `ErrorCode::DuplicateAttribute`

```cpp
xml::StrictParser p{src};
xml::deserialize(p, "root", obj);   // rejects ill-formed input; also normalizes
```

These checks each add a scan to the hot path, so they are **opt-in**: the default `xml::Parser` compiles them away entirely (zero cost). Parser policy is selected via the `xml::ParserOptions` flags (`normalize`, `strict`); the `Parser` / `NormalizingParser` / `StrictParser` aliases cover the common combinations, or use `xml::BasicParser<ParserOptions{...}>` directly.

## Building

### Requirements

- C++20 compiler (Tested with Clang 19)
- CMake 3.16+ (for tests/benchmarks)

### Header-Only Usage

Copy `include/TurboXML.hh` into your project. No build step required.

### CMake Integration

```bash
./build.sh         # build and install tests and benchmarks
./bin/turboxml_tests
./bin/turboxml_bench
```

### CMake Options

| Option | Default | Description |
|---|---|---|
| `TURBOXML_BUILD_TESTS` | `ON` | Build unit tests (fetches GTest if not found) |
| `TURBOXML_BUILD_BENCHMARKS` | `ON` | Build benchmarks (fetches Google Benchmark if not found) |
| `TURBOXML_WITH_PUGIXML` | `OFF` | Build pugixml comparison benchmarks (fetches pugixml if not found) |
| `TURBOXML_WITH_RAPIDXML` | `OFF` | Build RapidXML comparison benchmarks (fetches Boost, uses its bundled RapidXML) |
| `TURBOXML_WITH_LIBXML2` | `OFF` | Build libxml2 comparison benchmarks (fetches libxml2 if not found) |

### As a CMake Subdirectory

```cmake
add_subdirectory(TurboXML)
target_link_libraries(my_target PRIVATE TurboXML::turboxml)
```

## Project Layout

```
├── CMakeLists.txt
├── LICENSE
├── README.md
├── build.sh
├── clean.sh
├── include/
│   └── TurboXML.hh
├── test/
│   ├── bench_TurboXML.cc
│   ├── Helpers.hh
│   └── test_TurboXML.cc
```

## License

MIT License

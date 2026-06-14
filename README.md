# TurboXML

A high-performance, header-only XML pull-parser, deserializer, and serializer for C++20.

Define your structs, declare the field mapping, and deserialize or serialize without any code generation or required dependencies. The intent of TurboXML is to trade compile-time work for runtime performance. The goal is not to be feature-rich, but to provide a simple API for getting XML-formatted data into your applications for processing as quickly as possible.

## Performance

Benchmarked against [pugixml](https://pugixml.org/) on identical workloads. Both libraries populate similar outputs, attempting to be as fair as possible. Benchmarked on Ubuntu 24.04 with an AMD Ryzen 9 7950X3D 16-Core Processor, 32 GB DDR5 RAM. 

| Workload | TurboXML | pugixml | Speedup |
|---|---|---|---|
| Flat (2K items, 4 fields + attr) | 1.92 GB/s | 1.47 GB/s | **1.3×** |
| Deep (2K chains, 5 levels each) | 1.70 GB/s | 1.00 GB/s | **1.7×** |
| Attributes (2K items, 10 attrs) | 1.06 GB/s | 418 MB/s | **2.5×** |
| Small (1 element) | 1.28 GB/s | 660 MB/s | **1.9×** |
| Large (10K users) | 1.87 GB/s | 375 MB/s | **5.0×** |

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

All four factories accept an optional trailing `required` flag (default `false`,
i.e. fields are optional). When `true`, `deserialize()` fails with
`ErrorCode::MissingRequiredField` if the element/attribute is absent (for
containers, if no item is matched):

```cpp
xml::field("title", &Book::title, true)   // field must be present
xml::attr_field("id", &Book::id, false)   // optional. Note that the parameter is not needed (default = false) 
```

Types with no required fields pay nothing for the check: presence tracking
compiles away entirely.

### Supported Member Types
- **Primitives**: `int`, `unsigned`, `long`, `float`, `double`, and other arithmetic types
- **Booleans**: `bool`, parsed from `true`/`false`/`1`/`0`, serialized as `true`/`false`
- **Strings**: `std::string_view` (zero-copy, must outlive source), `std::string` (owning copy)
- **Nested objects**: any type with an `XmlMetadata` specialization
- **Dynamic containers**: `std::vector<T>` via `vec_field`
- **Fixed containers**: `std::array<T, N>` via `arr_field`

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

For XML-conformant text, use `xml::NormalizingParser` (an alias for `xml::BasicParser<true>`). On this parser, **owning `std::string` fields** receive normalized, reference-expanded text:

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

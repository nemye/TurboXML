/// @file TurboXML.hh
/// @brief High-performance C++20 XML pull-parsing deserializer and serializer.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace xml {

/// @brief FNV-1a hash of an XML field name used for O(1) field dispatch.
using FieldHash = uint64_t;

/// @brief Token types produced by the pull parser.
enum class TokenType : uint8_t {
  ElementOpen,            ///< Opening element tag.
  ElementClose,           ///< Closing element tag.
  Text,                   ///< Character data between tags.
  CData,                  ///< CDATA section content.
  Comment,                ///< XML comment.
  ProcessingInstruction,  ///< Processing instruction.
  XmlDeclaration,         ///< XML declaration (<?xml ... ?>).
  Error,                  ///< Parse error; see Parser::error_code().
};

/// @brief Specific reason the most recent parse failed.
///
/// Query via Parser::error_code() after deserialize() returns false. The
/// first error encountered wins; later cascading failures do not overwrite it.
enum class ErrorCode : uint8_t {
  None = 0,  ///< No error.
  // Start-tag / attribute structure
  UnexpectedEndAfterLt,   ///< "<" with nothing after it.
  UnexpectedCharAfterLt,  ///< Char after "<" cannot begin markup.
  ExpectedElementName,    ///< Missing element name in a start-tag.
  UnclosedTag,            ///< Input ended while scanning a start-tag.
  ExpectedAttributeName,  ///< Non-name char where an attribute was expected.
  TooManyAttributes,      ///< Start-tag exceeds kMaxAttributesPerElement.
  ExpectedEquals,         ///< Missing '=' after an attribute name.
  ExpectedQuotedValue,    ///< Attribute value not quoted.
  UnterminatedAttributeValue,  ///< No closing quote on an attribute value.
  ExpectedNameInCloseTag,      ///< Empty name in an end-tag ("</>").
  ExpectedCloseTagEnd,         ///< Missing '>' in an end-tag.
  // Unterminated / malformed constructs
  ExpectedPiTarget,     ///< Missing PI target name.
  ReservedPiTarget,     ///< PI target is a reserved case-variant of "xml".
  UnterminatedComment,  ///< Comment with no "-->".
  MalformedComment,     ///< "--" appears inside a comment's content.
  UnterminatedCData,    ///< CDATA section with no "]]>".
  UnterminatedPi,       ///< PI with no "?>".
  // Content / structure
  InvalidNumericValue,   ///< Numeric/bool field text failed to parse.
  RootElementNotFound,   ///< Requested root element not present.
  ElementMismatch,       ///< End-tag name does not match its start-tag.
  UnexpectedEof,         ///< Input ended mid-element.
  DepthExceeded,         ///< Nesting deeper than kMaxDepth.
  MissingRequiredField,  ///< A field marked required was absent from the
                         ///< element.
  UndefinedEntity,       ///< Reference to an entity that is not one of the five
                         ///< predefined entities (no DTD is processed).
  InvalidCharRef,        ///< Malformed or out-of-range character reference
                         ///< (e.g. "&#;", "&#xZZ;", or a non-XML code point).
};

/// @brief A parsed XML attribute from an element's opening tag.
struct Attribute {
  std::string_view name;    ///< Local attribute name.
  std::string_view prefix;  ///< Namespace prefix, or empty.
  std::string_view value;   ///< Raw attribute value (not unescaped).
  FieldHash name_hash{};    ///< FNV-1a hash of name.
};

/// @brief A single token produced by the pull parser.
struct Token {
  std::string_view name;    ///< Element or PI target name (local part).
  std::string_view prefix;  ///< Namespace prefix, or empty.
  std::string_view data;    ///< Text, CDATA, comment, or PI content.
  FieldHash name_hash{};    ///< FNV-1a hash of name.
  TokenType type{};         ///< Token kind.
  bool self_closing{};      ///< True for self-closing elements (<foo/>).
};

// FNV-1a helpers
namespace detail {

static constexpr FieldHash kFnvOffset = 14695981039346656037ULL;
static constexpr FieldHash kFnvPrime = 1099511628211ULL;

/// @brief Accumulates one byte into a running FNV-1a hash.
constexpr FieldHash fnv1a_step(FieldHash h, unsigned char c) noexcept {
  return (h ^ c) * kFnvPrime;
}

/// @brief Computes the FNV-1a hash of a string at compile time.
constexpr FieldHash hash_field_name(std::string_view s) noexcept {
  return std::accumulate(s.begin(), s.end(), kFnvOffset,
                         [](FieldHash h, char c) {
                           return fnv1a_step(h, static_cast<unsigned char>(c));
                         });
}

}  // namespace detail

/// @brief Classifies a field as a child element, attribute, or container.
enum class FieldKind : uint8_t { Element, Attr, Container };

/// @brief Compile-time descriptor binding an XML name to a class data member.
/// @tparam K      Field kind.
/// @tparam Class  Containing class type.
/// @tparam Member Member pointer type.
template <FieldKind K, typename Class, typename Member>
struct FieldBase {
  static constexpr FieldKind kind = K;
  std::string_view xml_name;
  Member Class::* member;
  FieldHash hash;
  bool required;  ///< If true, deserialize() fails when the field is absent.
};

/// @brief Descriptor for a scalar child element field.
template <typename C, typename M>
using Field = FieldBase<FieldKind::Element, C, M>;
/// @brief Descriptor for an XML attribute field.
template <typename C, typename M>
using AttrField = FieldBase<FieldKind::Attr, C, M>;
/// @brief Descriptor for a repeating child element stored in a container.
template <typename C, typename M>
using ContainerField = FieldBase<FieldKind::Container, C, M>;

namespace detail {

/// @brief Shared factory behind field/attr_field/vec_field/arr_field.
template <FieldKind K, typename C, typename M>
constexpr auto make_field(std::string_view name, M C::* m, bool required)
    -> FieldBase<K, C, M> {
  return {name, m, hash_field_name(name), required};
}

}  // namespace detail

/// @brief Creates an element field descriptor.
/// @param name     XML element name.
/// @param m        Pointer to the target member.
/// @param required If true, deserialize() fails unless the element is present.
template <typename C, typename M>
constexpr auto field(std::string_view name, M C::* m, bool required = false)
    -> Field<C, M> {
  return detail::make_field<FieldKind::Element>(name, m, required);
}

/// @brief Creates an attribute field descriptor.
/// @param name     XML attribute name.
/// @param m        Pointer to the target member.
/// @param required If true, deserialize() fails unless the attribute is
/// present.
template <typename C, typename M>
constexpr auto attr_field(std::string_view name, M C::* m,
                          bool required = false) -> AttrField<C, M> {
  return detail::make_field<FieldKind::Attr>(name, m, required);
}

/// @brief Creates a container field descriptor for dynamic containers (e.g.,
/// std::vector).
/// @param name     XML element name for each item.
/// @param m        Pointer to the target member.
/// @param required If true, deserialize() fails unless at least one item is
///                 present.
template <typename C, typename M>
constexpr auto vec_field(std::string_view name, M C::* m, bool required = false)
    -> ContainerField<C, M> {
  return detail::make_field<FieldKind::Container>(name, m, required);
}

/// @brief Creates a container field descriptor for fixed containers (e.g.,
/// std::array).
/// @param name     XML element name for each item.
/// @param m        Pointer to the target member.
/// @param required If true, deserialize() fails unless at least one item is
///                 present.
template <typename C, typename M>
constexpr auto arr_field(std::string_view name, M C::* m, bool required = false)
    -> ContainerField<C, M> {
  return detail::make_field<FieldKind::Container>(name, m, required);
}

// Metadata + concepts
/// @brief Specialize this to register XML field mappings for type T.
///
/// The specialization must provide `static constexpr auto fields` as a tuple
/// of Field, AttrField, or ContainerField descriptors.
/// @tparam T User-defined struct to describe.
template <typename T>
struct XmlMetadata;

/// @brief Satisfied when T has an XmlMetadata specialization with a fields
/// tuple.
template <typename T>
concept XmlObject = requires { XmlMetadata<T>::fields; };

/// @brief Satisfied when T is std::string or std::string_view.
template <typename T>
concept XmlStringLike =
    std::same_as<T, std::string_view> || std::same_as<T, std::string>;

/// @brief Satisfied when T is a numeric type or a string-like type.
template <typename T>
concept XmlPrimitive = std::is_arithmetic_v<T> || XmlStringLike<T>;

/// @brief Adapts a container for use with ContainerField. Specialize for custom
/// types.
/// @tparam C Container type to adapt.
template <typename C>
struct XmlContainerTraits;

/// @brief XmlContainerTraits specialization for std::vector.
template <typename T, typename A>
struct XmlContainerTraits<std::vector<T, A>> {
  using value_type = T;
  static T& emplace(std::vector<T, A>& c) { return c.emplace_back(); }
  static void pop(std::vector<T, A>& c) { c.pop_back(); }
};

/// @brief XmlContainerTraits specialization for std::array.
template <typename T, size_t N>
struct XmlContainerTraits<std::array<T, N>> {
  using value_type = T;
  static constexpr size_t capacity = N;
  static T& at(std::array<T, N>& c, size_t i) { return c[i]; }
  static const T& at(const std::array<T, N>& c, size_t i) { return c[i]; }
};

/// @brief Satisfied when C supports dynamic insertion via XmlContainerTraits
/// (e.g., std::vector).
template <typename C>
concept XmlDynContainer = requires(C& c) {
  typename XmlContainerTraits<C>::value_type;
  {
    XmlContainerTraits<C>::emplace(c)
  } -> std::same_as<typename XmlContainerTraits<C>::value_type&>;
  XmlContainerTraits<C>::pop(c);
};

/// @brief Satisfied when C has a fixed capacity and indexed access via
/// XmlContainerTraits (e.g., std::array).
template <typename C>
concept XmlFixedContainer = requires(C& c, size_t i) {
  typename XmlContainerTraits<C>::value_type;
  XmlContainerTraits<C>::capacity;
  {
    XmlContainerTraits<C>::at(c, i)
  } -> std::same_as<typename XmlContainerTraits<C>::value_type&>;
};

// Compile-time field introspection
namespace detail {

/// @brief Number of XML field descriptors declared for T.
template <typename T>
inline constexpr size_t field_count =
    std::tuple_size_v<decltype(XmlMetadata<T>::fields)>;

/// @brief Index sequence over T's field descriptors, in declaration order.
template <typename T>
inline constexpr auto field_seq = std::make_index_sequence<field_count<T>>{};

/// @brief Builds a std::array<Elem, field_count<T>> by applying `proj` to each
/// field descriptor of T in declaration order. Elem is given explicitly so the
/// result type is well-defined even for a zero-field type.
template <typename Elem, typename T, typename Proj>
constexpr auto map_fields(Proj proj) noexcept {
  return [&]<size_t... I>(std::index_sequence<I...>) {
    return std::array<Elem, field_count<T>>{
        {proj(std::get<I>(XmlMetadata<T>::fields))...}};
  }(field_seq<T>);
}

template <typename T>
constexpr auto make_field_hashes() noexcept {
  return map_fields<FieldHash, T>([](const auto& f) { return f.hash; });
}

template <typename T>
constexpr auto make_field_names() noexcept {
  return map_fields<std::string_view, T>(
      [](const auto& f) { return f.xml_name; });
}

template <typename T>
constexpr auto make_field_kinds() noexcept {
  return map_fields<FieldKind, T>([](const auto& f) { return f.kind; });
}

template <typename T>
inline size_t find_field_index(FieldHash hash) noexcept {
  constexpr auto hashes = make_field_hashes<T>();
  const auto it = std::ranges::find(hashes, hash);
  return static_cast<size_t>(std::distance(hashes.begin(), it));
}

/// @brief Bit i is set when field i of XmlMetadata<T> is marked required.
/// Drives the parsed-vs-required check in Parser::pull(). A zero mask (the
/// default, since fields are optional unless opted in) lets pull() skip all
/// presence tracking, so types without required fields pay nothing.
template <typename T>
constexpr uint64_t make_required_mask() noexcept {
  static_assert(field_count<T> <= 64,
                "TurboXML tracks required fields in a 64-bit mask; a type may "
                "declare at most 64 fields");
  return [&]<size_t... I>(std::index_sequence<I...>) {
    uint64_t mask = 0;
    ((mask |= std::get<I>(XmlMetadata<T>::fields).required ? (uint64_t{1} << I)
                                                           : uint64_t{0}),
     ...);
    return mask;
  }(field_seq<T>);
}

/// @brief True if any field of T is an attribute field.
template <typename T>
constexpr bool has_attr_fields() noexcept {
  constexpr auto kinds = make_field_kinds<T>();
  return std::ranges::any_of(kinds,
                             [](FieldKind k) { return k == FieldKind::Attr; });
}

/// @brief True if any field of T is a child element or container field.
template <typename T>
constexpr bool has_element_fields() noexcept {
  constexpr auto kinds = make_field_kinds<T>();
  return std::ranges::any_of(kinds,
                             [](FieldKind k) { return k != FieldKind::Attr; });
}

/// @brief Index of the first non-attribute field, or 0 if none.
template <typename T>
constexpr auto first_elem_index() noexcept -> size_t {
  constexpr auto kinds = make_field_kinds<T>();
  for (size_t i = 0; i < kinds.size(); ++i) {
    if (kinds[i] != FieldKind::Attr) {
      return i;
    }
  }
  return 0;
}

/// @brief Cyclic successor table over non-attribute fields: entry i holds
/// the index of the next element/container field after i (itself if it is
/// the only one). Drives the document-order hint in Parser::pull().
template <typename T>
constexpr auto make_next_elem_table() noexcept {
  constexpr auto kinds = make_field_kinds<T>();
  constexpr size_t n = kinds.size();
  std::array<size_t, (n != 0 ? n : 1)> next{};
  for (size_t i = 0; i < n; ++i) {
    next[i] = i;
    for (size_t step = 1; step <= n; ++step) {
      const size_t j = (i + step) % n;
      if (kinds[j] != FieldKind::Attr) {
        next[i] = j;
        break;
      }
    }
  }
  return next;
}

// Compile-time check that no two field names in a type produce the same
// FNV-1a hash. Fires via static_assert in pull() for every deserialized type.
template <size_t N>
constexpr bool all_unique(const std::array<FieldHash, N>& arr) noexcept {
  for (size_t i = 0; i < N; ++i) {
    for (size_t j = i + 1; j < N; ++j) {
      if (arr[i] == arr[j]) {
        return false;
      }
    }
  }
  return true;
}

// ---- Text normalization (owning std::string fields, opt-in) ----

/// @brief How a run of character data is normalized when appended to an owning
/// std::string field. Reference expansion and line-ending normalization only
/// run on this path; std::string_view fields stay raw and zero-copy.
enum class NormMode : uint8_t {
  Text,  ///< Element text: expand references, normalize CR/CRLF -> LF.
  Attr,  ///< Attribute value: as Text, plus literal whitespace -> single space.
  CData,  ///< CDATA content: normalize line endings only; '&' stays literal.
};

/// @brief Appends the UTF-8 encoding of code point cp to out. Returns false if
/// cp is not a valid XML character (out of range, a surrogate, or a forbidden
/// control character), per the Char production [2].
inline bool encode_utf8(std::string& out, uint32_t cp) {
  const bool valid =
      cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) ||
      (cp >= 0xE000 && cp <= 0xFFFD) || (cp >= 0x10000 && cp <= 0x10FFFF);
  if (!valid) {
    return false;
  }
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return true;
}

/// @brief Expands the reference at s[i] (s[i] == '&'), appending the result to
/// out and advancing i past the terminating ';'. Handles the five predefined
/// entities and decimal/hex character references; any other name is an
/// UndefinedEntity (no DTD is processed).
inline ErrorCode expand_reference(std::string& out, std::string_view s,
                                  size_t& i) {
  const size_t semi = s.find(';', i + 1);
  if (semi == std::string_view::npos) {
    return ErrorCode::InvalidCharRef;  // bare '&' / unterminated reference
  }
  const std::string_view body = s.substr(i + 1, semi - (i + 1));
  if (body.empty()) {
    return ErrorCode::UndefinedEntity;
  }
  if (body.front() == '#') {
    std::string_view digits = body.substr(1);
    int base = 10;
    if (!digits.empty() && (digits.front() == 'x' || digits.front() == 'X')) {
      base = 16;
      digits.remove_prefix(1);
    }
    uint32_t cp = 0;
    const char* const last = digits.data() + digits.size();
    const auto r = std::from_chars(digits.data(), last, cp, base);
    if (digits.empty() || r.ec != std::errc() || r.ptr != last ||
        !encode_utf8(out, cp)) {
      return ErrorCode::InvalidCharRef;
    }
  } else if (body == "amp") {
    out.push_back('&');
  } else if (body == "lt") {
    out.push_back('<');
  } else if (body == "gt") {
    out.push_back('>');
  } else if (body == "apos") {
    out.push_back('\'');
  } else if (body == "quot") {
    out.push_back('"');
  } else {
    return ErrorCode::UndefinedEntity;
  }
  i = semi + 1;
  return ErrorCode::None;
}

/// @brief Bytes that interrupt a bulk copy, indexed by NormMode. A run of
/// ordinary bytes (table entry false) is copied verbatim with one memcpy; only
/// the marked bytes need per-character handling: '&' (reference expansion, not
/// in CData), '\r' (EOL folding, all modes), and literal '\n'/'\t' (folded to a
/// space, Attr only).
inline const std::array<bool, 256>& norm_special_table(NormMode mode) noexcept {
  constexpr auto make = [](bool amp, bool ws) {
    std::array<bool, 256> t{};
    t[static_cast<unsigned char>('\r')] = true;
    if (amp) {
      t[static_cast<unsigned char>('&')] = true;
    }
    if (ws) {
      t[static_cast<unsigned char>('\n')] = true;
      t[static_cast<unsigned char>('\t')] = true;
    }
    return t;
  };
  static constexpr std::array<bool, 256> kText = make(true, false);
  static constexpr std::array<bool, 256> kAttr = make(true, true);
  static constexpr std::array<bool, 256> kCData = make(false, false);
  switch (mode) {
    case NormMode::Attr:
      return kAttr;
    case NormMode::CData:
      return kCData;
    case NormMode::Text:
      break;
  }
  return kText;
}

/// @brief Appends `raw` to `out` under the given NormMode. Returns the first
/// error encountered (None on success). CData never errors.
///
/// Ordinary bytes (the overwhelming majority of typical content) are copied in
/// bulk runs via std::string::append; only reference starts and line-ending /
/// whitespace bytes are handled one at a time.
inline ErrorCode append_normalized(std::string& out, std::string_view raw,
                                   NormMode mode) {
  const std::array<bool, 256>& special = norm_special_table(mode);
  const char* const base = raw.data();
  const size_t n = raw.size();
  out.reserve(out.size() + n);
  size_t i = 0;
  while (i < n) {
    // Copy the run of ordinary bytes up to the next byte needing attention.
    size_t j = i;
    while (j < n && !special[static_cast<unsigned char>(base[j])]) {
      ++j;
    }
    if (j > i) {
      out.append(base + i, j - i);
      i = j;
      if (i == n) {
        break;
      }
    }
    const char c = base[i];
    if (c == '&') {  // marked special only for Text/Attr
      if (const ErrorCode ec = expand_reference(out, raw, i);
          ec != ErrorCode::None) {
        return ec;
      }
      continue;  // expand_reference advanced i past ';'
    }
    if (c == '\r') {  // EOL normalization: CR and CRLF collapse to one LF.
      out.push_back(mode == NormMode::Attr ? ' ' : '\n');
      ++i;
      if (i < n && base[i] == '\n') {
        ++i;
      }
      continue;
    }
    // Reached only in Attr mode, for a literal '\n' or '\t'.
    out.push_back(' ');  // literal whitespace -> space (XML 3.3.3 CDATA-type)
    ++i;
  }
  return ErrorCode::None;
}

}  // namespace detail

/// @brief Pull parser for XML deserialization.
///
/// Parses a string_view in a single forward pass with no heap allocation
/// beyond the attribute vector. Not copyable. Use deserialize() to drive it.
/// @note String-view lifetime
/// For XmlMetadata std::string_view fields, this is a zero-copy parser:
/// every std::string_view it produces (element text, attribute values,
/// deserialized string_view fields) aliases bytes in the source buffer. The
/// source must outlive both the Parser and any object populated from it.
/// Deserialize into std::string fields when you need owned copies that outlive
/// the buffer.
template <bool Normalize = false>
class BasicParser {
 public:
  /// @brief Maximum element nesting depth (descent and skip) before
  /// ErrorCode::DepthExceeded. Guards against stack exhaustion on hostile
  /// input.
  static constexpr int kMaxDepth = 256;

  /// @brief Maximum number of attributes accepted on a single start-tag before
  /// ErrorCode::TooManyAttributes. Bounds attribute-storage amplification.
  static constexpr size_t kMaxAttributesPerElement = 1U << 16;

  /// @brief When true (BasicParser<true>), owning std::string fields receive
  /// normalized, reference-expanded text: the five predefined entities and
  /// numeric character references are expanded, CR/CRLF are normalized to LF,
  /// and attribute whitespace is normalized to spaces. std::string_view fields
  /// are always raw zero-copy regardless (a view cannot hold transformed
  /// bytes). The default (BasicParser<false>, aliased as Parser) emits raw,
  /// byte-for-byte output and compiles the normalization paths away entirely.
  static constexpr bool kNormalize = Normalize;

  /// @brief Constructs a parser over src. src must outlive the Parser.
  explicit BasicParser(std::string_view src) noexcept
      : src_(src), cur_(src.data()), end_(src.data() + src.size()) {}

  BasicParser(const BasicParser&) = delete;
  auto operator=(const BasicParser&) -> BasicParser& = delete;
  BasicParser(BasicParser&&) noexcept = default;
  auto operator=(BasicParser&&) noexcept -> BasicParser& = default;

  template <bool Nz, typename T>
  friend auto deserialize(BasicParser<Nz>& parser, std::string_view root_name,
                          T& object) -> bool;

  /// @brief Resets the parser to the beginning of the source string.
  void reset() {
    cur_ = src_.data();
    end_ = src_.data() + src_.size();
    has_peek_ = false;
    attributes_.clear();
    error_code_ = ErrorCode::None;
    last_self_closing_ = false;
  }

  /// @brief Reason the most recent parse failed, or None if it succeeded.
  [[nodiscard]] auto error_code() const noexcept -> ErrorCode {
    return error_code_;
  }

 private:
  static constexpr auto kSpaceTable = [] {
    std::array<bool, 256> t{false};
    t[' '] = t['\t'] = t['\n'] = t['\r'] = true;
    return t;
  }();

  static constexpr auto kNameStartTable = [] {
    std::array<bool, 256> t{false};
    for (unsigned i = 'a'; i <= 'z'; ++i) {
      t[i] = true;
    }
    for (unsigned i = 'A'; i <= 'Z'; ++i) {
      t[i] = true;
    }
    for (unsigned i = 128; i < 256; ++i) {
      t[i] = true;
    }
    t['_'] = t[':'] = true;
    return t;
  }();

  static constexpr auto kNameCharTable = [] {
    std::array<bool, 256> t = kNameStartTable;
    for (unsigned i = '0'; i <= '9'; ++i) {
      t[i] = true;
    }
    t['-'] = t['.'] = true;
    return t;
  }();

  [[nodiscard]] auto next() -> const Token*;
  [[nodiscard]] auto peek() -> const Token*;

  template <typename T>
  [[nodiscard]] auto value(std::string_view expected_name, T& out) -> bool;

  template <typename T>
  [[nodiscard]] auto attr(FieldHash hash, T& out, size_t& pos) -> bool;

  [[nodiscard]] auto begin_element(std::string_view expected_name) -> bool;
  [[nodiscard]] auto end_element(std::string_view expected_name) -> bool;

  template <typename T>
  static auto parse_numeric(std::string_view text, T& out) noexcept -> bool;

  [[nodiscard]] auto at_end() const noexcept -> bool { return cur_ >= end_; }

  [[nodiscard]] auto peek_char() const noexcept -> char {
    return at_end() ? '\0' : *cur_;
  }

  void skip_whitespace() noexcept {
    while (!at_end() && is_space(*cur_)) [[likely]] {
      ++cur_;
    }
  }

  [[nodiscard]] static constexpr auto is_space(char c) noexcept -> bool {
    return kSpaceTable[static_cast<unsigned char>(c)];
  }
  [[nodiscard]] static constexpr auto is_name_start(char c) noexcept -> bool {
    return kNameStartTable[static_cast<unsigned char>(c)];
  }
  [[nodiscard]] static constexpr auto is_name_char(char c) noexcept -> bool {
    return kNameCharTable[static_cast<unsigned char>(c)];
  }

  void parse_name(std::string_view& prefix, std::string_view& local,
                  FieldHash& local_hash) noexcept {
    prefix = {};
    if (at_end() || !is_name_start(*cur_)) [[unlikely]] {
      local = {};
      local_hash = detail::kFnvOffset;
      return;
    }
    const char* start = cur_;
    const char* local_start = start;
    FieldHash hash = detail::kFnvOffset;

    while (!at_end() && is_name_char(*cur_)) {
      if (*cur_ == ':') {
        prefix = {start, static_cast<size_t>(cur_ - start)};
        local_start = cur_ + 1;
        hash = detail::kFnvOffset;  // reset; hash only the local part
      } else {
        hash = detail::fnv1a_step(hash, static_cast<unsigned char>(*cur_));
      }
      ++cur_;
    }
    local = {local_start, static_cast<size_t>(cur_ - local_start)};
    local_hash = hash;
  }

  [[nodiscard]] auto expect(char c) noexcept -> bool {
    if (at_end() || *cur_ != c) {
      return false;
    }
    ++cur_;
    return true;
  }

  [[nodiscard]] auto starts_with(std::string_view s) const noexcept -> bool {
    return std::string_view{cur_, static_cast<size_t>(end_ - cur_)}.starts_with(
        s);
  }

  // Records the first error (later cascading failures don't overwrite it) and
  // flags the parser stopped. Returns false so callers can `return fail(code)`.
  auto fail(ErrorCode code) noexcept -> bool {
    if (!error()) [[likely]] {
      error_code_ = code;
    }
    return false;
  }

  // Assigns a single text run to a string-like field. When normalization is on
  // and the target owns its bytes (std::string), expand references and
  // normalize line endings; otherwise assign the raw view (zero-copy for
  // std::string_view). Compiled away to a plain assignment for BasicParser<>.
  template <typename T>
  auto assign_value(T& out, std::string_view text) -> bool {
    if constexpr (kNormalize && std::same_as<T, std::string>) {
      out.clear();
      const ErrorCode ec =
          detail::append_normalized(out, text, detail::NormMode::Text);
      return ec == ErrorCode::None ? true : fail(ec);
    } else {
      out = text;
      return true;
    }
  }

  auto make_error(Token& token, ErrorCode code) noexcept -> bool {
    token.type = TokenType::Error;
    return fail(code);
  }

  /// @brief Whether the parser has encountered an error.
  /// @return true if `error_code_` is not `ErrorCode::None`.
  auto error() const noexcept -> bool { return error_code_ != ErrorCode::None; }

  // Shared logic: record self-closing state after consuming an ElementOpen.
  void update_self_closing() noexcept {
    if (current_token_.type == TokenType::ElementOpen) {
      last_self_closing_ = current_token_.self_closing;
    }
  }

  void consume() { std::ignore = next(); }

  // Branch-free consume when we know a peek() just succeeded.
  void consume_peeked() noexcept {
    has_peek_ = false;
    update_self_closing();
  }

  // Consume the peeked element and skip its entire subtree.
  void skip_current() {
    consume_peeked();
    skip_element();
  }

  auto parse_markup(Token& token) -> bool;
  auto parse_element_open(Token& token) -> bool;
  auto parse_element_close(Token& token) -> bool;
  auto next_from_source(Token& token) -> bool;
  void skip_element();

  // First occurrence of `c` in [from, end_), or end_ if absent — std::find
  // semantics over the parse range.
  [[nodiscard]] auto find_byte(const char* from, char c) const noexcept -> const
      char* {
    const char* hit = static_cast<const char*>(
        std::memchr(from, c, static_cast<size_t>(end_ - from)));
    return hit != nullptr ? hit : end_;
  }

  // Scan forward until `delim` is found. Sets token.data to the content
  // before the delimiter and advances past it.
  auto scan_to_delimiter(Token& token, std::string_view delim, ErrorCode ec)
      -> bool {
    const char* start = cur_;
    const char first = delim[0];
    while (cur_ < end_) {
      cur_ = find_byte(cur_, first);
      if (cur_ == end_) {
        break;
      }
      if (starts_with(delim)) {
        token.data = {start, static_cast<size_t>(cur_ - start)};
        cur_ += delim.size();
        return true;
      }
      ++cur_;
    }
    return make_error(token, ec);
  }

  // Advances past the next occurrence of delim; cur_ = end_ if absent.
  void skip_past(std::string_view delim) noexcept {
    const char first = delim[0];
    while (cur_ < end_) {
      cur_ = find_byte(cur_, first);
      if (cur_ == end_) {
        break;
      }
      if (starts_with(delim)) {
        cur_ += delim.size();
        return;
      }
      ++cur_;
    }
    cur_ = end_;
  }

  // Comment ::= '<!--' ((Char - '-') | ('-' (Char - '-')))* '-->'
  // The well-formedness rule forbids "--" anywhere in the content, so the first
  // "--" we encounter must be the start of the "-->" terminator; any other "--"
  // is a fatal error. This is a single std::find-driven pass (no slower than
  // the generic scan it replaces) that additionally enforces the WFC.
  auto parse_comment(Token& token) -> bool {
    token.type = TokenType::Comment;
    const char* start = cur_;
    while (cur_ < end_) {
      const char* hit = find_byte(cur_, '-');
      if (hit == end_ || hit + 1 >= end_) {
        break;  // no '-' (or a lone trailing '-'): unterminated
      }
      if (hit[1] != '-') {
        cur_ = hit + 1;  // isolated '-', keep scanning
        continue;
      }
      if (hit + 2 >= end_) {
        break;  // "--" at end of input: unterminated
      }
      if (hit[2] == '>') {
        token.data = {start, static_cast<size_t>(hit - start)};
        cur_ = hit + 3;
        return true;
      }
      return make_error(token, ErrorCode::MalformedComment);  // interior "--"
    }
    cur_ = end_;
    return make_error(token, ErrorCode::UnterminatedComment);
  }

  auto parse_cdata(Token& token) -> bool {
    token.type = TokenType::CData;
    return scan_to_delimiter(token, "]]>", ErrorCode::UnterminatedCData);
  }

  auto parse_pi(Token& token) -> bool {
    parse_name(token.prefix, token.name, token.name_hash);
    if (token.name.empty()) {
      return make_error(token, ErrorCode::ExpectedPiTarget);
    }
    // Production [17]: PITarget excludes every case variant of "xml". Exact
    // lowercase "xml" names the XML declaration; "XML", "Xml", ... are reserved
    // and ill-formed as PI targets. (Cheap: runs only on processing
    // instructions, never on element/attribute content.)
    if (token.name == "xml") {
      token.type = TokenType::XmlDeclaration;
    } else {
      if (token.prefix.empty() && is_reserved_xml_target(token.name)) {
        return make_error(token, ErrorCode::ReservedPiTarget);
      }
      token.type = TokenType::ProcessingInstruction;
    }
    skip_whitespace();
    return scan_to_delimiter(token, "?>", ErrorCode::UnterminatedPi);
  }

  // True for a case-insensitive but not-exactly-lowercase match of "xml".
  [[nodiscard]] static auto is_reserved_xml_target(
      std::string_view name) noexcept -> bool {
    return name.size() == 3 && (name[0] | 0x20) == 'x' &&
           (name[1] | 0x20) == 'm' && (name[2] | 0x20) == 'l';
  }

  // Fast-path: match and consume "<name>" or "<name/>" without tokenisation.
  // Only succeeds for simple tags (no attributes, no namespace prefix).
  // Returns false to fall through to normal tokenisation path.
  [[nodiscard]] auto try_begin_element(std::string_view name) noexcept -> bool {
    const size_t name_len = name.size();
    const size_t avail = static_cast<size_t>(end_ - cur_);
    if (avail < name_len + 2 || cur_[0] != '<') {
      return false;
    }
    if (std::memcmp(cur_ + 1, name.data(), name_len) != 0) {
      return false;
    }
    const char after = cur_[1 + name_len];
    if (after == '>') {
      cur_ += name_len + 2;
      last_self_closing_ = false;
      has_peek_ = false;
      attributes_.clear();
      return true;
    }
    if (after == '/' && avail > name_len + 2 && cur_[name_len + 2] == '>') {
      cur_ += name_len + 3;
      last_self_closing_ = true;
      has_peek_ = false;
      attributes_.clear();
      return true;
    }
    return false;
  }

  // Post-consume dispatch: opening tag already consumed, route to the correct
  // read_element. arr_fill tracks fill position for fixed containers.
  template <typename T, size_t I>
  static bool read_field(BasicParser& p, T& obj, uint16_t depth,
                         std::span<size_t> arr_fill, uint64_t& parsed) {
    // Reaching here means field I's element matched, so it is present. Record
    // it for the required-field check; gated so types with no required field
    // never touch parsed (the arg dead-codes away). Failure paths below still
    // return false and the mask is then irrelevant.
    if constexpr (detail::make_required_mask<T>() != 0) {
      parsed |= (uint64_t{1} << I);
    }
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      p.skip_element();
      return true;
    } else if constexpr (f.kind == FieldKind::Container) {
      using M = std::decay_t<decltype(obj.*(f.member))>;
      if constexpr (XmlFixedContainer<M>) {
        using Traits = XmlContainerTraits<M>;
        if (arr_fill[I] < Traits::capacity) {
          if (!p.read_element(f.xml_name,
                              Traits::at(obj.*(f.member), arr_fill[I]),
                              depth + 1)) {
            return false;
          }
          ++arr_fill[I];
        } else {
          p.skip_element();
        }
      } else {
        static_assert(
            XmlDynContainer<M>,
            "container member requires XmlContainerTraits specialization");
        using Traits = XmlContainerTraits<M>;
        auto& container = obj.*(f.member);
        auto& elem = Traits::emplace(container);
        if (!p.read_element(f.xml_name, elem, depth + 1)) {
          Traits::pop(container);
          return false;
        }
      }
      return true;
    } else {
      return p.read_element(f.xml_name, obj.*(f.member), depth + 1);
    }
  }

  template <typename T, size_t I>
  static void apply_attr(BasicParser& p, T& obj, size_t& pos,
                         uint64_t& parsed) {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      if (p.attr(f.hash, obj.*(f.member), pos)) {
        parsed |= (uint64_t{1} << I);
      }
    }
  }

  template <typename T, size_t... I>
  static constexpr auto build_elem_dispatch(
      std::index_sequence<I...>) noexcept {
    using Handler =
        bool (*)(BasicParser&, T&, uint16_t, std::span<size_t>, uint64_t&);
    return std::array<Handler, sizeof...(I)>{&read_field<T, I>...};
  }

  template <typename T, size_t... I>
  static void dispatch_attrs(BasicParser& p, T& obj, uint64_t& parsed,
                             std::index_sequence<I...>) {
    size_t pos = 0;  // document-order cursor over attributes_
    (apply_attr<T, I>(p, obj, pos, parsed), ...);
  }

  template <typename T>
  auto pull(T& object, uint16_t depth) -> bool;

  // Opening tag already consumed by caller.
  template <typename T>
  auto read_element(std::string_view expected_name, T& out, uint16_t depth)
      -> bool;

  Token current_token_;
  std::vector<Attribute> attributes_;
  std::string_view src_;
  const char* cur_;
  const char* end_;
  ErrorCode error_code_{ErrorCode::None};
  bool last_self_closing_{};
  bool has_peek_{false};
};

/// @brief Default parser: raw, zero-copy output (no reference expansion or
/// normalization). This is the common case.
using Parser = BasicParser<false>;

/// @brief Normalizing parser: owning std::string fields receive
/// reference-expanded, line-ending- and attribute-normalized text. See
/// BasicParser::kNormalize.
using NormalizingParser = BasicParser<true>;

/// @brief Deserializes the root element from parser into object.
/// @tparam T        XmlObject type with an XmlMetadata specialization.
/// @param parser    Parser positioned at the start of the XML input.
/// @param root_name Expected root element name.
/// @param object    Output object to populate.
/// @return True on success, false on any parse or structure error.
template <bool Normalize, typename T>
auto deserialize(BasicParser<Normalize>& parser, std::string_view root_name,
                 T& object) -> bool {
  if (!parser.begin_element(root_name)) [[unlikely]] {
    // begin_element() may have hit a tokenizer error (code already set); only
    // attribute a plain "root missing/mismatched" when nothing else did.
    return parser.fail(ErrorCode::RootElementNotFound);
  }
  const bool is_self_closing = parser.last_self_closing_;
  if (!parser.pull(object, 1)) [[unlikely]] {
    return false;
  }
  if (!is_self_closing && !parser.end_element(root_name)) [[unlikely]] {
    return false;
  }
  return true;
}

// Parser method implementations
template <bool Normalize>
inline auto BasicParser<Normalize>::peek() -> const Token* {
  if (!has_peek_) {
    if (!next_from_source(current_token_)) {
      return nullptr;
    }
    has_peek_ = true;
  }
  if (current_token_.type == TokenType::Error) {
    return nullptr;
  }
  return &current_token_;
}

template <bool Normalize>
inline auto BasicParser<Normalize>::next() -> const Token* {
  if (!has_peek_ && !next_from_source(current_token_)) {
    return nullptr;
  }
  has_peek_ = false;
  update_self_closing();
  if (current_token_.type == TokenType::Error) {
    return nullptr;
  }
  return &current_token_;
}

template <bool Normalize>
inline auto BasicParser<Normalize>::next_from_source(Token& token) -> bool {
  if (error() || at_end()) {
    return false;
  }
  if (*cur_ == '<') {
    ++cur_;
    return parse_markup(token);
  }
  const char* start = cur_;
  cur_ = find_byte(cur_, '<');
  token.type = TokenType::Text;
  token.data = {start, static_cast<size_t>(cur_ - start)};
  return true;
}

template <bool Normalize>
inline auto BasicParser<Normalize>::parse_markup(Token& token) -> bool {
  if (at_end()) {
    return make_error(token, ErrorCode::UnexpectedEndAfterLt);
  }
  const char c = *cur_;
  if (c == '/') {
    ++cur_;
    return parse_element_close(token);
  }
  if (c == '!') {
    ++cur_;
    if (starts_with("--")) {
      cur_ += 2;
      return parse_comment(token);
    }
    if (starts_with("[CDATA[")) {
      cur_ += 7;
      return parse_cdata(token);
    }
    const char* gt = find_byte(cur_, '>');
    cur_ = gt == end_ ? end_ : gt + 1;
    return next_from_source(token);
  }
  if (c == '?') {
    ++cur_;
    return parse_pi(token);
  }
  if (is_name_start(c)) {
    return parse_element_open(token);
  }
  return make_error(token, ErrorCode::UnexpectedCharAfterLt);
}

template <bool Normalize>
inline auto BasicParser<Normalize>::parse_element_open(Token& token) -> bool {
  token.type = TokenType::ElementOpen;
  token.self_closing = false;
  parse_name(token.prefix, token.name, token.name_hash);
  if (token.name.empty()) {
    return make_error(token, ErrorCode::ExpectedElementName);
  }

  attributes_.clear();
  while (true) {
    skip_whitespace();
    if (at_end()) {
      return make_error(token, ErrorCode::UnclosedTag);
    }
    const char c = *cur_;
    if (c == '>') {
      ++cur_;
      return true;
    }
    if (c == '/' && cur_ + 1 < end_ && *(cur_ + 1) == '>') {
      cur_ += 2;
      token.self_closing = true;
      return true;
    }
    if (!is_name_start(c)) {
      return make_error(token, ErrorCode::ExpectedAttributeName);
    }
    if (attributes_.size() >= kMaxAttributesPerElement) [[unlikely]] {
      return make_error(token, ErrorCode::TooManyAttributes);
    }

    Attribute& a = attributes_.emplace_back();
    parse_name(a.prefix, a.name, a.name_hash);
    skip_whitespace();
    if (!expect('=')) {
      return make_error(token, ErrorCode::ExpectedEquals);
    }
    skip_whitespace();
    const char quote = peek_char();
    if (quote != '"' && quote != '\'') {
      return make_error(token, ErrorCode::ExpectedQuotedValue);
    }
    ++cur_;
    const char* val_start = cur_;
    const char* val_end = find_byte(cur_, quote);
    if (val_end == end_) {
      return make_error(token, ErrorCode::UnterminatedAttributeValue);
    }
    a.value = {val_start, static_cast<size_t>(val_end - val_start)};
    cur_ = val_end + 1;
  }
}

template <bool Normalize>
inline auto BasicParser<Normalize>::parse_element_close(Token& token) -> bool {
  token.type = TokenType::ElementClose;
  FieldHash name_hash;
  parse_name(token.prefix, token.name, name_hash);
  if (token.name.empty()) {
    return make_error(token, ErrorCode::ExpectedNameInCloseTag);
  }
  skip_whitespace();
  if (!expect('>')) {
    return make_error(token, ErrorCode::ExpectedCloseTagEnd);
  }
  return true;
}

template <bool Normalize>
template <typename T>
inline auto BasicParser<Normalize>::parse_numeric(std::string_view text,
                                                  T& out) noexcept -> bool {
  if constexpr (std::same_as<T, bool>) {
    // XML Schema boolean lexical space; std::from_chars has no bool overload.
    if (text == "true" || text == "1") {
      out = true;
      return true;
    }
    if (text == "false" || text == "0") {
      out = false;
      return true;
    }
    return false;
  } else {
    if (text.empty()) {
      return false;
    }
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
  }
}

template <bool Normalize>
template <typename T>
inline auto BasicParser<Normalize>::value(std::string_view expected_name,
                                          T& out) -> bool {
  if constexpr (XmlStringLike<T>) {
    if constexpr (kNormalize && std::same_as<T, std::string>) {
      // Owning target + normalization: accumulate every character-data run
      // (Text + CDATA) between the tags, expanding references in Text and
      // copying CDATA literally (EOL-normalized, never reference-expanded).
      out.clear();
      while (const Token* token = next()) {
        if (token->type == TokenType::Text) {
          if (const ErrorCode ec = detail::append_normalized(
                  out, token->data, detail::NormMode::Text);
              ec != ErrorCode::None) {
            return fail(ec);
          }
        } else if (token->type == TokenType::CData) {
          detail::append_normalized(out, token->data, detail::NormMode::CData);
        } else if (token->type == TokenType::ElementClose) {
          return token->name == expected_name
                     ? true
                     : fail(ErrorCode::ElementMismatch);
        }
      }
      return fail(ErrorCode::UnexpectedEof);
    } else {
      while (const Token* token = next()) {
        if (token->type == TokenType::Text) {
          out = token->data;
        }
        if (token->type == TokenType::ElementClose) {
          return token->name == expected_name
                     ? true
                     : fail(ErrorCode::ElementMismatch);
        }
      }
      return fail(ErrorCode::UnexpectedEof);  // next() set a code, or true EOF
    }
  } else {
    std::string_view text;
    if (!value(expected_name, text)) {
      return false;  // string pass already recorded the cause
    }
    return parse_numeric(text, out) ? true
                                    : fail(ErrorCode::InvalidNumericValue);
  }
}

// Document-order fast path: attribute fields are typically declared in the
// same order the attributes appear, so try the cursor position first and
// fall back to a full first-match scan on miss.
template <bool Normalize>
template <typename T>
inline auto BasicParser<Normalize>::attr(const FieldHash hash, T& out,
                                         size_t& pos) -> bool {
  size_t idx;
  if (pos < attributes_.size() && attributes_[pos].name_hash == hash) {
    idx = pos++;
  } else {
    const auto it = std::ranges::find_if(
        attributes_,
        [hash](const Attribute& at) { return at.name_hash == hash; });
    if (it == attributes_.end()) {
      return false;
    }
    idx = static_cast<size_t>(it - attributes_.begin());
    pos = idx + 1;
  }
  const Attribute& a = attributes_[idx];
  if constexpr (XmlStringLike<T>) {
    if constexpr (kNormalize && std::same_as<T, std::string>) {
      out.clear();
      const ErrorCode ec =
          detail::append_normalized(out, a.value, detail::NormMode::Attr);
      // On a bad reference, fail() records the code; the attribute reports
      // "not matched" and pull() converts the recorded error into a hard fail
      // right after attribute dispatch.
      return ec == ErrorCode::None ? true : fail(ec);
    } else {
      out = a.value;
      return true;
    }
  } else {
    return parse_numeric(a.value, out);
  }
}

template <bool Normalize>
inline auto BasicParser<Normalize>::begin_element(
    std::string_view expected_name) -> bool {
  while (const Token* peeked = peek()) {
    if (peeked->type == TokenType::ElementOpen) {
      if (peeked->name == expected_name) {
        consume();
        return true;
      }
      return false;
    }
    if (peeked->type == TokenType::ElementClose) {
      return false;
    }
    consume();
  }
  return false;
}

template <bool Normalize>
inline auto BasicParser<Normalize>::end_element(std::string_view expected_name)
    -> bool {
  if (!has_peek_) {
    skip_whitespace();
    const auto name_len = expected_name.size();
    const size_t required = name_len + 3;  // "</" + name + ">"
    const size_t remaining = static_cast<size_t>(end_ - cur_);
    if (remaining >= required) {
      const char* p = cur_;
      if (p[0] == '<' && p[1] == '/' &&
          std::memcmp(p + 2, expected_name.data(), name_len) == 0) {
        p += 2 + name_len;
        while (p < end_ && is_space(*p)) {
          ++p;
        }
        if (p < end_ && *p == '>') {
          cur_ = p + 1;
          return true;
        }
      }
    }
  }

  while (const Token* peeked = peek()) {
    switch (peeked->type) {
      case TokenType::ElementClose:
        if (peeked->name != expected_name) {
          return fail(ErrorCode::ElementMismatch);
        }
        consume();
        return true;
      case TokenType::ElementOpen:
        return fail(ErrorCode::ElementMismatch);
      default:
        consume();
        break;
    }
  }
  return fail(ErrorCode::UnexpectedEof);  // peek() set a code, or true EOF
}

// Skips the remainder of the current element without tokenising: a raw scan
// tracking nesting depth, quoted attribute values, comments, CDATA, and PIs.
// Precondition: the opening tag has been consumed and no token is peeked.
// On malformed or truncated content, leaves cur_ == end_ so the caller's
// next read fails the parse.
template <bool Normalize>
inline void BasicParser<Normalize>::skip_element() {
  if (last_self_closing_) {
    return;
  }
  size_t depth = 1;
  while (depth > 0) {
    const char* lt = find_byte(cur_, '<');
    if (lt == end_ || lt + 1 >= end_) {
      cur_ = end_;
      return;
    }
    cur_ = lt + 1;
    const char c = *cur_;
    if (c == '/') {
      const char* gt = find_byte(cur_, '>');
      if (gt == end_) {
        cur_ = end_;
        return;
      }
      cur_ = gt + 1;
      --depth;
    } else if (c == '!') {
      ++cur_;
      if (starts_with("--")) {
        cur_ += 2;
        skip_past("-->");
      } else if (starts_with("[CDATA[")) {
        cur_ += 7;
        skip_past("]]>");
      } else {
        skip_past(">");
      }
    } else if (c == '?') {
      ++cur_;
      skip_past("?>");
    } else if (is_name_start(c)) {
      // Open tag: find the closing '>' outside quoted attribute values.
      bool closed = false;
      bool self_closing = false;
      while (cur_ < end_) {
        const char ch = *cur_;
        if (ch == '>') {
          self_closing = cur_[-1] == '/';
          ++cur_;
          closed = true;
          break;
        }
        if (ch == '"' || ch == '\'') {
          const char* q = find_byte(cur_ + 1, ch);
          if (q == end_) {
            cur_ = end_;
            return;
          }
          cur_ = q + 1;
          continue;
        }
        ++cur_;
      }
      if (!closed) {
        return;  // truncated tag; cur_ == end_
      }
      if (!self_closing && ++depth > static_cast<size_t>(kMaxDepth))
          [[unlikely]] {
        fail(ErrorCode::DepthExceeded);
        cur_ = end_;  // force the caller's next read to fail
        return;
      }
    } else {
      cur_ = end_;  // malformed markup after '<'
      return;
    }
  }
}

// Opening tag already consumed by caller (handle_element, try_begin_element,
// or consume_peeked in the N==1 inline path).
template <bool Normalize>
template <typename T>
inline auto BasicParser<Normalize>::read_element(std::string_view expected_name,
                                                 T& out, const uint16_t depth)
    -> bool {
  if (depth > kMaxDepth) {
    return fail(ErrorCode::DepthExceeded);
  }
  const bool is_self_closing = last_self_closing_;

  if constexpr (XmlPrimitive<T>) {
    if (is_self_closing) {
      if constexpr (XmlStringLike<T>) {
        return true;
      }
      return fail(ErrorCode::InvalidNumericValue);  // empty numeric/bool
    }
    // Fast path: locate closing tag directly.
    const char* found = find_byte(cur_, '<');
    if (found != end_ && found + 3 + expected_name.size() <= end_ &&
        found[1] == '/' &&
        std::memcmp(found + 2, expected_name.data(), expected_name.size()) ==
            0 &&
        found[2 + expected_name.size()] == '>') {
      std::string_view text{cur_, static_cast<size_t>(found - cur_)};
      cur_ = found + 3 + expected_name.size();
      has_peek_ = false;
      if constexpr (XmlStringLike<T>) {
        return assign_value(out, text);
      } else {
        return parse_numeric(text, out) ? true
                                        : fail(ErrorCode::InvalidNumericValue);
      }
    }
    return value<T>(expected_name, out);
  } else {
    bool result = false;
    if constexpr (XmlObject<T>) {
      result = pull(out, depth);
    }
    if (!is_self_closing && !end_element(expected_name)) {
      return false;
    }
    return result;
  }
}

template <bool Normalize>
template <typename T>
inline auto BasicParser<Normalize>::pull(T& object, const uint16_t depth)
    -> bool {
  constexpr size_t N = detail::field_count<T>;
  constexpr auto kIdxSeq = detail::field_seq<T>;
  static_assert(detail::all_unique(detail::make_field_hashes<T>()),
                "FNV-1a hash collision among field names in XmlMetadata<T>");

  // Per-field fill counters for fixed containers. Only those entries are
  // touched; the compiler elides the rest.
  std::array<size_t, (N != 0 ? N : 1)> arr_fill{};

  // Presence tracking for required fields. When nothing is required (the
  // default) kHasRequired is false, so 'parsed' is never read: every write to
  // it is dead-store eliminated and check_required() compiles to `return true`.
  constexpr uint64_t kRequiredMask = detail::make_required_mask<T>();
  constexpr bool kHasRequired = kRequiredMask != 0;
  [[maybe_unused]] uint64_t parsed = 0;
  const auto check_required = [&]() -> bool {
    if constexpr (kHasRequired) {
      if ((parsed & kRequiredMask) != kRequiredMask) {
        return fail(ErrorCode::MissingRequiredField);
      }
    }
    return true;
  };

  // Apply attribute fields only when the type actually has some.
  constexpr bool kHasAttrs = detail::has_attr_fields<T>();
  if constexpr (kHasAttrs) {
    dispatch_attrs<T>(*this, object, parsed, kIdxSeq);
    if constexpr (kNormalize) {
      // A string attribute may have carried a malformed/undefined reference;
      // attr() recorded the code but reports "absent". Surface it as a hard
      // failure now (only compiled in for the normalizing parser).
      if (error()) [[unlikely]] {
        return false;
      }
    }
  }

  if (last_self_closing_) {
    return check_required();
  }

  // Document-order hint: index of the field expected next. Schema-ordered
  // XML hits the memcmp fast path below on every element; out-of-order
  // documents miss once, re-sync at the dispatch site, and stay correct.
  constexpr bool kHasElems = detail::has_element_fields<T>();
  static constexpr auto dispatch = build_elem_dispatch<T>(kIdxSeq);
  static constexpr auto kNames = detail::make_field_names<T>();
  static constexpr auto kNextElem = detail::make_next_elem_table<T>();
  [[maybe_unused]] size_t hint = detail::first_elem_index<T>();

  while (true) {
    if (!has_peek_) {
      skip_whitespace();
      if (at_end()) {
        return fail(ErrorCode::UnexpectedEof);
      }
      if (cur_[0] == '<' && cur_ + 1 < end_ && cur_[1] == '/') {
        return check_required();
      }

      // Fast path: match the hinted open tag via memcmp, bypassing full
      // tokenisation (parse_name, hash, peek machinery).
      if constexpr (kHasElems && N == 1) {
        // Single-field types: compile-time tag name and direct call.
        if (try_begin_element(kNames[0])) {
          if (!read_field<T, 0>(*this, object, depth, arr_fill, parsed)) {
            return false;
          }
          continue;
        }
      } else if constexpr (kHasElems) {
        if (try_begin_element(kNames[hint])) {
          if (!dispatch[hint](*this, object, depth, arr_fill, parsed)) {
            return false;
          }
          hint = kNextElem[hint];
          continue;
        }
      }

      cur_ = find_byte(cur_, '<');
    }

    const Token* token = peek();
    if (!token || token->type == TokenType::Error) [[unlikely]] {
      return false;
    }
    if (token->type == TokenType::ElementClose) {
      return check_required();
    }
    if (token->type != TokenType::ElementOpen) {
      consume();
      continue;
    }

    const size_t idx = detail::find_field_index<T>(token->name_hash);
    if (idx >= N) {
      skip_current();
      continue;
    }
    consume_peeked();
    if (!dispatch[idx](*this, object, depth, arr_fill, parsed)) {
      return false;
    }
    if constexpr (kHasElems && N > 1) {
      hint = kNextElem[idx];
    }
  }
}

/// @brief XML serializer. Converts XmlObject instances to XML strings.
/// @tparam kPretty If true, emits indented output; if false, compact.
template <bool kPretty = true>
class Serializer {
 public:
  explicit Serializer(std::string& out) noexcept : out_(out) {}

  /// @brief Serializes obj as an XML element named tag, appending to the output
  /// string.
  template <typename T>
  void write(std::string_view tag, const T& obj) {
    write_element(tag, obj, 0);
  }

 private:
  std::string& out_;

  void do_indent(int depth) {
    if constexpr (kPretty) {
      out_.append(static_cast<size_t>(depth) * 2, ' ');
    }
  }

  void do_newline() {
    if constexpr (kPretty) {
      out_ += '\n';
    }
  }

  // Escapes '&' and '<' always; attribute values additionally escape '"',
  // text content escapes '>'.
  template <bool kAttr>
  static void escape(std::string& out, std::string_view s) {
    for (char c : s) {
      if (c == '&') {
        out += "&amp;";
      } else if (c == '<') {
        out += "&lt;";
      } else if (!kAttr && c == '>') {
        out += "&gt;";
      } else if (kAttr && c == '"') {
        out += "&quot;";
      } else {
        out += c;
      }
    }
  }

  template <typename V>
  static void append_arithmetic(std::string& out, V v) {
    if constexpr (std::same_as<V, bool>) {
      out += v ? "true" : "false";
    } else {
      char buf[32];
      const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
      if (ec == std::errc()) {
        out.append(buf, static_cast<size_t>(ptr - buf));
      }
    }
  }

  template <typename V>
  void write_attr_value(std::string_view name, const V& v) {
    out_ += ' ';
    out_ += name;
    out_ += "=\"";
    if constexpr (XmlStringLike<V>) {
      escape<true>(out_, v);
    } else {
      append_arithmetic(out_, v);
    }
    out_ += '"';
  }

  template <typename V>
  void write_prim_element(std::string_view tag, const V& v, int depth) {
    do_indent(depth);
    out_ += '<';
    out_ += tag;
    out_ += '>';
    if constexpr (XmlStringLike<V>) {
      escape<false>(out_, v);
    } else {
      append_arithmetic(out_, v);
    }
    out_ += "</";
    out_ += tag;
    out_ += '>';
    do_newline();
  }

  template <typename V>
  void write_field_value(std::string_view tag, const V& v, int depth) {
    if constexpr (XmlPrimitive<V>) {
      write_prim_element(tag, v, depth);
    } else {
      static_assert(XmlObject<V>,
                    "field type must be XmlPrimitive or XmlObject");
      write_element(tag, v, depth);
    }
  }

  template <typename T, size_t I>
  void write_attr_if(const T& obj) {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      write_attr_value(f.xml_name, obj.*(f.member));
    }
  }

  template <typename T, size_t... I>
  void write_attrs(const T& obj, std::index_sequence<I...>) {
    (..., write_attr_if<T, I>(obj));
  }

  template <typename T, size_t I>
  void write_child(const T& obj, int depth) {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      // written on opening tag
    } else if constexpr (f.kind == FieldKind::Container) {
      using M = std::decay_t<decltype(obj.*(f.member))>;
      if constexpr (XmlFixedContainer<M>) {
        using Traits = XmlContainerTraits<M>;
        for (size_t i = 0; i < Traits::capacity; ++i) {
          write_field_value(f.xml_name, Traits::at(obj.*(f.member), i), depth);
        }
      } else {
        for (const auto& item : obj.*(f.member)) {
          write_field_value(f.xml_name, item, depth);
        }
      }
    } else {
      write_field_value(f.xml_name, obj.*(f.member), depth);
    }
  }

  template <typename T, size_t... I>
  void write_children(const T& obj, int depth, std::index_sequence<I...>) {
    (..., write_child<T, I>(obj, depth));
  }

  template <typename T>
  void write_element(std::string_view tag, const T& obj, int depth) {
    constexpr size_t N = detail::field_count<T>;
    using Seq = std::make_index_sequence<N>;

    do_indent(depth);
    out_ += '<';
    out_ += tag;
    write_attrs<T>(obj, Seq{});

    if constexpr (detail::has_element_fields<T>()) {
      out_ += '>';
      do_newline();
      write_children<T>(obj, depth + 1, Seq{});
      do_indent(depth);
      out_ += "</";
      out_ += tag;
      out_ += '>';
      do_newline();
    } else {
      out_ += "/>";
      do_newline();
    }
  }
};

/// @brief Serializes object to an XML string under root_name.
/// @tparam kPretty  If true, emits indented output.
/// @tparam T        XmlObject type with an XmlMetadata specialization.
/// @param root_name Root element tag name.
/// @param object    Object to serialize.
/// @return XML string containing the serialized data.
template <bool kPretty = true, typename T>
auto serialize(std::string_view root_name, const T& object) -> std::string {
  std::string out;
  Serializer<kPretty> s{out};
  s.write(root_name, object);
  return out;
}

}  // namespace xml

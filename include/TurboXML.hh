/// @file TurboXML.hh
/// @brief High-performance C++20 XML pull-parsing deserializer and serializer.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
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
  InvalidNumericValue,  ///< Numeric/bool field text failed to parse.
  InvalidEnumValue,     ///< Enum field text matched no XmlEnumTraits token.
  InvalidValue,         ///< Custom-value (e.g. date/time) text failed to parse.
  RootElementNotFound,  ///< Requested root element not present.
  ElementMismatch,      ///< End-tag name does not match its start-tag.
  UnexpectedEof,        ///< Input ended mid-element.
  DepthExceeded,        ///< Nesting deeper than kMaxDepth.
  MissingRequiredField,  ///< A field marked required was absent from the
                         ///< element.
  UndefinedEntity,       ///< Reference to an entity that is not one of the five
                         ///< predefined entities (no DTD is processed).
  InvalidCharRef,        ///< Malformed or out-of-range character reference
                         ///< (e.g. "&#;", "&#xZZ;", or a non-XML code point).
  // Strict well-formedness (StrictParser only)
  CDataEndInContent,   ///< "]]>" appears in character data (Production [14]).
  LtInAttributeValue,  ///< '<' appears in an attribute value (Production [10]).
  DuplicateAttribute,  ///< Two attributes share a name (WFC: Unique Att Spec).
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
enum class FieldKind : uint8_t { Element, Attr, Container, Value, Variant };

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
/// @brief Descriptor for the enclosing element's own text (XSD simpleContent).
template <typename C, typename M>
using ValueField = FieldBase<FieldKind::Value, C, M>;

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

namespace detail {
template <typename T>
struct is_unique_ptr : std::false_type {};
template <typename U, typename D>
struct is_unique_ptr<std::unique_ptr<U, D>> : std::true_type {};

template <typename T>
struct is_optional : std::false_type {};
template <typename U>
struct is_optional<std::optional<U>> : std::true_type {};
}  // namespace detail

/// @brief Satisfied when T is a std::unique_ptr to an XmlObject. Such members
/// hold an optional (possibly recursive) child element: null when the element
/// is absent, heap-allocated and populated when present.
template <typename T>
concept XmlUniquePtr =
    detail::is_unique_ptr<T>::value && XmlObject<typename T::element_type>;

/// @brief Satisfied when T is a std::optional. Such members hold an optional
/// leaf value or child element: empty when absent, engaged (in place) when
/// present. An optional field can never be marked required.
template <typename T>
concept XmlOptional = detail::is_optional<T>::value;

/// @brief Satisfied when T is std::string or std::string_view.
template <typename T>
concept XmlStringLike =
    std::same_as<T, std::string_view> || std::same_as<T, std::string>;

/// @brief Satisfied when T is a numeric type or a string-like type.
template <typename T>
concept XmlPrimitive = std::is_arithmetic_v<T> || XmlStringLike<T>;

/// @brief One token-to-enumerator mapping entry for an XmlEnumTraits table.
template <typename E>
using EnumEntry = std::pair<std::string_view, E>;

/// @brief Adapts a C++ enum to/from its XML token spelling. Specialize with a
/// `values` member: a constexpr range of EnumEntry<E> pairs mapping each token
/// to its enumerator (e.g. one entry per xs:enumeration facet). Use
/// enum_table() to build it without spelling the element type or count:
/// @code
/// template <> struct xml::XmlEnumTraits<Priority> {
///   static constexpr auto values = xml::enum_table<Priority>(
///       {{"Low", Priority::Low}, {"High", Priority::High}});
/// };
/// @endcode
/// @tparam E Enum type to map.
template <typename E>
struct XmlEnumTraits;

/// @brief Builds an XmlEnumTraits `values` table from a braced list of entries,
/// deducing the entry count. The enum type E is given explicitly; each entry is
/// a `{"token", E::value}` pair.
template <typename E, size_t N>
constexpr auto enum_table(const EnumEntry<E> (&entries)[N])
    -> std::array<EnumEntry<E>, N> {
  return std::to_array(entries);
}

/// @brief Satisfied when E is an enum with an XmlEnumTraits specialization.
template <typename E>
concept XmlEnum = std::is_enum_v<E> && requires { XmlEnumTraits<E>::values; };

/// @brief Adapts an arbitrary leaf type to/from its XML text form. Specialize
/// with two static members:
///   `static bool parse(std::string_view, T&);`     // false on bad input
///   `static void format(std::string& out, const T&);`  // append XML-safe text
/// The built-in xml::Date / xml::Time / xml::DateTime types specialize this;
/// the same hook lets the codegen map any simple type with a known lexical
/// form.
/// @tparam T Leaf value type to map.
template <typename T>
struct XmlValueTraits;

/// @brief Satisfied when T has an XmlValueTraits specialization (a custom leaf
/// value type with text parse/format, e.g. a date).
template <typename T>
concept XmlCustomValue =
    requires(std::string_view s, T& v, const T& cv, std::string& out) {
      { XmlValueTraits<T>::parse(s, v) } -> std::same_as<bool>;
      XmlValueTraits<T>::format(out, cv);
    };

/// @brief Satisfied when T is a leaf value type: a primitive (arithmetic or
/// string-like), a mapped enum, or a custom value (XmlValueTraits). These map
/// to an element's text or an attribute value, as opposed to a nested
/// XmlObject.
template <typename T>
concept XmlScalar = XmlPrimitive<T> || XmlEnum<T> || XmlCustomValue<T>;

// ---- Built-in date/time value types (XSD date / dateTime / time) ----

/// @brief An XSD `date`: a proleptic Gregorian calendar date with an optional
/// timezone. Construct from XML via a field typed `xml::Date`; obtain chrono
/// values via the accessors.
struct Date {
  int year{};           ///< Proleptic Gregorian year (may be negative).
  unsigned month{};     ///< 1-12.
  unsigned day{};       ///< 1-31.
  bool has_tz{};        ///< True if an explicit timezone was present.
  int tz_offset_min{};  ///< Minutes east of UTC (0 for 'Z').

  [[nodiscard]] constexpr auto to_year_month_day() const
      -> std::chrono::year_month_day {
    return std::chrono::year{year} / std::chrono::month{month} /
           std::chrono::day{day};
  }
  [[nodiscard]] constexpr auto to_sys_days() const -> std::chrono::sys_days {
    return std::chrono::sys_days{to_year_month_day()};
  }
  auto operator==(const Date&) const -> bool = default;
};

/// @brief An XSD `time`: time of day with optional fractional seconds and an
/// optional timezone.
struct Time {
  unsigned hour{};    ///< 0-24 (24 only with zero minute/second/fraction).
  unsigned minute{};  ///< 0-59.
  unsigned second{};  ///< 0-59.
  std::uint32_t nanosecond{};  ///< Fractional second, in nanoseconds.
  bool has_tz{};
  int tz_offset_min{};

  /// @brief Time elapsed since midnight (ignores any timezone).
  [[nodiscard]] constexpr auto since_midnight() const
      -> std::chrono::nanoseconds {
    using namespace std::chrono;
    return hours{hour} + minutes{minute} + seconds{second} +
           nanoseconds{nanosecond};
  }
  auto operator==(const Time&) const -> bool = default;
};

/// @brief An XSD `dateTime`: a `Date` and `Time`; any timezone is carried on
/// the time component.
struct DateTime {
  Date date{};
  Time time{};

  /// @brief The instant as a UTC `sys_time` (applies the timezone offset if one
  /// was present; otherwise treats the value as UTC).
  [[nodiscard]] auto to_sys_time() const
      -> std::chrono::sys_time<std::chrono::nanoseconds> {
    using namespace std::chrono;
    sys_time<nanoseconds> t = date.to_sys_days() + time.since_midnight();
    if (time.has_tz) {
      t -= minutes{time.tz_offset_min};
    }
    return t;
  }
  auto operator==(const DateTime&) const -> bool = default;
};

namespace detail {

// ---- Lexical scanners for the date/time types (allocation-free) ----

// Reads exactly `n` decimal digits into `out`, advancing `i`. False if fewer.
constexpr auto dt_digits(std::string_view s, size_t& i, int n, unsigned& out)
    -> bool {
  if (i + static_cast<size_t>(n) > s.size()) {
    return false;
  }
  unsigned v = 0;
  for (int k = 0; k < n; ++k) {
    const char c = s[i + static_cast<size_t>(k)];
    if (c < '0' || c > '9') {
      return false;
    }
    v = v * 10 + static_cast<unsigned>(c - '0');
  }
  i += static_cast<size_t>(n);
  out = v;
  return true;
}

// Year: optional '-' then at least four digits.
constexpr auto dt_year(std::string_view s, size_t& i, int& year) -> bool {
  int sign = 1;
  if (i < s.size() && s[i] == '-') {
    sign = -1;
    ++i;
  }
  const size_t start = i;
  long v = 0;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
    v = v * 10 + (s[i] - '0');
    ++i;
  }
  if (i - start < 4) {
    return false;
  }
  year = sign * static_cast<int>(v);
  return true;
}

// Optional timezone suffix: 'Z' or (+|-)hh:mm. Absent (i at end) is allowed.
constexpr auto dt_tz(std::string_view s, size_t& i, bool& has_tz, int& off)
    -> bool {
  has_tz = false;
  off = 0;
  if (i >= s.size()) {
    return true;
  }
  const char c = s[i];
  if (c == 'Z') {
    has_tz = true;
    ++i;
    return true;
  }
  if (c == '+' || c == '-') {
    const int sign = (c == '-') ? -1 : 1;
    ++i;
    unsigned hh = 0;
    unsigned mm = 0;
    if (!dt_digits(s, i, 2, hh) || i >= s.size() || s[i] != ':') {
      return false;
    }
    ++i;
    if (!dt_digits(s, i, 2, mm) || hh > 14 || mm > 59) {
      return false;
    }
    has_tz = true;
    off = sign * static_cast<int>(hh * 60 + mm);
    return true;
  }
  return false;
}

constexpr auto dt_date(std::string_view s, size_t& i, Date& d) -> bool {
  unsigned mo = 0;
  unsigned da = 0;
  if (!dt_year(s, i, d.year) || i >= s.size() || s[i] != '-') {
    return false;
  }
  ++i;
  if (!dt_digits(s, i, 2, mo) || i >= s.size() || s[i] != '-') {
    return false;
  }
  ++i;
  if (!dt_digits(s, i, 2, da) || mo < 1 || mo > 12 || da < 1 || da > 31) {
    return false;
  }
  d.month = mo;
  d.day = da;
  return true;
}

constexpr auto dt_time(std::string_view s, size_t& i, Time& t) -> bool {
  unsigned hh = 0;
  unsigned mm = 0;
  unsigned ss = 0;
  if (!dt_digits(s, i, 2, hh) || i >= s.size() || s[i] != ':') {
    return false;
  }
  ++i;
  if (!dt_digits(s, i, 2, mm) || i >= s.size() || s[i] != ':') {
    return false;
  }
  ++i;
  if (!dt_digits(s, i, 2, ss)) {
    return false;
  }
  std::uint32_t nano = 0;
  if (i < s.size() && s[i] == '.') {
    ++i;
    const size_t frac_start = i;
    std::uint64_t frac = 0;
    int digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      if (digits < 9) {
        frac = frac * 10 + static_cast<unsigned>(s[i] - '0');
        ++digits;
      }
      ++i;
    }
    if (i == frac_start) {
      return false;  // '.' with no digits
    }
    while (digits < 9) {
      frac *= 10;
      ++digits;
    }
    nano = static_cast<std::uint32_t>(frac);
  }
  if (hh > 24 || mm > 59 || ss > 59 || (hh == 24 && (mm || ss || nano))) {
    return false;
  }
  t.hour = hh;
  t.minute = mm;
  t.second = ss;
  t.nanosecond = nano;
  return true;
}

constexpr auto parse_date(std::string_view s, Date& d) -> bool {
  size_t i = 0;
  Date out{};
  if (!dt_date(s, i, out) || !dt_tz(s, i, out.has_tz, out.tz_offset_min) ||
      i != s.size()) {
    return false;
  }
  d = out;
  return true;
}

constexpr auto parse_time(std::string_view s, Time& t) -> bool {
  size_t i = 0;
  Time out{};
  if (!dt_time(s, i, out) || !dt_tz(s, i, out.has_tz, out.tz_offset_min) ||
      i != s.size()) {
    return false;
  }
  t = out;
  return true;
}

constexpr auto parse_datetime(std::string_view s, DateTime& dt) -> bool {
  size_t i = 0;
  DateTime out{};
  if (!dt_date(s, i, out.date) || i >= s.size() || s[i] != 'T') {
    return false;
  }
  ++i;
  if (!dt_time(s, i, out.time) ||
      !dt_tz(s, i, out.time.has_tz, out.time.tz_offset_min) || i != s.size()) {
    return false;
  }
  dt = out;
  return true;
}

// ---- Canonical-form formatters ----

inline void dt_pad(std::string& o, unsigned v, int width) {
  char tmp[10];
  int n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + v % 10);
    v /= 10;
  } while (v != 0);
  for (int k = n; k < width; ++k) {
    o.push_back('0');
  }
  while (n != 0) {
    o.push_back(tmp[--n]);
  }
}

inline void dt_fmt_tz(std::string& o, bool has_tz, int off) {
  if (!has_tz) {
    return;
  }
  if (off == 0) {
    o.push_back('Z');
    return;
  }
  o.push_back(off < 0 ? '-' : '+');
  const unsigned a = static_cast<unsigned>(off < 0 ? -off : off);
  dt_pad(o, a / 60, 2);
  o.push_back(':');
  dt_pad(o, a % 60, 2);
}

inline void dt_fmt_date(std::string& o, const Date& d) {
  if (d.year < 0) {
    o.push_back('-');
    dt_pad(o, static_cast<unsigned>(-d.year), 4);
  } else {
    dt_pad(o, static_cast<unsigned>(d.year), 4);
  }
  o.push_back('-');
  dt_pad(o, d.month, 2);
  o.push_back('-');
  dt_pad(o, d.day, 2);
}

inline void dt_fmt_time(std::string& o, const Time& t) {
  dt_pad(o, t.hour, 2);
  o.push_back(':');
  dt_pad(o, t.minute, 2);
  o.push_back(':');
  dt_pad(o, t.second, 2);
  if (t.nanosecond != 0) {
    char d[9];
    std::uint32_t n = t.nanosecond;
    for (int k = 8; k >= 0; --k) {
      d[k] = static_cast<char>('0' + n % 10);
      n /= 10;
    }
    int len = 9;
    while (len > 1 && d[len - 1] == '0') {
      --len;
    }
    o.push_back('.');
    o.append(d, static_cast<size_t>(len));
  }
}

}  // namespace detail

/// @brief Maps xml::Date to/from the XSD `date` lexical form.
template <>
struct XmlValueTraits<Date> {
  static auto parse(std::string_view s, Date& d) -> bool {
    return detail::parse_date(s, d);
  }
  static void format(std::string& out, const Date& d) {
    detail::dt_fmt_date(out, d);
    detail::dt_fmt_tz(out, d.has_tz, d.tz_offset_min);
  }
};

/// @brief Maps xml::Time to/from the XSD `time` lexical form.
template <>
struct XmlValueTraits<Time> {
  static auto parse(std::string_view s, Time& t) -> bool {
    return detail::parse_time(s, t);
  }
  static void format(std::string& out, const Time& t) {
    detail::dt_fmt_time(out, t);
    detail::dt_fmt_tz(out, t.has_tz, t.tz_offset_min);
  }
};

/// @brief Maps xml::DateTime to/from the XSD `dateTime` lexical form.
template <>
struct XmlValueTraits<DateTime> {
  static auto parse(std::string_view s, DateTime& dt) -> bool {
    return detail::parse_datetime(s, dt);
  }
  static void format(std::string& out, const DateTime& dt) {
    detail::dt_fmt_date(out, dt.date);
    out.push_back('T');
    detail::dt_fmt_time(out, dt.time);
    detail::dt_fmt_tz(out, dt.time.has_tz, dt.time.tz_offset_min);
  }
};

namespace detail {

/// @brief Resolves an XML token to its enumerator via XmlEnumTraits<E>::values.
/// @return false if no token matches (an undefined enumeration value).
template <typename E>
constexpr auto enum_from_string(std::string_view s, E& out) noexcept -> bool {
  for (const auto& [name, value] : XmlEnumTraits<E>::values) {
    if (name == s) {
      out = value;
      return true;
    }
  }
  return false;
}

/// @brief Maps an enumerator back to its XML token, or "" if unmapped.
template <typename E>
constexpr auto enum_to_string(E v) noexcept -> std::string_view {
  for (const auto& [name, value] : XmlEnumTraits<E>::values) {
    if (value == v) {
      return name;
    }
  }
  return {};
}

}  // namespace detail

/// @brief Creates a value field: binds the enclosing element's own character
/// data to a member (XSD simpleContent). The element may still carry attribute
/// fields; it must not also declare child element/container fields. Takes no
/// XML name -- the element name comes from where the owning type is referenced.
/// @param m        Pointer to the target scalar member (string/number/enum).
/// @param required If true, deserialize() fails when the element has no text.
template <typename C, typename M>
constexpr auto value_field(M C::* m, bool required = false)
    -> ValueField<C, M> {
  static_assert(XmlScalar<M>,
                "value_field target must be a scalar (string/number/enum)");
  return detail::make_field<FieldKind::Value>(std::string_view{}, m, required);
}

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

// ---- Variant fields (xs:choice -> std::variant) ----
namespace detail {
template <typename T>
struct is_variant : std::false_type {};
template <typename... Ts>
struct is_variant<std::variant<Ts...>> : std::true_type {};

// The std::variant underlying a variant field member: the member itself when it
// is a variant, else the element type of a dynamic container of variant.
template <typename M, bool = is_variant<M>::value>
struct variant_member {
  using type = M;
};
template <typename M>
struct variant_member<M, false> {
  using type = typename XmlContainerTraits<M>::value_type;
};
template <typename M>
using variant_member_t = typename variant_member<M>::type;

// Index of alternative T within std::variant V (T must appear exactly once).
template <typename V, typename T>
struct variant_index;
template <typename T, typename... Rest>
struct variant_index<std::variant<T, Rest...>, T> {
  static constexpr size_t value = 0;
};
template <typename T, typename U, typename... Rest>
struct variant_index<std::variant<U, Rest...>, T> {
  static constexpr size_t value =
      1 + variant_index<std::variant<Rest...>, T>::value;
};
}  // namespace detail

/// @brief One alternative of a variant field: binds element `<name>` to the
/// std::variant alternative of type T. Build with `xml::alt<T>("name")`.
template <typename T>
struct VariantAlt {
  std::string_view name;
};

/// @brief Names a variant alternative. @tparam T the std::variant alternative.
template <typename T>
constexpr auto alt(std::string_view name) -> VariantAlt<T> {
  return {name};
}

/// @brief Descriptor for a variant (xs:choice) field. `names` and `hashes` are
/// indexed by the std::variant alternative index.
template <typename Class, typename Member, size_t NAlts>
struct VariantField {
  static constexpr FieldKind kind = FieldKind::Variant;
  Member Class::* member;
  bool required;
  std::array<std::string_view, NAlts> names;
  std::array<FieldHash, NAlts> hashes;
};

namespace detail {
template <typename V, typename C, typename Member, size_t N, typename T>
constexpr void place_variant_alt(VariantField<C, Member, N>& f,
                                 VariantAlt<T> a) {
  constexpr size_t idx = variant_index<V, T>::value;
  f.names[idx] = a.name;
  f.hashes[idx] = hash_field_name(a.name);
}

template <typename C, typename Member, typename... Ts>
constexpr auto make_variant_field(Member C::* m, bool required,
                                  VariantAlt<Ts>... alts) {
  using V = variant_member_t<Member>;
  constexpr size_t N = std::variant_size_v<V>;
  static_assert(sizeof...(Ts) == N,
                "variant_field: provide exactly one alt<T>() per std::variant "
                "alternative");
  VariantField<C, Member, N> f{m, required, {}, {}};
  (place_variant_alt<V>(f, alts), ...);
  return f;
}
}  // namespace detail

/// @brief Creates an optional variant (xs:choice) field. The member is a
/// `std::variant<...>` (exactly one branch) or a dynamic container of variant
/// (a repeated/interleaved choice). Each `alt<T>("name")` binds an element name
/// to the alternative of type T; alternatives must be distinct types.
/// @code
/// std::variant<Circle, Square> shape;  // member
/// xml::variant_field(&Shapes::shape, xml::alt<Circle>("circle"),
///                                    xml::alt<Square>("square"));
/// @endcode
template <typename C, typename Member, typename... Ts>
constexpr auto variant_field(Member C::* m, VariantAlt<Ts>... alts) {
  return detail::make_variant_field(m, false, alts...);
}

/// @brief Like variant_field, but required: deserialize() fails with
/// MissingRequiredField if no alternative is matched (xs:choice minOccurs>=1).
template <typename C, typename Member, typename... Ts>
constexpr auto required_variant_field(Member C::* m, VariantAlt<Ts>... alts) {
  return detail::make_variant_field(m, true, alts...);
}

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

// Variant fields carry per-alternative names/hashes rather than a single
// xml_name/hash, so the per-field projections fall back to a sentinel for them;
// they are matched via the separate variant tables, never these.
template <typename T>
constexpr auto make_field_hashes() noexcept {
  return map_fields<FieldHash, T>([](const auto& f) -> FieldHash {
    if constexpr (requires { f.hash; }) {
      return f.hash;
    } else {
      return FieldHash{0};
    }
  });
}

template <typename T>
constexpr auto make_field_names() noexcept {
  return map_fields<std::string_view, T>([](const auto& f) -> std::string_view {
    if constexpr (requires { f.xml_name; }) {
      return f.xml_name;
    } else {
      return std::string_view{};
    }
  });
}

template <typename T>
constexpr auto make_field_kinds() noexcept {
  return map_fields<FieldKind, T>([](const auto& f) { return f.kind; });
}

/// @brief Kinds matched against a child element by their own single name/hash
/// in find_field_index (element/attribute/container). Value and Variant fields
/// are matched by other means and excluded here.
constexpr bool is_named_field(FieldKind k) noexcept {
  return k == FieldKind::Element || k == FieldKind::Attr ||
         k == FieldKind::Container;
}

template <typename T>
inline size_t find_field_index(FieldHash hash) noexcept {
  constexpr auto hashes = make_field_hashes<T>();
  const auto it = std::ranges::find(hashes, hash);
  return static_cast<size_t>(std::distance(hashes.begin(), it));
}

/// @brief Constexpr fixed-width bitmask over N field indices.
///
/// Backs required-field presence tracking. Sized to as many 64-bit words as N
/// needs, so the common N <= 64 case is a single word and compiles to the same
/// code as a plain uint64_t; wider types simply use more words. A zero mask
/// (any() == false) lets pull() skip all presence tracking entirely.
template <size_t N>
struct FieldMask {
  static constexpr size_t kWords = (N == 0) ? 1 : (N + 63) / 64;
  std::array<uint64_t, kWords> words{};

  constexpr void set(size_t i) noexcept {
    words[i / 64] |= uint64_t{1} << (i % 64);
  }
  [[nodiscard]] constexpr bool any() const noexcept {
    for (uint64_t w : words) {
      if (w != 0) {
        return true;
      }
    }
    return false;
  }
  /// @brief True when every bit set in `required` is also set in *this.
  [[nodiscard]] constexpr bool contains_all(
      const FieldMask& required) const noexcept {
    for (size_t i = 0; i < kWords; ++i) {
      if ((words[i] & required.words[i]) != required.words[i]) {
        return false;
      }
    }
    return true;
  }
  constexpr auto operator==(const FieldMask&) const noexcept -> bool = default;
};

/// @brief Required/parsed presence mask type for T, sized to its field count.
template <typename T>
using RequiredMaskT = FieldMask<field_count<T>>;

/// @brief Bit i is set when field i of XmlMetadata<T> is marked required.
/// Drives the parsed-vs-required check in Parser::pull(). A zero mask (the
/// default, since fields are optional unless opted in) lets pull() skip all
/// presence tracking, so types without required fields pay nothing.
template <typename T>
constexpr auto make_required_mask() noexcept -> RequiredMaskT<T> {
  RequiredMaskT<T> mask{};
  [&]<size_t... I>(std::index_sequence<I...>) {
    ((void)(std::get<I>(XmlMetadata<T>::fields).required &&
            (mask.set(I), true)),
     ...);
  }(field_seq<T>);
  return mask;
}

/// @brief True for the kinds matched against child element tags in pull()'s
/// element loop: child elements and containers (not attributes, not the
/// nameless value field).
constexpr bool is_element_kind(FieldKind k) noexcept {
  return k == FieldKind::Element || k == FieldKind::Container;
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
  return std::ranges::any_of(kinds, is_element_kind);
}

/// @brief True if T declares a value field (binds the element's own text).
template <typename T>
constexpr bool has_value_field() noexcept {
  constexpr auto kinds = make_field_kinds<T>();
  return std::ranges::any_of(kinds,
                             [](FieldKind k) { return k == FieldKind::Value; });
}

/// @brief True if T declares a variant (xs:choice) field.
template <typename T>
constexpr bool has_variant_fields() noexcept {
  constexpr auto kinds = make_field_kinds<T>();
  return std::ranges::any_of(
      kinds, [](FieldKind k) { return k == FieldKind::Variant; });
}

/// @brief Index of T's first value field (only meaningful when one exists).
template <typename T>
constexpr auto value_field_index() noexcept -> size_t {
  constexpr auto kinds = make_field_kinds<T>();
  for (size_t i = 0; i < kinds.size(); ++i) {
    if (kinds[i] == FieldKind::Value) {
      return i;
    }
  }
  return 0;
}

/// @brief Index of the first child-element field, or 0 if none.
template <typename T>
constexpr auto first_elem_index() noexcept -> size_t {
  constexpr auto kinds = make_field_kinds<T>();
  for (size_t i = 0; i < kinds.size(); ++i) {
    if (is_element_kind(kinds[i])) {
      return i;
    }
  }
  return 0;
}

/// @brief Cyclic successor table over child-element fields: entry i holds
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
      if (is_element_kind(kinds[j])) {
        next[i] = j;
        break;
      }
    }
  }
  return next;
}

// ---- Variant (xs:choice) matcher tables ----

/// @brief A single (variant field, alternative) match target: the alternative's
/// element-name hash, the field's index, and the std::variant alternative
/// index.
struct VariantMatcher {
  FieldHash hash;
  size_t field_index;
  size_t alt_index;
};

/// @brief Total number of variant alternatives across all of T's variant
/// fields.
template <typename T>
constexpr auto variant_matcher_count() noexcept -> size_t {
  size_t n = 0;
  [&]<size_t... I>(std::index_sequence<I...>) {
    (
        [&] {
          constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
          if constexpr (f.kind == FieldKind::Variant) {
            n += f.names.size();
          }
        }(),
        ...);
  }(field_seq<T>);
  return n;
}

/// @brief Flat (field_index, alt_index) pairs over every variant alternative,
/// in declaration order then alternative order. Drives both the matcher table
/// and the handler table so they stay aligned.
template <typename T>
constexpr auto variant_pairs() noexcept {
  std::array<std::pair<size_t, size_t>, variant_matcher_count<T>()> out{};
  size_t w = 0;
  [&]<size_t... I>(std::index_sequence<I...>) {
    (
        [&] {
          constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
          if constexpr (f.kind == FieldKind::Variant) {
            for (size_t a = 0; a < f.names.size(); ++a) {
              out[w++] = {I, a};
            }
          }
        }(),
        ...);
  }(field_seq<T>);
  return out;
}

/// @brief Matcher table aligned with variant_pairs(): one entry per
/// alternative.
template <typename T>
constexpr auto make_variant_matchers() noexcept {
  std::array<VariantMatcher, variant_matcher_count<T>()> out{};
  size_t w = 0;
  [&]<size_t... I>(std::index_sequence<I...>) {
    (
        [&] {
          constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
          if constexpr (f.kind == FieldKind::Variant) {
            for (size_t a = 0; a < f.names.size(); ++a) {
              out[w++] = {f.hashes[a], I, a};
            }
          }
        }(),
        ...);
  }(field_seq<T>);
  return out;
}

// Compile-time check that no two element names collide under FNV-1a: among the
// named (element/attribute/container) fields, and across variant alternatives
// (which must be unique among themselves and disjoint from named fields).
template <typename T>
constexpr bool all_names_unique() noexcept {
  constexpr auto hashes = make_field_hashes<T>();
  constexpr auto kinds = make_field_kinds<T>();
  for (size_t i = 0; i < hashes.size(); ++i) {
    if (!is_named_field(kinds[i])) {
      continue;
    }
    for (size_t j = i + 1; j < hashes.size(); ++j) {
      if (is_named_field(kinds[j]) && hashes[i] == hashes[j]) {
        return false;
      }
    }
  }
  constexpr auto vm = make_variant_matchers<T>();
  for (size_t i = 0; i < vm.size(); ++i) {
    for (size_t j = i + 1; j < vm.size(); ++j) {
      if (vm[i].hash == vm[j].hash) {
        return false;
      }
    }
    for (size_t j = 0; j < hashes.size(); ++j) {
      if (is_named_field(kinds[j]) && hashes[j] == vm[i].hash) {
        return false;
      }
    }
  }
  return true;
}

// Compile-time check that no std::optional member is marked required: an
// optional field is inherently optional, so the combination is rejected.
template <typename T>
constexpr bool optionals_not_required() noexcept {
  bool ok = true;
  [&]<size_t... I>(std::index_sequence<I...>) {
    (
        [&] {
          constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
          using M =
              std::remove_cvref_t<decltype(std::declval<T&>().*(f.member))>;
          if constexpr (is_optional<M>::value) {
            if (f.required) {
              ok = false;
            }
          }
        }(),
        ...);
  }(field_seq<T>);
  return ok;
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
/// @brief Compile-time parser policy. A structural type usable as a class
/// non-type template parameter, e.g. `BasicParser<ParserOptions{.strict =
/// true}>`; the Parser / NormalizingParser / StrictParser aliases cover the
/// common cases.
struct ParserOptions {
  /// Expand entity/character references and normalize line endings and
  /// attribute whitespace into owning std::string fields (std::string_view
  /// fields stay raw zero-copy regardless). See NormalizingParser.
  bool normalize = false;
  /// Enforce well-formedness constraints the fast path otherwise skips for
  /// speed: reject "]]>" in character data, '<' in attribute values, and
  /// duplicate attribute names. See StrictParser.
  bool strict = false;
};

template <ParserOptions Opts = ParserOptions{}>
class BasicParser {
 public:
  /// @brief Maximum element nesting depth (descent and skip) before
  /// ErrorCode::DepthExceeded. Guards against stack exhaustion on hostile
  /// input.
  static constexpr int kMaxDepth = 256;

  /// @brief Maximum number of attributes accepted on a single start-tag before
  /// ErrorCode::TooManyAttributes. Bounds attribute-storage amplification.
  static constexpr size_t kMaxAttributesPerElement = 1U << 16;

  /// @brief Mirrors ParserOptions::normalize for this instantiation. When set
  /// (NormalizingParser), owning std::string fields receive normalized,
  /// reference-expanded text: the five predefined entities and numeric
  /// character references are expanded, CR/CRLF are normalized to LF, and
  /// attribute whitespace is normalized to spaces. std::string_view fields are
  /// always raw zero-copy regardless. Off by default (Parser), which compiles
  /// the normalization paths away entirely.
  static constexpr bool kNormalize = Opts.normalize;

  /// @brief Mirrors ParserOptions::strict. When set (StrictParser), the parser
  /// enforces well-formedness constraints the fast path otherwise skips for
  /// speed: "]]>" in character data, '<' in attribute values, and duplicate
  /// attribute names are rejected. Off by default (Parser); the checks compile
  /// away entirely.
  static constexpr bool kStrict = Opts.strict;

  /// @brief Constructs a parser over src. src must outlive the Parser.
  explicit BasicParser(std::string_view src) noexcept
      : src_(src), cur_(src.data()), end_(src.data() + src.size()) {}

  BasicParser(const BasicParser&) = delete;
  auto operator=(const BasicParser&) -> BasicParser& = delete;
  BasicParser(BasicParser&&) noexcept = default;
  auto operator=(BasicParser&&) noexcept -> BasicParser& = default;

  template <ParserOptions O, typename T>
  friend auto deserialize(BasicParser<O>& parser, std::string_view root_name,
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

  // Parse a non-string scalar (arithmetic/bool, a mapped enum, or a custom
  // value type such as a date) from text.
  template <typename T>
  static auto parse_scalar(std::string_view text, T& out) -> bool {
    if constexpr (XmlEnum<T>) {
      return detail::enum_from_string(text, out);
    } else if constexpr (XmlCustomValue<T>) {
      return XmlValueTraits<T>::parse(text, out);
    } else {
      return parse_numeric(text, out);
    }
  }

  // Error code reported when parse_scalar<T> fails on element text.
  template <typename T>
  static constexpr auto scalar_error() noexcept -> ErrorCode {
    if constexpr (XmlEnum<T>) {
      return ErrorCode::InvalidEnumValue;
    } else if constexpr (XmlCustomValue<T>) {
      return ErrorCode::InvalidValue;
    } else {
      return ErrorCode::InvalidNumericValue;
    }
  }

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

  // Assigns a matched attribute's value to a leaf member (string normalized
  // when applicable, otherwise a scalar/enum/custom-value parse).
  template <typename U>
  auto assign_attr_value(U& out, const Attribute& a) -> bool {
    if constexpr (XmlStringLike<U>) {
      if constexpr (kNormalize && std::same_as<U, std::string>) {
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
      return parse_scalar(a.value, out);
    }
  }

  // Captures the enclosing element's own character data into a value_field
  // member (XSD simpleContent), then leaves the closing tag peeked but
  // unconsumed -- the same state pull() returns in normally, so read_element's
  // end_element() handles it. Mirrors the scalar handling of value(): strings
  // (raw or normalized), numbers and enums all route through the same paths.
  template <typename M>
  auto capture_value(M& out) -> bool {
    if constexpr (kNormalize && std::same_as<M, std::string>) {
      out.clear();
      while (const Token* tok = peek()) {
        if (tok->type == TokenType::Text) {
          const ErrorCode ec =
              detail::append_normalized(out, tok->data, detail::NormMode::Text);
          if (ec != ErrorCode::None) {
            return fail(ec);
          }
          consume_peeked();
        } else if (tok->type == TokenType::CData) {
          detail::append_normalized(out, tok->data, detail::NormMode::CData);
          consume_peeked();
        } else if (tok->type == TokenType::ElementClose) {
          return true;
        } else if (tok->type == TokenType::Error) {
          return false;
        } else {
          consume_peeked();
        }
      }
      return fail(ErrorCode::UnexpectedEof);
    } else {
      // Raw string/view, or a numeric/enum to parse: take the last text run.
      std::string_view text;
      while (const Token* tok = peek()) {
        if (tok->type == TokenType::Text || tok->type == TokenType::CData) {
          text = tok->data;
          consume_peeked();
        } else if (tok->type == TokenType::ElementClose) {
          if constexpr (XmlStringLike<M>) {
            out = text;
            return true;
          } else {
            return parse_scalar(text, out) ? true : fail(scalar_error<M>());
          }
        } else if (tok->type == TokenType::Error) {
          return false;
        } else {
          consume_peeked();
        }
      }
      return fail(ErrorCode::UnexpectedEof);
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

  // First occurrence of `c` in [from, end_), or end_ if absent - std::find
  // semantics over the parse range.
  [[nodiscard]] auto find_byte(const char* from, char c) const noexcept -> const
      char* {
    const char* hit = static_cast<const char*>(
        std::memchr(from, c, static_cast<size_t>(end_ - from)));
    return hit != nullptr ? hit : end_;
  }

  // Whether [p, e) contains the CDATA-close delimiter "]]>". Used only by the
  // strict path (Production [14] forbids it in character data); memchr-driven
  // so the scan is fast when the parser opts into the check.
  [[nodiscard]] static auto contains_cdata_end(const char* p,
                                               const char* e) noexcept -> bool {
    while (p < e) {
      const char* hit = static_cast<const char*>(
          std::memchr(p, ']', static_cast<size_t>(e - p)));
      if (hit == nullptr) {
        return false;
      }
      if (e - hit >= 3 && hit[1] == ']' && hit[2] == '>') {
        return true;
      }
      p = hit + 1;
    }
    return false;
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
                         std::span<size_t> arr_fill,
                         detail::RequiredMaskT<T>& parsed) {
    // Reaching here means field I's element matched, so it is present. Record
    // it for the required-field check; gated so types with no required field
    // never touch parsed (the arg dead-codes away). Failure paths below still
    // return false and the mask is then irrelevant.
    if constexpr (detail::make_required_mask<T>().any()) {
      parsed.set(I);
    }
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr || f.kind == FieldKind::Value ||
                  f.kind == FieldKind::Variant) {
      // Attr fields are handled before the element loop; value fields carry the
      // element's own text; variant fields match via the variant tables. None
      // is matched as a child element by its own name, so this handler is never
      // invoked for them at runtime -- it exists only because the field-indexed
      // dispatch table is built over all indices.
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
                         detail::RequiredMaskT<T>& parsed) {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      if (p.attr(f.hash, obj.*(f.member), pos)) {
        parsed.set(I);
      }
    }
  }

  template <typename T, size_t... I>
  static constexpr auto build_elem_dispatch(
      std::index_sequence<I...>) noexcept {
    using Handler = bool (*)(BasicParser&, T&, uint16_t, std::span<size_t>,
                             detail::RequiredMaskT<T>&);
    return std::array<Handler, sizeof...(I)>{&read_field<T, I>...};
  }

  // Handle a matched variant alternative: field FieldI's element matched its
  // alternative AltJ. Emplaces that alternative (into the variant, or into a
  // freshly pushed slot for a repeated/container choice) and reads into it.
  template <typename T, size_t FieldI, size_t AltJ>
  static bool read_variant(BasicParser& p, T& obj, uint16_t depth,
                           detail::RequiredMaskT<T>& parsed) {
    constexpr auto& f = std::get<FieldI>(XmlMetadata<T>::fields);
    using Member = std::decay_t<decltype(obj.*(f.member))>;
    if constexpr (detail::make_required_mask<T>().any()) {
      parsed.set(FieldI);
    }
    if constexpr (detail::is_variant<Member>::value) {
      auto& var = obj.*(f.member);
      var.template emplace<AltJ>();
      return p.read_element(f.names[AltJ], std::get<AltJ>(var), depth + 1);
    } else {
      using Traits = XmlContainerTraits<Member>;
      auto& container = obj.*(f.member);
      auto& slot = Traits::emplace(container);
      slot.template emplace<AltJ>();
      if (!p.read_element(f.names[AltJ], std::get<AltJ>(slot), depth + 1)) {
        Traits::pop(container);
        return false;
      }
      return true;
    }
  }

  template <typename T, size_t... K>
  static constexpr auto build_variant_dispatch(
      std::index_sequence<K...>) noexcept {
    using Handler =
        bool (*)(BasicParser&, T&, uint16_t, detail::RequiredMaskT<T>&);
    constexpr auto pairs = detail::variant_pairs<T>();
    return std::array<Handler, sizeof...(K)>{
        &read_variant<T, pairs[K].first, pairs[K].second>...};
  }

  template <typename T, size_t... I>
  static void dispatch_attrs(BasicParser& p, T& obj,
                             detail::RequiredMaskT<T>& parsed,
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

/// @brief Default parser: raw, zero-copy output, and the fast non-validating
/// path (no reference expansion, normalization, or strict WFC checks). This is
/// the common case.
using Parser = BasicParser<>;

/// @brief Normalizing parser: owning std::string fields receive
/// reference-expanded, line-ending- and attribute-normalized text. See
/// BasicParser::kNormalize.
using NormalizingParser = BasicParser<ParserOptions{.normalize = true}>;

/// @brief Fully-conforming parser: normalizes (expands entity/character
/// references, folds line endings, normalizes attribute whitespace into owning
/// std::string fields) AND enforces the well-formedness constraints the default
/// fast path skips for speed ("]]>" in character data, '<' in attribute values,
/// duplicate attribute names). Combines kNormalize and kStrict.
using StrictParser =
    BasicParser<ParserOptions{.normalize = true, .strict = true}>;

/// @brief Deserializes the root element from parser into object.
/// @tparam T        XmlObject type with an XmlMetadata specialization.
/// @param parser    Parser positioned at the start of the XML input.
/// @param root_name Expected root element name.
/// @param object    Output object to populate.
/// @return True on success, false on any parse or structure error.
template <ParserOptions Opts, typename T>
auto deserialize(BasicParser<Opts>& parser, std::string_view root_name,
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
template <ParserOptions Opts>
inline auto BasicParser<Opts>::peek() -> const Token* {
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

template <ParserOptions Opts>
inline auto BasicParser<Opts>::next() -> const Token* {
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

template <ParserOptions Opts>
inline auto BasicParser<Opts>::next_from_source(Token& token) -> bool {
  if (error() || at_end()) {
    return false;
  }
  if (*cur_ == '<') {
    ++cur_;
    return parse_markup(token);
  }
  const char* start = cur_;
  cur_ = find_byte(cur_, '<');
  if constexpr (kStrict) {
    if (contains_cdata_end(start, cur_)) {
      return make_error(token, ErrorCode::CDataEndInContent);
    }
  }
  token.type = TokenType::Text;
  token.data = {start, static_cast<size_t>(cur_ - start)};
  return true;
}

template <ParserOptions Opts>
inline auto BasicParser<Opts>::parse_markup(Token& token) -> bool {
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

template <ParserOptions Opts>
inline auto BasicParser<Opts>::parse_element_open(Token& token) -> bool {
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
    if constexpr (kStrict) {
      // WFC: Unique Att Spec. Hash compares are the fast filter; the exact
      // name/prefix compare only runs on the (astronomically rare) hash match,
      // so this is O(n^2) hash comparisons over the element's attributes.
      for (size_t i = 0; i + 1 < attributes_.size(); ++i) {
        if (attributes_[i].name_hash == a.name_hash &&
            attributes_[i].name == a.name &&
            attributes_[i].prefix == a.prefix) {
          return make_error(token, ErrorCode::DuplicateAttribute);
        }
      }
    }
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
    if constexpr (kStrict) {
      // WFC: No '<' in attribute values (Production [10]). One short memchr
      // over the value; a '<' beyond val_end belongs to later markup.
      if (find_byte(val_start, '<') < val_end) {
        return make_error(token, ErrorCode::LtInAttributeValue);
      }
    }
    a.value = {val_start, static_cast<size_t>(val_end - val_start)};
    cur_ = val_end + 1;
  }
}

template <ParserOptions Opts>
inline auto BasicParser<Opts>::parse_element_close(Token& token) -> bool {
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

template <ParserOptions Opts>
template <typename T>
inline auto BasicParser<Opts>::parse_numeric(std::string_view text,
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

template <ParserOptions Opts>
template <typename T>
inline auto BasicParser<Opts>::value(std::string_view expected_name, T& out)
    -> bool {
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
    return parse_scalar(text, out) ? true : fail(scalar_error<T>());
  }
}

// Document-order fast path: attribute fields are typically declared in the
// same order the attributes appear, so try the cursor position first and
// fall back to a full first-match scan on miss.
template <ParserOptions Opts>
template <typename T>
inline auto BasicParser<Opts>::attr(const FieldHash hash, T& out, size_t& pos)
    -> bool {
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
  if constexpr (XmlOptional<T>) {
    // Parse into a temporary so a parse failure leaves the optional empty
    // (rather than engaged with a half-parsed value).
    typename T::value_type tmp{};
    if (!assign_attr_value(tmp, a)) {
      return false;
    }
    out = std::move(tmp);
    return true;
  } else {
    return assign_attr_value(out, a);
  }
}

template <ParserOptions Opts>
inline auto BasicParser<Opts>::begin_element(std::string_view expected_name)
    -> bool {
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

template <ParserOptions Opts>
inline auto BasicParser<Opts>::end_element(std::string_view expected_name)
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
template <ParserOptions Opts>
inline void BasicParser<Opts>::skip_element() {
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
template <ParserOptions Opts>
template <typename T>
inline auto BasicParser<Opts>::read_element(std::string_view expected_name,
                                            T& out, const uint16_t depth)
    -> bool {
  if (depth > kMaxDepth) {
    return fail(ErrorCode::DepthExceeded);
  }
  [[maybe_unused]] const bool is_self_closing = last_self_closing_;

  if constexpr (XmlUniquePtr<T>) {
    // Optional/recursive child: allocate, then parse the element into it. The
    // depth guard above bounds recursion; the unwrap keeps the same depth.
    out = std::make_unique<typename T::element_type>();
    return read_element(expected_name, *out, depth);
  } else if constexpr (XmlOptional<T>) {
    // Element present -> engage the optional and parse the inner value/object.
    return read_element(expected_name, out.emplace(), depth);
  } else if constexpr (XmlScalar<T>) {
    if (is_self_closing) {
      if constexpr (XmlStringLike<T>) {
        return true;
      }
      return fail(scalar_error<T>());  // empty numeric/bool/enum
    }
    // Fast path: locate closing tag directly.
    const char* found = find_byte(cur_, '<');
    if (found != end_ && found + 3 + expected_name.size() <= end_ &&
        found[1] == '/' &&
        std::memcmp(found + 2, expected_name.data(), expected_name.size()) ==
            0 &&
        found[2 + expected_name.size()] == '>') {
      std::string_view text{cur_, static_cast<size_t>(found - cur_)};
      if constexpr (kStrict) {
        if (contains_cdata_end(cur_, found)) {
          return fail(ErrorCode::CDataEndInContent);
        }
      }
      cur_ = found + 3 + expected_name.size();
      has_peek_ = false;
      if constexpr (XmlStringLike<T>) {
        return assign_value(out, text);
      } else {
        return parse_scalar(text, out) ? true : fail(scalar_error<T>());
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

template <ParserOptions Opts>
template <typename T>
inline auto BasicParser<Opts>::pull(T& object, const uint16_t depth) -> bool {
  constexpr size_t N = detail::field_count<T>;
  constexpr auto kIdxSeq = detail::field_seq<T>;
  static_assert(
      detail::all_names_unique<T>(),
      "FNV-1a hash collision among element/attribute/variant names in "
      "XmlMetadata<T>");
  static_assert(detail::optionals_not_required<T>(),
                "a std::optional field cannot be marked required (an optional "
                "field is inherently optional)");

  // Per-field fill counters for fixed containers. Only those entries are
  // touched; the compiler elides the rest.
  std::array<size_t, (N != 0 ? N : 1)> arr_fill{};

  // Presence tracking for required fields. When nothing is required (the
  // default) kHasRequired is false, so 'parsed' is never read: every write to
  // it is dead-store eliminated and check_required() compiles to `return true`.
  constexpr auto kRequiredMask = detail::make_required_mask<T>();
  constexpr bool kHasRequired = kRequiredMask.any();
  [[maybe_unused]] detail::RequiredMaskT<T> parsed{};
  const auto check_required = [&]() -> bool {
    if constexpr (kHasRequired) {
      if (!parsed.contains_all(kRequiredMask)) {
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

  // simpleContent: the element's own text feeds a value field (its attributes
  // were handled above). Such a type has no child element fields, so capturing
  // the text and checking required fields completes it.
  if constexpr (detail::has_value_field<T>()) {
    static_assert(!detail::has_element_fields<T>(),
                  "a value_field cannot coexist with child element/container "
                  "fields (XSD simpleContent has no element children)");
    constexpr size_t kValueIdx = detail::value_field_index<T>();
    constexpr auto& vf = std::get<kValueIdx>(XmlMetadata<T>::fields);
    using M = std::decay_t<decltype(object.*(vf.member))>;
    // A value field counts as present only when it carries non-empty text
    // (an empty <e/> or <e></e> satisfies a string but not a required field).
    bool present = false;
    if (last_self_closing_) {
      if constexpr (XmlStringLike<M>) {
        object.*(vf.member) = {};
      } else {
        return fail(scalar_error<M>());  // empty number/enum is invalid
      }
    } else {
      if (!capture_value(object.*(vf.member))) {
        return false;
      }
      if constexpr (XmlStringLike<M>) {
        present = !(object.*(vf.member)).empty();
      } else {
        present = true;  // a number/enum only parses from non-empty text
      }
    }
    if (present) {
      parsed.set(kValueIdx);
    }
    return check_required();
  } else if (last_self_closing_) {
    return check_required();
  }

  // Document-order hint: index of the field expected next. Schema-ordered
  // XML hits the memcmp fast path below on every element; out-of-order
  // documents miss once, re-sync at the dispatch site, and stay correct.
  constexpr bool kHasElems = detail::has_element_fields<T>();
  constexpr bool kHasVariants = detail::has_variant_fields<T>();
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
      // No named field matched. A variant (xs:choice) alternative might; this
      // path is compiled out entirely for types with no variant field.
      bool handled = false;
      if constexpr (kHasVariants) {
        static constexpr auto kVariantMatch =
            detail::make_variant_matchers<T>();
        static constexpr auto variant_dispatch = build_variant_dispatch<T>(
            std::make_index_sequence<kVariantMatch.size()>{});
        for (size_t k = 0; k < kVariantMatch.size(); ++k) {
          if (kVariantMatch[k].hash == token->name_hash) {
            consume_peeked();
            if (!variant_dispatch[k](*this, object, depth, parsed)) {
              return false;
            }
            handled = true;
            break;
          }
        }
      }
      if (!handled) {
        skip_current();
      }
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
    } else if constexpr (XmlEnum<V>) {
      escape<true>(out_, detail::enum_to_string(v));
    } else if constexpr (XmlCustomValue<V>) {
      XmlValueTraits<V>::format(out_, v);  // contract: emits XML-safe text
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
    } else if constexpr (XmlEnum<V>) {
      escape<false>(out_, detail::enum_to_string(v));
    } else if constexpr (XmlCustomValue<V>) {
      XmlValueTraits<V>::format(out_, v);  // contract: emits XML-safe text
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
    if constexpr (XmlUniquePtr<V>) {
      if (v) {  // null optional/recursive child: omit the element entirely
        write_element(tag, *v, depth);
      }
    } else if constexpr (XmlOptional<V>) {
      if (v) {  // disengaged optional: omit the element entirely
        write_field_value(tag, *v, depth);
      }
    } else if constexpr (XmlScalar<V>) {
      write_prim_element(tag, v, depth);
    } else {
      static_assert(XmlObject<V>, "field type must be XmlScalar or XmlObject");
      write_element(tag, v, depth);
    }
  }

  template <typename T, size_t I>
  void write_attr_if(const T& obj) {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      using M = std::remove_cvref_t<decltype(obj.*(f.member))>;
      if constexpr (XmlOptional<M>) {
        if (const auto& m = obj.*(f.member)) {  // omit absent optional attrs
          write_attr_value(f.xml_name, *m);
        }
      } else {
        write_attr_value(f.xml_name, obj.*(f.member));
      }
    }
  }

  template <typename T, size_t... I>
  void write_attrs(const T& obj, std::index_sequence<I...>) {
    (..., write_attr_if<T, I>(obj));
  }

  // Writes the active alternative of a variant under its bound element name.
  template <typename FieldT, typename Var>
  void write_variant_active(const FieldT& f, const Var& var, int depth) {
    [&]<size_t... J>(std::index_sequence<J...>) {
      (..., (var.index() == J
                 ? write_field_value(f.names[J], std::get<J>(var), depth)
                 : void()));
    }(std::make_index_sequence<std::variant_size_v<Var>>{});
  }

  template <typename T, size_t I>
  void write_child(const T& obj, int depth) {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      // written on opening tag
    } else if constexpr (f.kind == FieldKind::Value) {
      // emitted inline by write_element, not as a child
    } else if constexpr (f.kind == FieldKind::Variant) {
      using M = std::decay_t<decltype(obj.*(f.member))>;
      if constexpr (detail::is_variant<M>::value) {
        write_variant_active(f, obj.*(f.member), depth);
      } else {
        for (const auto& item : obj.*(f.member)) {
          write_variant_active(f, item, depth);
        }
      }
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

    if constexpr (detail::has_value_field<T>()) {
      // simpleContent: <tag attrs>text</tag> on a single line.
      constexpr size_t VI = detail::value_field_index<T>();
      constexpr auto& vf = std::get<VI>(XmlMetadata<T>::fields);
      using M = std::decay_t<decltype(obj.*(vf.member))>;
      const auto& v = obj.*(vf.member);
      out_ += '>';
      if constexpr (XmlStringLike<M>) {
        escape<false>(out_, v);
      } else if constexpr (XmlEnum<M>) {
        escape<false>(out_, detail::enum_to_string(v));
      } else if constexpr (XmlCustomValue<M>) {
        XmlValueTraits<M>::format(out_, v);  // contract: emits XML-safe text
      } else {
        append_arithmetic(out_, v);
      }
      out_ += "</";
      out_ += tag;
      out_ += '>';
      do_newline();
    } else if constexpr (detail::has_element_fields<T>() ||
                         detail::has_variant_fields<T>()) {
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

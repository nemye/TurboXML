/// @file LightningXML.hh
/// @brief High-performance C++20 XML pull-parsing deserializer and serializer.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
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

namespace xmlight {

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
  Error,                  ///< Parse error; see Parser::errorCode().
};

/// @brief Specific reason the most recent parse failed.
///
/// Query via Parser::errorCode() after deserialize() returns false. The
/// first error encountered wins; later cascading failures do not overwrite it.
enum class ErrorCode : uint8_t {
  None = 0,                    ///< No error.
  UnexpectedEndAfterLt,        ///< "<" with nothing after it.
  UnexpectedCharAfterLt,       ///< Char after "<" cannot begin markup.
  ExpectedElementName,         ///< Missing element name in a start-tag.
  UnclosedTag,                 ///< Input ended while scanning a start-tag.
  ExpectedAttributeName,       ///< Non-name char where an attribute was expected.
  TooManyAttributes,           ///< Start-tag exceeds MAX_ATTRIBUTES_PER_ELEMENT.
  ExpectedEquals,              ///< Missing '=' after an attribute name.
  ExpectedQuotedValue,         ///< Attribute value not quoted.
  UnterminatedAttributeValue,  ///< No closing quote on an attribute value.
  ExpectedNameInCloseTag,      ///< Empty name in an end-tag ("</>").
  ExpectedCloseTagEnd,         ///< Missing '>' in an end-tag.
  ExpectedPiTarget,            ///< Missing PI target name.
  ReservedPiTarget,            ///< PI target is a reserved case-variant of "xml".
  UnterminatedComment,         ///< Comment with no "-->".
  MalformedComment,            ///< "--" appears inside a comment's content.
  UnterminatedCData,           ///< CDATA section with no "]]>".
  UnterminatedPi,              ///< PI with no "?>".
  InvalidNumericValue,         ///< Numeric/bool field text failed to parse.
  InvalidEnumValue,            ///< Enum field text matched no XmlEnumTraits token.
  InvalidValue,                ///< Custom-value (e.g. date/time) text failed to parse.
  RootElementNotFound,         ///< Requested root element not present.
  ElementMismatch,             ///< End-tag name does not match its start-tag.
  UnexpectedEof,               ///< Input ended mid-element.
  DepthExceeded,               ///< Nesting deeper than MAX_DEPTH.
  MissingRequiredField,        ///< A field marked required was absent from the
                               ///< element.
  UndefinedEntity,             ///< Reference to an entity that is not one of the five
                               ///< predefined entities (no DTD is processed).
  InvalidCharRef,              ///< Malformed or out-of-range character reference
                               ///< (e.g. "&#;", "&#xZZ;", or a non-XML code point).
  CDataEndInContent,           ///< "]]>" appears in character data (Production [14]).
  LtInAttributeValue,          ///< '<' appears in an attribute value (Production [10]).
  DuplicateAttribute,          ///< Two attributes share a name (WFC: Unique Att Spec).
  ForbiddenControlChar,        ///< Control byte outside the Char production [2] (strict).
};

/// @brief Returned by xmlight::validate() when an XmlConstraints check fails.
/// Distinct from ErrorCode (parser errors) so callers can handle the two
/// failure modes independently.
struct ValidationError {
  std::string message;
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

namespace detail {

static constexpr FieldHash FNV_OFFSET = 14695981039346656037ULL;
static constexpr FieldHash FNV_PRIME = 1099511628211ULL;

/// @brief Accumulates one byte into a running FNV-1a hash.
constexpr auto fnv1aStep(FieldHash h, unsigned char c) noexcept -> FieldHash {
  return (h ^ c) * FNV_PRIME;
}

/// @brief Computes the FNV-1a hash of a string at compile time.
constexpr auto hashFieldName(std::string_view s) noexcept -> FieldHash {
  return std::accumulate(s.begin(), s.end(), FNV_OFFSET, [](FieldHash h, char c) {
    return fnv1aStep(h, static_cast<unsigned char>(c));
  });
}

template<typename T>
struct IsUniquePtr : std::false_type {};
template<typename U, typename D>
struct IsUniquePtr<std::unique_ptr<U, D>> : std::true_type {};

template<typename T>
struct IsOptional : std::false_type {};
template<typename U>
struct IsOptional<std::optional<U>> : std::true_type {};

}  // namespace detail

/// @brief Classifies a field as a child element, attribute, or container.
enum class FieldKind : uint8_t { Element, Attr, Container, Value, Variant, List };

/// @brief Compile-time descriptor binding an XML name to a class data member.
/// @tparam K      Field kind.
/// @tparam Class  Containing class type.
/// @tparam Member Member pointer type.
template<FieldKind K, typename Class, typename Member>
struct FieldBase {
  static constexpr FieldKind kind = K;
  std::string_view xml_name;
  Member Class::*member;
  FieldHash hash{};
  bool required{};  ///< If true, deserialize() fails when the field is absent.
};

namespace detail {
template<FieldKind K, typename C, typename M>
constexpr auto makeFieldDesc(std::string_view name, M C::*m, bool required) -> FieldBase<K, C, M> {
  return {.xml_name = name,
          .member = m,
          .hash = name.empty() ? FieldHash{} : hashFieldName(name),
          .required = required};
}
}  // namespace detail

/// @brief Descriptor for a scalar child element field.
template<typename C, typename M>
using Field = FieldBase<FieldKind::Element, C, M>;
/// @brief Descriptor for an XML attribute field.
template<typename C, typename M>
using AttrField = FieldBase<FieldKind::Attr, C, M>;
/// @brief Descriptor for a repeating child element stored in a container.
template<typename C, typename M>
using ContainerField = FieldBase<FieldKind::Container, C, M>;
/// @brief Descriptor for the enclosing element's own text (XSD simpleContent).
template<typename C, typename M>
using ValueField = FieldBase<FieldKind::Value, C, M>;
/// @brief Descriptor for a whitespace-separated list value (XSD xs:list).
template<typename C, typename M>
using ListField = FieldBase<FieldKind::List, C, M>;

/// @brief Creates an element field descriptor.
/// @param name     XML element name.
/// @param m        Pointer to the target member.
/// @param required If true, deserialize() fails unless the element is present.
template<typename C, typename M>
constexpr auto field(std::string_view name, M C::*m, bool required = false) -> Field<C, M> {
  return detail::makeFieldDesc<FieldKind::Element>(name, m, required);
}

/// @brief Creates an attribute field descriptor.
/// @param name     XML attribute name.
/// @param m        Pointer to the target member.
/// @param required If true, deserialize() fails unless the attribute is
/// present.
template<typename C, typename M>
constexpr auto attrField(std::string_view name, M C::*m, bool required = false) -> AttrField<C, M> {
  return detail::makeFieldDesc<FieldKind::Attr>(name, m, required);
}

/// @brief Creates a container field descriptor for dynamic containers (e.g.,
/// std::vector).
/// @param name     XML element name for each item.
/// @param m        Pointer to the target member.
/// @param required If true, deserialize() fails unless at least one item is
///                 present.
template<typename C, typename M>
constexpr auto vecField(std::string_view name, M C::*m,
                        bool required = false) -> ContainerField<C, M> {
  return detail::makeFieldDesc<FieldKind::Container>(name, m, required);
}

/// @brief Creates a container field descriptor for fixed containers (e.g.,
/// std::array).
/// @param name     XML element name for each item.
/// @param m        Pointer to the target member.
/// @param required If true, deserialize() fails unless at least one item is
///                 present.
template<typename C, typename M>
constexpr auto arrField(std::string_view name, M C::*m,
                        bool required = false) -> ContainerField<C, M> {
  return vecField(name, m, required);
}

/// @brief Creates a list field: a single element whose text is a
/// whitespace-separated list of values (XSD xs:list), each parsed into the
/// container member. (For a list-valued attribute, use attrField with a
/// container member.)
/// @param name     XML element name.
/// @param m        Pointer to the target container member.
/// @param required If true, deserialize() fails unless the element is present.
template<typename C, typename M>
constexpr auto listField(std::string_view name, M C::*m, bool required = false) -> ListField<C, M> {
  return detail::makeFieldDesc<FieldKind::List>(name, m, required);
}

/// @brief Specialize this to register XML field mappings for type T.
///
/// The specialization must provide `static constexpr auto fields` as a tuple
/// of Field, AttrField, or ContainerField descriptors.
/// @tparam T User-defined struct to describe.
template<typename T>
struct XmlMetadata;

/// @brief Satisfied when T has an XmlMetadata specialization with a fields
/// tuple.
template<typename T>
concept XmlObject = requires { XmlMetadata<T>::fields; };

/// @brief Satisfied when T is a std::unique_ptr to an XmlObject. Such members
/// hold an optional (possibly recursive) child element: null when the element
/// is absent, heap-allocated and populated when present.
template<typename T>
concept XmlUniquePtr = detail::IsUniquePtr<T>::value && XmlObject<typename T::element_type>;

/// @brief Satisfied when T is a std::optional. Such members hold an optional
/// leaf value or child element: empty when absent, engaged (in place) when
/// present. An optional field can never be marked required.
template<typename T>
concept XmlOptional = detail::IsOptional<T>::value;

/// @brief Satisfied when T is std::string or std::string_view.
template<typename T>
concept XmlStringLike = std::same_as<T, std::string_view> || std::same_as<T, std::string>;

/// @brief Satisfied when T is a numeric type or a string-like type.
template<typename T>
concept XmlPrimitive = std::is_arithmetic_v<T> || XmlStringLike<T>;

/// @brief One token-to-enumerator mapping entry for an XmlEnumTraits table.
template<typename E>
using EnumEntry = std::pair<std::string_view, E>;

/// @brief Adapts a C++ enum to/from its XML token spelling. Specialize with a
/// `values` member: a constexpr range of EnumEntry<E> pairs mapping each token
/// to its enumerator (e.g. one entry per xs:enumeration facet). Keep the table
/// exhaustive: serializing an enumerator with no entry emits empty text. Use
/// enumTable() to build it without spelling the element type or count:
/// @code
/// template <> struct xmlight::XmlEnumTraits<Priority> {
///   static constexpr auto values = xmlight::enumTable<Priority>(
///       {{"Low", Priority::Low}, {"High", Priority::High}});
/// };
/// @endcode
/// @tparam E Enum type to map.
template<typename E>
struct XmlEnumTraits;

/// @brief Builds an XmlEnumTraits `values` table from a braced list of entries,
/// deducing the entry count. The enum type E is given explicitly; each entry is
/// a `{"token", E::value}` pair.
template<typename E, size_t N>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
constexpr auto enumTable(const EnumEntry<E> (&entries)[N]) -> std::array<EnumEntry<E>, N> {
  return std::to_array(entries);
}

/// @brief Satisfied when E is an enum with an XmlEnumTraits specialization.
template<typename E>
concept XmlEnum = std::is_enum_v<E> && requires { XmlEnumTraits<E>::values; };

/// @brief Adapts an arbitrary leaf type to/from its XML text form. Specialize
/// with two static members:
///   `static bool parse(std::string_view, T&);`     // false on bad input
///   `static auto format(std::string& out, const T&) -> void;`  // append XML-safe text
/// The built-in xmlight::Date / xmlight::Time / xmlight::DateTime types specialize this;
/// the same hook lets the codegen map any simple type with a known lexical
/// form.
/// @tparam T Leaf value type to map.
template<typename T>
struct XmlValueTraits;

/// @brief Satisfied when T has an XmlValueTraits specialization (a custom leaf
/// value type with text parse/format, e.g. a date).
template<typename T>
concept XmlCustomValue = requires(std::string_view s, T& v, const T& cv, std::string& out) {
  { XmlValueTraits<T>::parse(s, v) } -> std::same_as<bool>;
  XmlValueTraits<T>::format(out, cv);
};

/// @brief Satisfied when T is a leaf value type: a primitive (arithmetic or
/// string-like), a mapped enum, or a custom value (XmlValueTraits). These map
/// to an element's text or an attribute value, as opposed to a nested
/// XmlObject.
template<typename T>
concept XmlScalar = XmlPrimitive<T> || XmlEnum<T> || XmlCustomValue<T>;

/// @brief An XSD `date`: a proleptic Gregorian calendar date with an optional
/// timezone. Construct from XML via a field typed `xmlight::Date`; obtain chrono
/// values via the accessors.
struct Date {
  int year{};       ///< Proleptic Gregorian year (may be negative).
  uint8_t month{};  ///< 1-12.
  uint8_t day{};    ///< 1-31.
  // NOLINTNEXTLINE(readability-redundant-member-init): explicit for member-init checks
  std::optional<std::chrono::minutes> tz{};  ///< Offset east of UTC, if explicit (0 for 'Z').

  [[nodiscard]] constexpr auto toYearMonthDay() const -> std::chrono::year_month_day {
    return std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day};
  }
  [[nodiscard]] constexpr auto toSysDays() const -> std::chrono::sys_days {
    return std::chrono::sys_days{toYearMonthDay()};
  }
  auto operator<=>(const Date&) const = default;
};

/// @brief An XSD `time`: time of day with optional fractional seconds and an
/// optional timezone.
struct Time {
  uint8_t hour{};         ///< 0-24 (24 only with zero minute/second/fraction).
  uint8_t minute{};       ///< 0-59.
  uint8_t second{};       ///< 0-59.
  uint32_t nanosecond{};  ///< Fractional second, in nanoseconds.
  // NOLINTNEXTLINE(readability-redundant-member-init): explicit for member-init checks
  std::optional<std::chrono::minutes> tz{};  ///< Offset east of UTC, if explicit (0 for 'Z').

  /// @brief Time elapsed since midnight (ignores any timezone).
  [[nodiscard]] constexpr auto sinceMidnight() const -> std::chrono::nanoseconds {
    using namespace std::chrono;
    return hours{hour} + minutes{minute} + seconds{second} + nanoseconds{nanosecond};
  }
  auto operator<=>(const Time&) const = default;
};

/// @brief An XSD `dateTime`: a `Date` and `Time`; any timezone is carried on
/// the time component.
struct DateTime {
  Date date{};
  Time time{};

  /// @brief The instant as a UTC `sys_time` (applies the timezone offset if one
  /// was present; otherwise treats the value as UTC).
  [[nodiscard]] auto toSysTime() const -> std::chrono::sys_time<std::chrono::nanoseconds> {
    using namespace std::chrono;
    sys_time<nanoseconds> t = date.toSysDays() + time.sinceMidnight();
    if (time.tz) {
      t -= *time.tz;
    }
    return t;
  }
  auto operator<=>(const DateTime&) const = default;
};

/// @brief Creates a value field: binds the enclosing element's own character
/// data to a member (XSD simpleContent). The element may still carry attribute
/// fields; it must not also declare child element/container fields. Takes no
/// XML name -- the element name comes from where the owning type is referenced.
/// @param m        Pointer to the target scalar member (string/number/enum).
/// @param required If true, deserialize() fails when the element has no text.
template<typename C, XmlScalar M>
constexpr auto valueField(M C::*m, bool required = false) -> ValueField<C, M> {
  return detail::makeFieldDesc<FieldKind::Value>({}, m, required);
}

/// @brief Adapts a container for use with ContainerField. Specialize for custom
/// types.
/// @tparam C Container type to adapt.
template<typename C>
struct XmlContainerTraits;

/// @brief XmlContainerTraits specialization for std::vector.
template<typename T, typename A>
struct XmlContainerTraits<std::vector<T, A>> {
  using value_type = T;
  static auto emplace(std::vector<T, A>& c) -> T& {
    if (c.capacity() == 0) {
      // grow 0->2 in one allocation at the cost of one spare slot for
      // single-child containers
      c.reserve(2);
    }
    return c.emplace_back();
  }
  static auto pop(std::vector<T, A>& c) -> void { c.pop_back(); }
};

/// @brief XmlContainerTraits specialization for std::array.
template<typename T, size_t N>
struct XmlContainerTraits<std::array<T, N>> {
  using value_type = T;
  static constexpr size_t capacity = N;
  static auto at(std::array<T, N>& c, size_t i) -> T& { return c[i]; }
  static auto at(const std::array<T, N>& c, size_t i) -> const T& { return c[i]; }
};

/// @brief Satisfied when C supports dynamic insertion via XmlContainerTraits
/// (e.g., std::vector).
template<typename C>
concept XmlDynContainer = requires(C& c) {
  typename XmlContainerTraits<C>::value_type;
  {
    XmlContainerTraits<C>::emplace(c)
  } -> std::same_as<typename XmlContainerTraits<C>::value_type&>;
  XmlContainerTraits<C>::pop(c);
};

/// @brief Satisfied when C has a fixed capacity and indexed access via
/// XmlContainerTraits (e.g., std::array).
template<typename C>
concept XmlFixedContainer = requires(C& c, size_t i) {
  typename XmlContainerTraits<C>::value_type;
  XmlContainerTraits<C>::capacity;
  { XmlContainerTraits<C>::at(c, i) } -> std::same_as<typename XmlContainerTraits<C>::value_type&>;
};

/// @brief Satisfied when C is a dynamic or fixed container: the member type a
/// whitespace-separated list value (XSD xs:list) is parsed into.
template<typename C>
concept XmlListContainer = XmlDynContainer<C> || XmlFixedContainer<C>;

/// @brief One alternative of a variant field: binds element `<name>` to the
/// std::variant alternative of type T. Build with `xmlight::alt<T>("name")`.
template<typename T>
struct VariantAlt {
  std::string_view name;
};

/// @brief Names a variant alternative. @tparam T the std::variant alternative.
template<typename T>
constexpr auto alt(std::string_view name) -> VariantAlt<T> {
  return {name};
}

/// @brief Descriptor for a variant (xs:choice) field. `names` and `hashes` are
/// indexed by the std::variant alternative index.
template<typename Class, typename Member, size_t NAlts>
struct VariantField {
  static constexpr FieldKind kind = FieldKind::Variant;
  Member Class::*member;
  bool required;
  std::array<std::string_view, NAlts> names;
  std::array<FieldHash, NAlts> hashes;
};

namespace detail {

static constexpr auto SPACE_TABLE = [] {
  std::array<bool, 256> t{false};
  t[' '] = t['\t'] = t['\n'] = t['\r'] = true;
  return t;
}();

static constexpr auto NAME_START_TABLE = [] {
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

static constexpr auto NAME_CHAR_TABLE = [] {
  std::array<bool, 256> t = detail::NAME_START_TABLE;
  for (unsigned i = '0'; i <= '9'; ++i) {
    t[i] = true;
  }
  t['-'] = t['.'] = true;
  return t;
}();

static constexpr auto trimWhitespace(std::string_view& text) noexcept -> void {
  while (!text.empty() && detail::SPACE_TABLE[static_cast<unsigned char>(text.front())]) {
    text.remove_prefix(1);
  }

  while (!text.empty() && detail::SPACE_TABLE[static_cast<unsigned char>(text.back())]) {
    text.remove_suffix(1);
  }
}

[[nodiscard]] static constexpr auto isSpace(char c) noexcept -> bool {
  return detail::SPACE_TABLE[static_cast<unsigned char>(c)];
}
[[nodiscard]] static constexpr auto isNameStart(char c) noexcept -> bool {
  return detail::NAME_START_TABLE[static_cast<unsigned char>(c)];
}
[[nodiscard]] static constexpr auto isNameChar(char c) noexcept -> bool {
  return detail::NAME_CHAR_TABLE[static_cast<unsigned char>(c)];
}

/// @brief A forward cursor over a date/time lexical form. Each scan method
/// advances past what it consumed on success and leaves the cursor unchanged on
/// failure, so grammar rules compose as a short-circuiting boolean chain.
class DtCursor {
 public:
  explicit DtCursor(std::string_view s) : s_{s} {}

  [[nodiscard]] auto atEnd() const -> bool { return pos_ == s_.size(); }

  /// @brief Consumes the literal character c.
  [[nodiscard]] auto eat(char c) -> bool {
    const bool ok = pos_ < s_.size() && s_[pos_] == c;
    pos_ += ok ? 1 : 0;
    return ok;
  }

  /// @brief Reads exactly n decimal digits as an unsigned integer.
  [[nodiscard]] auto fixed(size_t n, uint32_t& out) -> bool {
    if (pos_ + n > s_.size()) {
      return false;
    }
    uint32_t v = 0;
    for (size_t k = 0; k < n; ++k) {
      const char c = s_[pos_ + k];
      if (!isDigit(c)) {
        return false;
      }
      v = v * 10 + static_cast<uint32_t>(c - '0');
    }
    pos_ += n;
    out = v;
    return true;
  }

  /// @brief Reads a year: an optional leading '-' then four or more digits.
  [[nodiscard]] auto year(int& out) -> bool {
    const int sign = eat('-') ? -1 : 1;
    const size_t start = pos_;
    int64_t v = 0;
    while (pos_ < s_.size() && isDigit(s_[pos_])) {
      v = v * 10 + (s_[pos_++] - '0');
      if (v > std::numeric_limits<int>::max()) {
        return false;
      }
    }
    if (pos_ - start < 4) {
      return false;
    }
    out = sign * static_cast<int>(v);
    return true;
  }

  /// @brief Reads an optional fractional second ('.' then digits) as
  /// nanoseconds, truncating precision beyond nine digits.
  [[nodiscard]] auto fraction(uint32_t& nanos) -> bool {
    nanos = 0;
    if (!eat('.')) {
      return true;
    }
    const size_t start = pos_;
    uint64_t v = 0;
    int digits = 0;
    for (; pos_ < s_.size() && isDigit(s_[pos_]); ++pos_) {
      if (digits < 9) {
        v = v * 10 + static_cast<uint32_t>(s_[pos_] - '0');
        ++digits;
      }
    }
    if (pos_ == start) {
      return false;  // '.' with no digits
    }
    for (; digits < 9; ++digits) {
      v *= 10;
    }
    nanos = static_cast<uint32_t>(v);
    return true;
  }

  /// @brief Reads an optional timezone ('Z' or (+|-)hh:mm); absence is allowed
  /// only at end of input.
  [[nodiscard]] auto timezone(std::optional<std::chrono::minutes>& tz) -> bool {
    tz.reset();
    if (atEnd()) {
      return true;
    }
    if (eat('Z')) {
      tz = std::chrono::minutes{0};
      return true;
    }
    int sign = 0;
    if (eat('+')) {
      sign = 1;
    } else if (eat('-')) {
      sign = -1;
    } else {
      return false;
    }
    uint32_t hh = 0;
    uint32_t mm = 0;
    if (!fixed(2, hh) || !eat(':') || !fixed(2, mm) || hh > 14 || mm > 59) {
      return false;
    }
    tz = std::chrono::minutes{sign * static_cast<int>(hh * 60 + mm)};
    return true;
  }

  /// @brief Reads a 'CCYY-MM-DD' date body (no timezone) into d.
  [[nodiscard]] auto date(Date& d) -> bool {
    uint32_t mo = 0;
    uint32_t da = 0;
    if (!year(d.year) || !eat('-') || !fixed(2, mo) || !eat('-') || !fixed(2, da)) {
      return false;
    }
    if (mo < 1 || mo > 12 || da < 1 || da > 31) {
      return false;
    }
    d.month = static_cast<uint8_t>(mo);
    d.day = static_cast<uint8_t>(da);
    return true;
  }

  /// @brief Reads an 'hh:mm:ss(.fraction)?' time body (no timezone) into t.
  [[nodiscard]] auto time(Time& t) -> bool {
    uint32_t hh = 0;
    uint32_t mm = 0;
    uint32_t ss = 0;
    uint32_t nanos = 0;
    if (!fixed(2, hh) || !eat(':') || !fixed(2, mm) || !eat(':') || !fixed(2, ss) ||
        !fraction(nanos)) {
      return false;
    }
    if (hh > 24 || mm > 59 || ss > 59 || (hh == 24 && (mm != 0 || ss != 0 || nanos != 0))) {
      return false;
    }
    t.hour = static_cast<uint8_t>(hh);
    t.minute = static_cast<uint8_t>(mm);
    t.second = static_cast<uint8_t>(ss);
    t.nanosecond = nanos;
    return true;
  }

 private:
  static auto isDigit(char c) -> bool { return c >= '0' && c <= '9'; }

  std::string_view s_;
  size_t pos_ = 0;
};

inline auto dtFmtTz(std::string& o, const std::optional<std::chrono::minutes> tz) -> void {
  if (!tz) {
    return;
  }
  const int off = static_cast<int>(tz->count());
  if (off == 0) {
    o += 'Z';
    return;
  }
  const int a = std::abs(off);
  o += std::format("{}{:02}:{:02}", off < 0 ? '-' : '+', a / 60, a % 60);
}

inline auto dtFmtDate(std::string& o, const Date& d) -> void {
  o += d.year < 0 ? std::format("-{:04}-{:02}-{:02}", -d.year, d.month, d.day)
                  : std::format("{:04}-{:02}-{:02}", d.year, d.month, d.day);
}

inline auto dtFmtTime(std::string& o, const Time& t) -> void {
  o += std::format("{:02}:{:02}:{:02}", t.hour, t.minute, t.second);
  if (t.nanosecond != 0) {
    const std::string frac = std::format("{:09}", t.nanosecond);
    o += '.' + frac.substr(0, frac.find_last_not_of('0') + 1);
  }
}

/// @brief Drives an XmlValueTraits date/time parse: trims, runs `grammar` over a
/// cursor that must consume the whole input, and assigns only on full success.
template<typename T, typename Grammar>
auto dtParse(std::string_view s, T& out, Grammar grammar) -> bool {
  trimWhitespace(s);
  DtCursor c{s};
  T tmp{};
  if (!grammar(c, tmp) || !c.atEnd()) {
    return false;
  }
  out = tmp;
  return true;
}

/// @brief Resolves an XML token to its enumerator via XmlEnumTraits<E>::values.
/// @return false if no token matches (an undefined enumeration value).
template<typename E>
constexpr auto enumFromString(std::string_view s, E& out) noexcept -> bool {
  const auto& vals = XmlEnumTraits<E>::values;
  const auto it = std::ranges::find(vals, s, &EnumEntry<E>::first);
  if (it == vals.end()) {
    return false;
  }
  out = it->second;
  return true;
}

/// @brief Maps an enumerator back to its XML token, or "" if unmapped.
template<typename E>
constexpr auto enumToString(E v) noexcept -> std::string_view {
  const auto& vals = XmlEnumTraits<E>::values;
  const auto it = std::ranges::find(vals, v, &EnumEntry<E>::second);
  return it != vals.end() ? it->first : std::string_view{};
}

template<typename T>
struct IsVariant : std::false_type {};
template<typename... Ts>
struct IsVariant<std::variant<Ts...>> : std::true_type {};

template<typename M, bool = IsVariant<M>::value>
struct VariantMember {
  using type = M;
};
template<typename M>
struct VariantMember<M, false> {
  using type = typename XmlContainerTraits<M>::value_type;
};
template<typename M>
using variant_member_t = typename VariantMember<M>::type;

template<typename V, typename T>
struct VariantIndex;
template<typename T, typename... Rest>
struct VariantIndex<std::variant<T, Rest...>, T> {
  static constexpr size_t value = 0;
};
template<typename T, typename U, typename... Rest>
struct VariantIndex<std::variant<U, Rest...>, T> {
  static constexpr size_t value = 1 + VariantIndex<std::variant<Rest...>, T>::value;
};

template<typename V, typename C, typename Member, size_t N, typename T>
constexpr auto placeVariantAlt(VariantField<C, Member, N>& f, VariantAlt<T> a) -> void {
  constexpr size_t idx = VariantIndex<V, T>::value;
  f.names[idx] = a.name;
  f.hashes[idx] = hashFieldName(a.name);
}

template<typename C, typename Member, typename... Ts>
constexpr auto makeVariantField(Member C::*m, bool required, VariantAlt<Ts>... alts) {
  using V = variant_member_t<Member>;
  constexpr size_t N = std::variant_size_v<V>;
  static_assert(sizeof...(Ts) == N,
                "variantField: provide exactly one alt<T>() per std::variant "
                "alternative");
  VariantField<C, Member, N> f{m, required, {}, {}};
  (placeVariantAlt<V>(f, alts), ...);
  return f;
}

/// @brief Number of XML field descriptors declared for T.
template<typename T>
inline constexpr size_t FIELD_COUNT = std::tuple_size_v<decltype(XmlMetadata<T>::fields)>;

/// @brief Index sequence over T's field descriptors, in declaration order.
template<typename T>
inline constexpr auto FIELD_SEQ = std::make_index_sequence<FIELD_COUNT<T>>{};

/// @brief Builds a std::array<Elem, FIELD_COUNT<T>> by applying `proj` to each
/// field descriptor of T in declaration order. Elem is given explicitly so the
/// result type is well-defined even for a zero-field type.
template<typename Elem, typename T, typename Proj>
constexpr auto mapFields(Proj proj) noexcept {
  return [&]<size_t... I>(std::index_sequence<I...>) {
    return std::array<Elem, FIELD_COUNT<T>>{{proj(std::get<I>(XmlMetadata<T>::fields))...}};
  }(FIELD_SEQ<T>);
}

// Variant fields carry per-alternative names/hashes rather than a single
// xml_name/hash, so the per-field projections fall back to a sentinel for them;
// they are matched via the separate variant tables, never these.
template<typename T>
constexpr auto makeFieldHashes() noexcept {
  return mapFields<FieldHash, T>([](const auto& f) -> FieldHash {
    if constexpr (requires { f.hash; }) {
      return f.hash;
    } else {
      return FieldHash{};
    }
  });
}

template<typename T>
constexpr auto makeFieldNames() noexcept {
  return mapFields<std::string_view, T>([](const auto& f) -> std::string_view {
    if constexpr (requires { f.xml_name; }) {
      return f.xml_name;
    } else {
      return std::string_view{};
    }
  });
}

template<typename T>
constexpr auto makeFieldKinds() noexcept {
  return mapFields<FieldKind, T>([](const auto& f) { return f.kind; });
}

/// @brief Kinds matched against a child element by their own single name/hash
/// in findFieldIndex (element/attribute/container/list). Value and Variant
/// fields are matched by other means and excluded here.
constexpr auto isNamedField(FieldKind k) noexcept -> bool {
  return k == FieldKind::Element || k == FieldKind::Attr || k == FieldKind::Container ||
         k == FieldKind::List;
}

template<typename T>
inline auto findFieldIndex(FieldHash hash) noexcept -> size_t {
  constexpr auto hashes = makeFieldHashes<T>();
  const auto it = std::ranges::find(hashes, hash);
  return static_cast<size_t>(std::distance(hashes.begin(), it));
}

/// @brief Constexpr fixed-width bitmask over N field indices.
///
/// Backs required-field presence tracking. Sized to as many 64-bit words as N
/// needs, so the common N <= 64 case is a single word and compiles to the same
/// code as a plain uint64_t; wider types simply use more words. A zero mask
/// (any() == false) lets pull() skip all presence tracking entirely.
template<size_t N>
struct FieldMask {
  static constexpr size_t WORDS = (N == 0) ? 1 : (N + 63) / 64;
  std::array<uint64_t, WORDS> words{};

  constexpr auto set(size_t i) noexcept -> void { words[i / 64] |= uint64_t{1} << (i % 64); }
  [[nodiscard]] constexpr auto any() const noexcept -> bool {
    return std::ranges::any_of(words, [](uint64_t w) { return w != 0; });
  }
  /// @brief True when every bit set in `required` is also set in *this.
  [[nodiscard]] constexpr auto containsAll(const FieldMask& required) const noexcept -> bool {
    for (size_t i = 0; i < WORDS; ++i) {
      if ((words[i] & required.words[i]) != required.words[i]) {
        return false;
      }
    }
    return true;
  }
  constexpr auto operator==(const FieldMask&) const noexcept -> bool = default;
};

/// @brief Required/parsed presence mask type for T, sized to its field count.
template<typename T>
using RequiredMaskT = FieldMask<FIELD_COUNT<T>>;

/// @brief Bit i is set when field i of XmlMetadata<T> is marked required.
/// Drives the parsed-vs-required check in Parser::pull(). A zero mask (the
/// default, since fields are optional unless opted in) lets pull() skip all
/// presence tracking, so types without required fields pay nothing.
template<typename T>
constexpr auto makeRequiredMask() noexcept -> RequiredMaskT<T> {
  RequiredMaskT<T> mask{};
  [&]<size_t... I>(std::index_sequence<I...>) {
    ([&] {
      if (std::get<I>(XmlMetadata<T>::fields).required) {
        mask.set(I);
      }
    }(), ...);
  }(FIELD_SEQ<T>);
  return mask;
}

/// @brief True when any field of T is marked required; gates presence
/// tracking in pull().
template<typename T>
inline constexpr bool HAS_REQUIRED_FIELDS = makeRequiredMask<T>().any();

/// @brief True for the kinds matched against child element tags in pull()'s
/// element loop: child elements, containers, and list elements (not attributes,
/// not the nameless value field).
constexpr auto isElementKind(FieldKind k) noexcept -> bool {
  return k == FieldKind::Element || k == FieldKind::Container || k == FieldKind::List;
}

/// @brief True if any of T's field kinds satisfies pred.
template<typename T, typename Pred>
constexpr auto anyFieldKindIs(Pred pred) noexcept -> bool {
  constexpr auto kinds = makeFieldKinds<T>();
  return std::ranges::any_of(kinds, pred);
}

/// @brief Index of T's first field kind satisfying pred, or FIELD_COUNT<T> if
/// none.
template<typename T, typename Pred>
constexpr auto firstFieldIndexIf(Pred pred) noexcept -> size_t {
  constexpr auto kinds = makeFieldKinds<T>();
  return static_cast<size_t>(std::distance(kinds.begin(), std::ranges::find_if(kinds, pred)));
}

constexpr auto isKind(FieldKind want) noexcept {
  return [want](FieldKind k) { return k == want; };
}

/// @brief True if any field of T is an attribute field.
template<typename T>
inline constexpr bool HAS_ATTR_FIELDS = anyFieldKindIs<T>(isKind(FieldKind::Attr));

/// @brief True if any field of T is a child element or container field.
template<typename T>
inline constexpr bool HAS_ELEMENT_FIELDS = anyFieldKindIs<T>(isElementKind);

/// @brief True if T declares a value field (binds the element's own text).
template<typename T>
inline constexpr bool HAS_VALUE_FIELD = anyFieldKindIs<T>(isKind(FieldKind::Value));

/// @brief True if T declares a variant (xs:choice) field.
template<typename T>
inline constexpr bool HAS_VARIANT_FIELDS = anyFieldKindIs<T>(isKind(FieldKind::Variant));

/// @brief True when the type a child-element member parses into (through
/// unique_ptr/optional wrappers) declares attribute fields.
template<typename M>
constexpr auto elementTargetHasAttrs() noexcept -> bool {
  if constexpr (IsUniquePtr<M>::value) {
    return elementTargetHasAttrs<typename M::element_type>();
  } else if constexpr (IsOptional<M>::value) {
    return elementTargetHasAttrs<typename M::value_type>();
  } else if constexpr (XmlObject<M>) {
    return HAS_ATTR_FIELDS<M>;
  } else {
    return false;
  }
}

/// @brief True if any field of T satisfies `pred`, which is called with the
/// field descriptor and a std::type_identity carrying the member type.
/// Constexpr-folds over the fields in declaration order.
template<typename T, typename Pred>
constexpr auto anyFieldSatisfies(Pred pred) noexcept -> bool {
  return [&]<size_t... I>(std::index_sequence<I...>) {
    return ([&] {
      constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
      using M = std::remove_cvref_t<decltype(std::declval<T&>().*(f.member))>;
      return pred(f, std::type_identity<M>{});
    }() || ...);
  }(FIELD_SEQ<T>);
}

/// @brief True when any child element/container field of T maps to a type
/// with attribute fields. Gates the attributed-tag fast path in pull():
/// schemas whose children carry no mapped attributes keep the minimal
/// simple-tag matcher (attributed documents still parse via the tokenizer).
template<typename T>
inline constexpr bool ANY_ELEM_TARGET_HAS_ATTRS = anyFieldSatisfies<T>([](const auto& f, auto m) {
  using F = std::remove_cvref_t<decltype(f)>;
  using M = typename decltype(m)::type;
  if constexpr (F::kind == FieldKind::Element) {
    return elementTargetHasAttrs<M>();
  } else if constexpr (F::kind == FieldKind::Container) {
    return elementTargetHasAttrs<typename XmlContainerTraits<M>::value_type>();
  } else {
    return false;
  }
});

/// @brief Type a child-element member parses into, unwrapped through
/// unique_ptr/optional wrappers. The type-level analog of
/// elementTargetHasAttrs.
template<typename M>
constexpr auto elementTargetType() noexcept {
  if constexpr (IsUniquePtr<M>::value) {
    return elementTargetType<typename M::element_type>();
  } else if constexpr (IsOptional<M>::value) {
    return elementTargetType<typename M::value_type>();
  } else {
    return std::type_identity<M>{};
  }
}
template<typename M>
using ElementTargetT = typename decltype(elementTargetType<M>())::type;

/// @brief Number of attribute fields declared by T.
template<typename T>
constexpr auto countAttrFields() noexcept -> size_t {
  constexpr auto kinds = makeFieldKinds<T>();
  return static_cast<size_t>(std::ranges::count_if(kinds, isKind(FieldKind::Attr)));
}
template<typename T>
inline constexpr size_t N_ATTR_FIELDS = countAttrFields<T>();

/// @brief Field indices of T's attribute fields in declaration order
/// (attribute ordinal -> field index).
template<typename T>
constexpr auto makeAttrOrdinals() noexcept {
  constexpr auto kinds = makeFieldKinds<T>();
  std::array<size_t, (N_ATTR_FIELDS<T> != 0 ? N_ATTR_FIELDS<T> : 1)> ords{};
  size_t k = 0;
  for (size_t i = 0; i < kinds.size(); ++i) {
    if (kinds[i] == FieldKind::Attr) {
      ords[k++] = i;
    }
  }
  return ords;
}

/// @brief Attribute ordinal of field I (count of attribute fields before it).
template<typename T, size_t I>
inline constexpr size_t ATTR_ORDINAL = [] {
  constexpr auto kinds = makeFieldKinds<T>();
  size_t k = 0;
  for (size_t i = 0; i < I; ++i) {
    if (kinds[i] == FieldKind::Attr) {
      ++k;
    }
  }
  return k;
}();

/// @brief Name hashes of T's attribute fields, indexed by attribute ordinal.
template<typename T>
constexpr auto makeAttrHashesByOrdinal() noexcept {
  constexpr auto ords = makeAttrOrdinals<T>();
  constexpr auto hashes = makeFieldHashes<T>();
  std::array<FieldHash, ords.size()> out{};
  for (size_t k = 0; k < N_ATTR_FIELDS<T>; ++k) {
    out[k] = hashes[ords[k]];
  }
  return out;
}

/// @brief "name=" byte pattern of T's K-th attribute field (by ordinal):
/// matching it plus the following quote with one constant-length compare is
/// the streamed-attribute fast path. The '=' terminator makes prefix
/// collisions ("id" vs "idx") impossible.
template<typename T, size_t K>
inline constexpr auto ATTR_PATTERN = [] {
  constexpr auto ords = makeAttrOrdinals<T>();
  constexpr std::string_view name = makeFieldNames<T>()[ords[K]];
  std::array<char, name.size() + 1> pat{};
  for (size_t i = 0; i < name.size(); ++i) {
    pat[i] = name[i];
  }
  pat[name.size()] = '=';
  return pat;
}();

/// @brief Whether attributes of an element deserializing into E take the
/// streamed typed capture path (single pass, no attributes_ materialization)
/// instead of the vector + dispatch pass. Strict mode streams too: its value
/// checks run inline per ordinal, and any list that deviates from the
/// schema-ordered shape rewinds to parseAttributes, where the duplicate and
/// well-formedness semantics live.
template<typename E, bool STRICT_MODE>
constexpr auto streamsAttrs() noexcept -> bool {
  if constexpr (!XmlObject<E>) {
    return false;
  } else {
    return HAS_ATTR_FIELDS<E> && N_ATTR_FIELDS<E> <= 32;
  }
}

/// @brief True if any container field of T stores into a fixed container
/// (std::array); gates the per-pull fill-counter array.
template<typename T>
inline constexpr bool HAS_FIXED_CONTAINER_FIELDS = anyFieldSatisfies<T>([](const auto& f, auto m) {
  using F = std::remove_cvref_t<decltype(f)>;
  using M = typename decltype(m)::type;
  if constexpr (F::kind == FieldKind::Container) {
    return XmlFixedContainer<M>;
  } else {
    return false;
  }
});

/// @brief Index of T's first value field (only meaningful when one exists).
template<typename T>
inline constexpr size_t VALUE_FIELD_INDEX = firstFieldIndexIf<T>(isKind(FieldKind::Value));

/// @brief Index of the first child-element field, or 0 if none.
template<typename T>
inline constexpr size_t FIRST_ELEM_INDEX =
    HAS_ELEMENT_FIELDS<T> ? firstFieldIndexIf<T>(isElementKind) : 0;

/// @brief Cyclic successor table over child-element fields: entry i holds
/// the index of the next element/container field after i (itself if it is
/// the only one). Drives the document-order hint in Parser::pull().
template<typename T>
constexpr auto makeNextElemTable() noexcept {
  constexpr auto kinds = makeFieldKinds<T>();
  constexpr size_t n = kinds.size();
  std::array<size_t, (n != 0 ? n : 1)> next{};
  for (size_t i = 0; i < n; ++i) {
    next[i] = i;
    for (size_t step = 1; step <= n; ++step) {
      const size_t j = (i + step) % n;
      if (isElementKind(kinds[j])) {
        next[i] = j;
        break;
      }
    }
  }
  return next;
}

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
template<typename T>
constexpr auto variantMatcherCount() noexcept -> size_t {
  size_t n = 0;
  [&]<size_t... I>(std::index_sequence<I...>) {
    ([&] {
      constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
      if constexpr (f.kind == FieldKind::Variant) {
        n += f.names.size();
      }
    }(), ...);
  }(FIELD_SEQ<T>);
  return n;
}

/// @brief One matcher per variant alternative, in declaration then alternative
/// order. Indexes the handler table built by buildVariantDispatch.
template<typename T>
constexpr auto makeVariantMatchers() noexcept {
  std::array<VariantMatcher, variantMatcherCount<T>()> out{};
  size_t w = 0;
  [&]<size_t... I>(std::index_sequence<I...>) {
    ([&] {
      constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
      if constexpr (f.kind == FieldKind::Variant) {
        for (size_t a = 0; a < f.names.size(); ++a) {
          out[w++] = {f.hashes[a], I, a};
        }
      }
    }(), ...);
  }(FIELD_SEQ<T>);
  return out;
}

// Rejects FNV-1a collisions among named fields and variant alternatives, which
// would otherwise alias during hash dispatch.
template<typename T>
constexpr auto allNamesUnique() noexcept -> bool {
  constexpr auto hashes = makeFieldHashes<T>();
  constexpr auto kinds = makeFieldKinds<T>();
  for (size_t i = 0; i < hashes.size(); ++i) {
    if (!isNamedField(kinds[i])) {
      continue;
    }
    for (size_t j = i + 1; j < hashes.size(); ++j) {
      if (isNamedField(kinds[j]) && hashes[i] == hashes[j]) {
        return false;
      }
    }
  }
  constexpr auto vm = makeVariantMatchers<T>();
  for (size_t i = 0; i < vm.size(); ++i) {
    for (size_t j = i + 1; j < vm.size(); ++j) {
      if (vm[i].hash == vm[j].hash) {
        return false;
      }
    }
    for (size_t j = 0; j < hashes.size(); ++j) {
      if (isNamedField(kinds[j]) && hashes[j] == vm[i].hash) {
        return false;
      }
    }
  }
  return true;
}

template<typename T>
constexpr auto optionalsNotRequired() noexcept -> bool {
  return !anyFieldSatisfies<T>([](const auto& f, auto m) {
    return f.required && IsOptional<typename decltype(m)::type>::value;
  });
}

/// @brief How a run of character data is normalized when appended to an owning
/// std::string field. Reference expansion and line-ending normalization only
/// run on this path; std::string_view fields stay raw and zero-copy.
enum class NormMode : uint8_t {
  Text,   ///< Element text: expand references, normalize CR/CRLF -> LF.
  Attr,   ///< Attribute value: as Text, plus literal whitespace -> single space.
  CData,  ///< CDATA content: normalize line endings only; '&' stays literal.
};

/// @brief Appends the UTF-8 encoding of code point cp to out. Returns false if
/// cp is not a valid XML character (out of range, a surrogate, or a forbidden
/// control character), per the Char production [2].
inline auto encodeUtf8(std::string& out, uint32_t cp) -> bool {
  const bool valid = cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) ||
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
inline auto expandReference(std::string& out, std::string_view s, size_t& i) -> ErrorCode {
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
    if (digits.empty() || r.ec != std::errc() || r.ptr != last || !encodeUtf8(out, cp)) {
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
inline auto normSpecialTable(NormMode mode) noexcept -> const std::array<bool, 256>& {
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

/// @brief Shortest run worth scanning with memchr; below this the plain
/// byte-table loop wins under the memchr call overhead.
inline constexpr size_t MEMCHR_MIN_RUN = 32;

/// @brief Offset of the first byte in [p, p+n) that `special` marks, or n if
/// the run is ordinary. `needles` lists exactly the marked bytes. Long runs are
/// scanned with one memchr per needle, short runs use the byte-table loop.
template<size_t N>
inline auto findSpecial(const char* p, size_t n, const std::array<bool, 256>& special,
                        const std::array<char, N>& needles) noexcept -> size_t {
  if (n < MEMCHR_MIN_RUN) {
    size_t j = 0;
    while (j < n && !special[static_cast<unsigned char>(p[j])]) {
      ++j;
    }
    return j;
  }
  size_t limit = n;
  for (const char needle : needles) {
    if (const auto* hit = static_cast<const char*>(std::memchr(p, needle, limit))) {
      limit = static_cast<size_t>(hit - p);
    }
  }
  return limit;
}

/// @brief Position of the first byte in [base+i, base+n) that appendNormalized
/// must handle specially under `mode`, or n if the run is ordinary. Long runs
/// are scanned with memchr per special byte (each pass bounded by the earliest
/// hit so far, so total work stays proportional to the run); short runs use the
/// byte-table loop. Kept as inline mode-branches rather than dispatching to
/// findSpecial: the extra dispatch measurably regressed strict/normalizing
/// parses of short mixed content.
inline auto findNormSpecial(const char* base, size_t i, size_t n, NormMode mode,
                            const std::array<bool, 256>& special) noexcept -> size_t {
  const size_t rem = n - i;
  if (rem < MEMCHR_MIN_RUN) {
    size_t j = i;
    while (j < n && !special[static_cast<unsigned char>(base[j])]) {
      ++j;
    }
    return j;
  }
  const char* const p = base + i;
  const char* stop = nullptr;
  if (mode != NormMode::CData) {
    stop = static_cast<const char*>(std::memchr(p, '&', rem));
  }
  size_t limit = stop != nullptr ? static_cast<size_t>(stop - p) : rem;
  if (const auto* cr = static_cast<const char*>(std::memchr(p, '\r', limit))) {
    stop = cr;
    limit = static_cast<size_t>(cr - p);
  }
  if (mode == NormMode::Attr) {
    if (const auto* nl = static_cast<const char*>(std::memchr(p, '\n', limit))) {
      stop = nl;
      limit = static_cast<size_t>(nl - p);
    }
    if (const auto* tab = static_cast<const char*>(std::memchr(p, '\t', limit))) {
      stop = tab;
    }
  }
  return stop != nullptr ? i + static_cast<size_t>(stop - p) : n;
}

/// @brief Appends `raw` to `out` under the given NormMode. Returns the first
/// error encountered (None on success). CData never errors.
///
/// Ordinary bytes (the overwhelming majority of typical content) are copied in
/// bulk runs via std::string::append; only reference starts and line-ending /
/// whitespace bytes are handled one at a time.
inline auto appendNormalized(std::string& out, std::string_view raw, NormMode mode) -> ErrorCode {
  const std::array<bool, 256>& special = normSpecialTable(mode);
  const char* const base = raw.data();
  const size_t n = raw.size();
  out.reserve(out.size() + n);
  size_t i = 0;
  while (i < n) {
    // Copy the run of ordinary bytes up to the next byte needing attention.
    const size_t j = findNormSpecial(base, i, n, mode, special);
    if (j > i) {
      out.append(base + i, j - i);
      i = j;
      if (i == n) {
        break;
      }
    }
    const char c = base[i];
    if (c == '&') {  // marked special only for Text/Attr
      if (const ErrorCode ec = expandReference(out, raw, i); ec != ErrorCode::None) {
        return ec;
      }
      continue;  // expandReference advanced i past ';'
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

/// @brief Emplaces a slot in a dynamic container, populates it via `read`,
/// and rolls the slot back on failure. Shared by the container, variant, and
/// list read paths.
template<typename Container, typename Read>
auto readIntoNew(Container& c, Read read) -> bool {
  auto& slot = XmlContainerTraits<Container>::emplace(c);
  if (!read(slot)) {
    XmlContainerTraits<Container>::pop(c);
    return false;
  }
  return true;
}

/// @brief Applies fn to each item of a fixed or dynamic container (read-only).
/// Shared by the serializer and validate().
template<typename M, typename Fn>
auto forEachItem(const M& container, Fn fn) -> void {
  if constexpr (XmlFixedContainer<M>) {
    using Traits = XmlContainerTraits<M>;
    for (size_t i = 0; i < Traits::capacity; ++i) {
      fn(Traits::at(container, i));
    }
  } else {
    for (const auto& v : container) {
      fn(v);
    }
  }
}

}  // namespace detail

/// @brief Maps xmlight::Date to/from the XSD `date` lexical form.
template<>
struct XmlValueTraits<Date> {
  [[nodiscard]] static auto parse(std::string_view s, Date& d) -> bool {
    return detail::dtParse(s, d, [](detail::DtCursor& c, Date& o) {
      return c.date(o) && c.timezone(o.tz);
    });
  }
  static auto format(std::string& out, const Date& d) -> void {
    detail::dtFmtDate(out, d);
    detail::dtFmtTz(out, d.tz);
  }
};

/// @brief Maps xmlight::Time to/from the XSD `time` lexical form.
template<>
struct XmlValueTraits<Time> {
  [[nodiscard]] static auto parse(std::string_view s, Time& t) -> bool {
    return detail::dtParse(s, t, [](detail::DtCursor& c, Time& o) {
      return c.time(o) && c.timezone(o.tz);
    });
  }
  static auto format(std::string& out, const Time& t) -> void {
    detail::dtFmtTime(out, t);
    detail::dtFmtTz(out, t.tz);
  }
};

/// @brief Maps xmlight::DateTime to/from the XSD `dateTime` lexical form.
template<>
struct XmlValueTraits<DateTime> {
  [[nodiscard]] static auto parse(std::string_view s, DateTime& dt) -> bool {
    return detail::dtParse(s, dt, [](detail::DtCursor& c, DateTime& o) {
      return c.date(o.date) && c.eat('T') && c.time(o.time) && c.timezone(o.time.tz);
    });
  }
  static auto format(std::string& out, const DateTime& dt) -> void {
    detail::dtFmtDate(out, dt.date);
    out.push_back('T');
    detail::dtFmtTime(out, dt.time);
    detail::dtFmtTz(out, dt.time.tz);
  }
};

/// @brief Creates an optional variant (xs:choice) field. The member is a
/// `std::variant<...>` (exactly one branch) or a dynamic container of variant
/// (a repeated/interleaved choice). Each `alt<T>("name")` binds an element name
/// to the alternative of type T; alternatives must be distinct types.
/// @code
/// std::variant<Circle, Square> shape;  // member
/// xmlight::variantField(&Shapes::shape, xmlight::alt<Circle>("circle"),
///                                    xmlight::alt<Square>("square"));
/// @endcode
template<typename C, typename Member, typename... Ts>
constexpr auto variantField(Member C::*m, VariantAlt<Ts>... alts) {
  return detail::makeVariantField(m, false, alts...);
}

/// @brief Like variantField, but required: deserialize() fails with
/// MissingRequiredField if no alternative is matched (xs:choice minOccurs>=1).
template<typename C, typename Member, typename... Ts>
constexpr auto requiredVariantField(Member C::*m, VariantAlt<Ts>... alts) {
  return detail::makeVariantField(m, true, alts...);
}

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
template<ParserOptions Opts = ParserOptions{}>
class BasicParser {
 public:
  /// @brief Maximum element nesting depth (descent and skip) before
  /// ErrorCode::DepthExceeded. Guards against stack exhaustion on hostile
  /// input.
  static constexpr int MAX_DEPTH = 256;

  /// @brief Maximum number of attributes accepted on a single start-tag before
  /// ErrorCode::TooManyAttributes. Bounds attribute-storage amplification.
  static constexpr size_t MAX_ATTRIBUTES_PER_ELEMENT = 1U << 16;

  /// @brief Mirrors ParserOptions::normalize for this instantiation. When set
  /// (NormalizingParser), owning std::string fields receive normalized,
  /// reference-expanded text: the five predefined entities and numeric
  /// character references are expanded, CR/CRLF are normalized to LF, and
  /// attribute whitespace is normalized to spaces. std::string_view fields are
  /// always raw zero-copy regardless. Off by default (Parser), which compiles
  /// the normalization paths away entirely.
  static constexpr bool NORMALIZE = Opts.normalize;

  /// @brief Mirrors ParserOptions::strict. When set (StrictParser), the parser
  /// enforces well-formedness constraints the fast path otherwise skips for
  /// speed: "]]>" in character data, '<' in attribute values, and duplicate
  /// attribute names are rejected. Off by default (Parser); the checks compile
  /// away entirely.
  static constexpr bool STRICT = Opts.strict;

  /// @brief Constructs a parser over src. src must outlive the Parser.
  explicit BasicParser(std::string_view src) noexcept
      : cur_(src.data()), end_(src.data() + src.size()), src_(src) {
    skipBom();
  }

  BasicParser(const BasicParser&) = delete;
  auto operator=(const BasicParser&) -> BasicParser& = delete;
  BasicParser(BasicParser&&) noexcept = default;
  auto operator=(BasicParser&&) noexcept -> BasicParser& = default;

  template<ParserOptions O, XmlObject T>
  friend auto deserialize(BasicParser<O>& parser, std::string_view root_name, T& object) -> bool;

  /// @brief Resets the parser to the beginning of the source string.
  auto reset() -> void {
    cur_ = src_.data();
    end_ = src_.data() + src_.size();
    has_peek_ = false;
    attributes_.clear();
    attr_have_ = 0;
    attr_streamed_ = false;
    error_code_ = ErrorCode::None;
    last_self_closing_ = false;
    skipBom();
  }

  /// @brief Reason the most recent parse failed, or None if it succeeded.
  [[nodiscard]] auto errorCode() const noexcept -> ErrorCode { return error_code_; }

 private:
  [[nodiscard]] auto next() -> const Token*;
  [[nodiscard]] auto peek() -> const Token*;

  template<typename T>
  [[nodiscard]] auto attr(FieldHash hash, T& out, size_t& pos) -> bool;

  [[nodiscard]] auto beginElement(std::string_view expected_name) -> bool;
  template<size_t NAME_LEN = 0>
  [[nodiscard]] auto endElement(std::string_view expected_name) -> bool;

  template<typename T>
  static auto parseNumeric(std::string_view text, T& out) noexcept -> bool;

  template<typename T>
  static auto parseScalar(std::string_view text, T& out) -> bool {
    if constexpr (XmlEnum<T>) {
      return detail::enumFromString(text, out);
    } else if constexpr (XmlCustomValue<T>) {
      return XmlValueTraits<T>::parse(text, out);
    } else {
      return parseNumeric(text, out);
    }
  }

  template<typename T>
  static constexpr auto scalarError() noexcept -> ErrorCode {
    if constexpr (XmlEnum<T>) {
      return ErrorCode::InvalidEnumValue;
    } else if constexpr (XmlCustomValue<T>) {
      return ErrorCode::InvalidValue;
    } else {
      return ErrorCode::InvalidNumericValue;
    }
  }

  [[nodiscard]] auto atEnd() const noexcept -> bool { return cur_ >= end_; }

  [[nodiscard]] auto peekChar() const noexcept -> char { return atEnd() ? '\0' : *cur_; }

  auto skipWhitespace() noexcept -> void {
    while (!atEnd() && detail::isSpace(*cur_)) [[likely]] {
      ++cur_;
    }
  }

  // Skips a UTF-8 BOM (\xEF\xBB\xBF) at the current position if present.
  auto skipBom() noexcept -> void {
    if (end_ - cur_ >= 3 && static_cast<uint8_t>(cur_[0]) == 0xEF &&
        static_cast<uint8_t>(cur_[1]) == 0xBB && static_cast<uint8_t>(cur_[2]) == 0xBF) {
      cur_ += 3;
    }
  }

  // Skips past the closing '>' of a markup declaration that began with '<!'.
  // cur_ must point to the character immediately after '<!'. Handles internal
  // subsets ('[' ... ']'), quoted literals, and nested comments so that '>'
  // characters inside those contexts do not terminate the scan prematurely.
  auto skipBangDecl() noexcept -> void {
    int bracket_depth = 0;
    while (cur_ < end_) {
      const char c = *cur_++;
      switch (c) {
        case '[':
          ++bracket_depth;
          break;
        case ']':
          if (bracket_depth > 0) {
            --bracket_depth;
          }
          break;
        case '>':
          if (bracket_depth == 0) {
            return;
          }
          break;
        case '"':
        case '\'':
          while (cur_ < end_) {
            if (*cur_++ == c) {
              break;
            }
          }
          break;
        case '<':
          if (cur_ + 2 < end_ && cur_[0] == '!' && cur_[1] == '-' && cur_[2] == '-') {
            cur_ += 3;
            skipPast("-->");
          } else if (cur_ < end_ && *cur_ == '?') {
            ++cur_;
            skipPast("?>");
          }
          break;
        default:
          break;
      }
    }
    cur_ = end_;
  }

  auto parseName(std::string_view& prefix, std::string_view& local,
                 FieldHash& local_hash) noexcept -> void {
    prefix = {};
    if (atEnd() || !detail::isNameStart(*cur_)) [[unlikely]] {
      local = {};
      local_hash = detail::FNV_OFFSET;
      return;
    }
    const char* start = cur_;
    const char* local_start = start;
    FieldHash hash = detail::FNV_OFFSET;

    while (!atEnd() && detail::isNameChar(*cur_)) {
      if (*cur_ == ':') {
        prefix = {start, static_cast<size_t>(cur_ - start)};
        local_start = cur_ + 1;
        hash = detail::FNV_OFFSET;  // reset; hash only the local part
      } else {
        hash = detail::fnv1aStep(hash, static_cast<unsigned char>(*cur_));
      }
      ++cur_;
    }
    local = {local_start, static_cast<size_t>(cur_ - local_start)};
    local_hash = hash;
  }

  [[nodiscard]] auto expect(char c) noexcept -> bool {
    if (atEnd() || *cur_ != c) {
      return false;
    }
    ++cur_;
    return true;
  }

  [[nodiscard]] auto startsWith(std::string_view s) const noexcept -> bool {
    return std::string_view{cur_, static_cast<size_t>(end_ - cur_)}.starts_with(s);
  }

  // Records the first error (later cascading failures don't overwrite it) and
  // flags the parser stopped. Returns false so callers can `return fail(code)`.
  auto fail(ErrorCode code) noexcept -> bool {
    if (!error()) [[likely]] {
      error_code_ = code;
    }
    return false;
  }

  // Assigns one text run to a string field: normalized into an owning
  // std::string under NormalizingParser, else the raw zero-copy view.
  template<typename T>
  auto assignValue(T& out, std::string_view text) -> bool {
    if constexpr (NORMALIZE && std::same_as<T, std::string>) {
      out.clear();
      const ErrorCode ec = detail::appendNormalized(out, text, detail::NormMode::Text);
      return ec == ErrorCode::None ? true : fail(ec);
    } else {
      out = text;
      return true;
    }
  }

  // Assigns a matched attribute's value to a leaf member (string normalized
  // when applicable, otherwise a scalar/enum/custom-value parse), or splits an
  // xs:list value into a container member. The string arm stays inline here
  // (not routed through assignValue) deliberately: it needs NormMode::Attr,
  // and the extra call layer measurably regressed attribute-heavy parses.
  template<typename U>
  auto assignAttrValue(U& out, std::string_view value) -> bool {
    if constexpr (XmlListContainer<U>) {
      using V = typename XmlContainerTraits<U>::value_type;
      if constexpr (NORMALIZE_LIST<V>) {
        return splitNormalized<detail::NormMode::Attr>(out, value);
      } else {
        return splitInto<false>(out, value);
      }
    } else if constexpr (XmlStringLike<U>) {
      if constexpr (NORMALIZE && std::same_as<U, std::string>) {
        out.clear();
        const ErrorCode ec = detail::appendNormalized(out, value, detail::NormMode::Attr);
        // On a bad reference, fail() records the code; the attribute reports
        // "not matched" and pull() converts the recorded error into a hard fail
        // right after attribute dispatch.
        return ec == ErrorCode::None ? true : fail(ec);
      } else {
        out = value;
        return true;
      }
    } else {
      return parseScalar(value, out);
    }
  }

  // Assigns a matched attribute's raw value to the member with the
  // optional-temporary and error-conversion semantics of attribute matching.
  // Shared by the vector dispatch (attr) and the streamed dispatch so both
  // produce byte-identical normalization and error codes.
  template<typename T>
  auto assignAttrChecked(std::string_view value, T& out) -> bool {
    if constexpr (XmlOptional<T>) {
      // Parse into a temporary so a parse failure leaves the optional empty
      typename T::value_type tmp{};
      if (!assignAttrValue(tmp, value)) {
        return false;
      }
      out = std::move(tmp);
      return true;
    } else {
      // The attribute is present; a parse failure here is a malformed value
      if (!assignAttrValue(out, value)) {
        fail(scalarError<T>());
        return false;
      }
      return true;
    }
  }

  // Assigns one whitespace-split list token to a container slot: a string
  // assignment for string-like items, else a scalar/enum/custom-value parse.
  // Pre-normalized tokens (Normalized == true, see splitNormalized) alias the
  // scratch buffer and are copied raw; assignValue would re-expand their '&'s.
  template<bool Normalized, typename V>
  auto assignToken(V& out, std::string_view tok) -> bool {
    if constexpr (XmlStringLike<V>) {
      if constexpr (Normalized) {
        out = V{tok};
        return true;
      } else {
        return assignValue(out, tok);
      }
    } else {
      return parseScalar(tok, out) ? true : fail(scalarError<V>());
    }
  }

  // True when list values for item type V must be reference-expanded before
  // splitting. string_view items always stay raw zero-copy (a view cannot hold
  // transformed bytes), matching the string-field contract.
  template<typename V>
  static constexpr bool NORMALIZE_LIST = NORMALIZE && !std::same_as<V, std::string_view>;

  // Normalizes a raw list value into the scratch buffer under the given mode,
  // then splits it. Tokens alias scalar_buf_ and are assigned pre-normalized.
  template<detail::NormMode MODE, typename Container>
  auto splitNormalized(Container& out, std::string_view raw) -> bool {
    scalar_buf_.clear();
    if (const ErrorCode ec = detail::appendNormalized(scalar_buf_, raw, MODE);
        ec != ErrorCode::None) {
      return fail(ec);
    }
    return splitInto<true>(out, scalar_buf_);
  }

  // Splits an xs:list value on XML whitespace and parses each token into the
  // container: dynamic containers grow per token; fixed containers fill
  // sequentially and skip overflow (as arrField does).
  template<bool Normalized, typename Container>
  auto splitInto(Container& out, std::string_view text) -> bool {
    using Traits = XmlContainerTraits<Container>;
    auto each_token = [text](auto handle) -> bool {
      size_t i = 0;
      while (i < text.size()) {
        while (i < text.size() && detail::isSpace(text[i])) {
          ++i;
        }
        const size_t start = i;
        while (i < text.size() && !detail::isSpace(text[i])) {
          ++i;
        }
        if (i == start) {
          break;
        }
        if (!handle(text.substr(start, i - start))) {
          return false;
        }
      }
      return true;
    };
    if constexpr (XmlFixedContainer<Container>) {
      size_t fill = 0;
      return each_token([this, &out, &fill](std::string_view tok) {
        if (fill < Traits::capacity) {
          if (!assignToken<Normalized>(Traits::at(out, fill++), tok)) {
            return false;
          }
        }
        return true;
      });
    } else {
      return each_token([this, &out](std::string_view tok) {
        return detail::readIntoNew(out, [this, tok](typename Traits::value_type& slot) {
          return assignToken<Normalized>(slot, tok);
        });
      });
    }
  }

  template<typename Container>
  auto readList(Container& out, std::string_view expected_name) -> bool {
    using V = typename XmlContainerTraits<Container>::value_type;
    if constexpr (NORMALIZE_LIST<V>) {
      // readChardata's owning branch expands references into scalar_buf_;
      // the tokens are then assigned pre-normalized.
      return readChardata<true>(scalar_buf_, expected_name) && splitInto<true>(out, scalar_buf_);
    } else {
      std::string_view text;
      return readChardata<true>(text, expected_name) && splitInto<false>(out, text);
    }
  }

  // Validates and consumes a readChardata closing tag. ConsumeClose drives the
  // two callers: value() consumes + name-checks the close; the valueField path
  // leaves it peeked for readElement's endElement().
  template<bool ConsumeClose>
  auto finishChardata(const Token& close, std::string_view expected_name) -> bool {
    if constexpr (ConsumeClose) {
      if (close.name != expected_name) {
        return fail(ErrorCode::ElementMismatch);
      }
      consumePeeked();
    }
    return true;
  }

  // Reads an element's character data into a leaf member: normalized into an
  // owning std::string, the raw last run into a string_view, or parsed for a
  // numeric/enum/custom value. Stops at the close tag (see finishChardata).
  template<bool ConsumeClose, typename M>
  auto readChardata(M& out, std::string_view expected_name) -> bool {
    constexpr bool OWNING = NORMALIZE && std::same_as<M, std::string>;
    if constexpr (OWNING) {
      out.clear();
    } else if constexpr (!XmlStringLike<M>) {
      scalar_buf_.clear();
    }
    // Both mutate only in some constexpr branches, so const would not compile
    // in every instantiation.
    [[maybe_unused]] std::string_view text;  // NOLINT(misc-const-correctness)
    [[maybe_unused]] bool buffered = false;  // NOLINT(misc-const-correctness)
    while (const Token* tok = peek()) {
      switch (tok->type) {
        case TokenType::Text:
        case TokenType::CData: {
          [[maybe_unused]] const detail::NormMode mode =
              tok->type == TokenType::CData ? detail::NormMode::CData : detail::NormMode::Text;
          if constexpr (OWNING) {
            if (const ErrorCode ec = detail::appendNormalized(out, tok->data, mode);
                ec != ErrorCode::None) {
              return fail(ec);
            }
          } else if constexpr (XmlStringLike<M>) {
            text = tok->data;  // raw view: the last run wins
          } else if constexpr (NORMALIZE) {
            if (const ErrorCode ec = detail::appendNormalized(scalar_buf_, tok->data, mode);
                ec != ErrorCode::None) {
              return fail(ec);
            }
            buffered = true;
          } else if (text.empty() && !buffered) {
            text = tok->data;  // first run: keep the zero-copy source view
          } else {
            if (!buffered) {
              scalar_buf_.assign(text.data(), text.size());
              buffered = true;
            }
            scalar_buf_.append(tok->data);
          }
          consumePeeked();
          break;
        }
        case TokenType::ElementClose:
          if (!finishChardata<ConsumeClose>(*tok, expected_name)) {
            return false;
          }
          if constexpr (OWNING) {
            return true;
          } else if constexpr (XmlStringLike<M>) {
            out = text;
            return true;
          } else {
            if (buffered) {
              text = scalar_buf_;
            }
            return parseScalar(text, out) ? true : fail(scalarError<M>());
          }
        case TokenType::Error:
          return false;
        default:
          consumePeeked();
          break;
      }
    }
    return fail(ErrorCode::UnexpectedEof);
  }

  auto makeError(Token& token, ErrorCode code) noexcept -> bool {
    token.type = TokenType::Error;
    return fail(code);
  }

  /// @brief Whether the parser has encountered an error.
  /// @return true if `error_code_` is not `ErrorCode::None`.
  [[nodiscard]] auto error() const noexcept -> bool { return error_code_ != ErrorCode::None; }

  // Record self-closing state after consuming an ElementOpen.
  auto updateSelfClosing() noexcept -> void {
    if (current_token_.type == TokenType::ElementOpen) {
      last_self_closing_ = current_token_.self_closing;
    }
  }

  auto consume() -> void { std::ignore = next(); }

  // Branch-free consume when we know a peek() just succeeded.
  auto consumePeeked() noexcept -> void {
    has_peek_ = false;
    updateSelfClosing();
  }

  // Consume the peeked element and skip its entire subtree.
  auto skipCurrent() -> void {
    consumePeeked();
    skipElement();
  }

  auto parseMarkup(Token& token) -> bool;
  auto parseElementOpen(Token& token) -> bool;
  [[nodiscard]] auto parseAttributes(bool& self_closing) -> bool;
  auto parseElementClose(Token& token) -> bool;
  auto nextFromSource(Token& token) -> bool;
  auto skipElement() -> void;

  // Matches "</name>" (whitespace allowed before '>') starting at p. Returns
  // the position just past '>' on a match, nullptr otherwise. Shared by
  // endElement's and readElement's close-tag fast paths.
  [[nodiscard]] auto matchCloseTag(const char* p, std::string_view name) const noexcept -> const
      char* {
    const size_t name_len = name.size();
    if (static_cast<size_t>(end_ - p) < name_len + 3 || p[0] != '<' || p[1] != '/' ||
        std::memcmp(p + 2, name.data(), name_len) != 0) {
      return nullptr;
    }
    p += 2 + name_len;
    while (p < end_ && detail::isSpace(*p)) {
      ++p;
    }
    return (p < end_ && *p == '>') ? p + 1 : nullptr;
  }

  // Constant-length sibling of matchCloseTag for compile-time-known field
  // names: LEN as a template parameter folds the memcmp to inline word
  // compares instead of a libc call. Kept as a duplicate body so neither
  // path gains a dispatch layer.
  template<size_t LEN>
  [[nodiscard]] auto matchCloseTagN(const char* p, const char* name) const noexcept -> const char* {
    if (static_cast<size_t>(end_ - p) < LEN + 3 || p[0] != '<' || p[1] != '/' ||
        std::memcmp(p + 2, name, LEN) != 0) {
      return nullptr;
    }
    p += 2 + LEN;
    while (p < end_ && detail::isSpace(*p)) {
      ++p;
    }
    return (p < end_ && *p == '>') ? p + 1 : nullptr;
  }

  // First occurrence of `c` in [from, end_), or end_ if absent.
  [[nodiscard]] auto findByte(const char* from, char c) const noexcept -> const char* {
    const char* hit =
        static_cast<const char*>(std::memchr(from, c, static_cast<size_t>(end_ - from)));
    return hit != nullptr ? hit : end_;
  }

  // Whether [p, e) contains a control byte outside the Char production [2]
  // (below 0x20 and not tab/LF/CR). Used only by the strict path. UTF-8 lead
  // and continuation bytes are >= 0x80, so the byte scan cannot misfire inside
  // a multi-byte sequence; branchless accumulation keeps it vectorizable.
  [[nodiscard]] static auto containsForbiddenControl(const char* p,
                                                     const char* e) noexcept -> bool {
    static constexpr std::array<bool, 256> kForbidden = [] {
      std::array<bool, 256> t{};
      for (int c = 0; c < 0x20; ++c) {
        t[static_cast<size_t>(c)] = c != '\t' && c != '\n' && c != '\r';
      }
      return t;
    }();
    const auto scan = [](const char* q, const char* qe) noexcept {
      bool bad = false;
      for (; q < qe; ++q) {
        bad |= kForbidden[static_cast<unsigned char>(*q)];
      }
      return bad;
    };
    // Long runs: detect any sub-0x20 byte two words at a time and consult the
    // table only for chunks that contain one (in real documents that is the
    // occasional legal tab/LF/CR). Short runs and tails use the table loop,
    // mirroring the findSpecial short/long split.
    constexpr uint64_t SPACES = 0x2020202020202020ULL;
    constexpr uint64_t HIGH_BITS = 0x8080808080808080ULL;
    for (; e - p >= 16; p += 16) {
      uint64_t lo = 0;
      uint64_t hi = 0;
      std::memcpy(&lo, p, sizeof(lo));
      std::memcpy(&hi, p + 8, sizeof(hi));
      const uint64_t flags = ((lo - SPACES) & ~lo) | ((hi - SPACES) & ~hi);
      if ((flags & HIGH_BITS) != 0 && scan(p, p + 16)) {
        return true;
      }
    }
    if (e - p >= 8) {
      uint64_t v = 0;
      std::memcpy(&v, p, sizeof(v));
      if (((v - SPACES) & ~v & HIGH_BITS) != 0 && scan(p, p + 8)) {
        return true;
      }
      p += 8;
    }
    return scan(p, e);
  }

  // Whether [p, e) contains the CDATA-close delimiter "]]>". Used only by the
  // strict path (Production [14] forbids it in character data); memchr-driven
  // so the scan is fast when the parser opts into the check.
  [[nodiscard]] static auto containsCdataEnd(const char* p, const char* e) noexcept -> bool {
    while (p < e) {
      const char* hit = static_cast<const char*>(std::memchr(p, ']', static_cast<size_t>(e - p)));
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

  // Advances cur_ just past the next occurrence of `delim` and returns where
  // the delimiter began, or nullptr (leaving cur_ == end_) if absent.
  auto scanPast(std::string_view delim) noexcept -> const char* {
    const char first = delim[0];
    while (cur_ < end_) {
      cur_ = findByte(cur_, first);
      if (cur_ == end_) {
        break;
      }
      if (startsWith(delim)) {
        const char* hit = cur_;
        cur_ += delim.size();
        return hit;
      }
      ++cur_;
    }
    cur_ = end_;
    return nullptr;
  }

  // Scan forward until `delim` is found. Sets token.data to the content
  // before the delimiter and advances past it.
  auto scanToDelimiter(Token& token, std::string_view delim, ErrorCode ec) -> bool {
    const char* start = cur_;
    const char* hit = scanPast(delim);
    if (hit == nullptr) {
      return makeError(token, ec);
    }
    token.data = {start, static_cast<size_t>(hit - start)};
    return true;
  }

  auto skipPast(std::string_view delim) noexcept -> void { std::ignore = scanPast(delim); }

  // Comment ::= '<!--' ((Char - '-') | ('-' (Char - '-')))* '-->'
  // The WFC forbids "--" in content, so the first "--" must begin the "-->"
  // terminator; any other "--" is a fatal error.
  auto parseComment(Token& token) -> bool {
    token.type = TokenType::Comment;
    const char* start = cur_;
    while (cur_ < end_) {
      const char* hit = findByte(cur_, '-');
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
        if constexpr (STRICT) {
          if (containsForbiddenControl(start, hit)) {
            return makeError(token, ErrorCode::ForbiddenControlChar);
          }
        }
        token.data = {start, static_cast<size_t>(hit - start)};
        cur_ = hit + 3;
        return true;
      }
      return makeError(token, ErrorCode::MalformedComment);  // interior "--"
    }
    cur_ = end_;
    return makeError(token, ErrorCode::UnterminatedComment);
  }

  auto parseCdata(Token& token) -> bool {
    token.type = TokenType::CData;
    if (!scanToDelimiter(token, "]]>", ErrorCode::UnterminatedCData)) {
      return false;
    }
    if constexpr (STRICT) {
      if (containsForbiddenControl(token.data.data(), token.data.data() + token.data.size())) {
        return makeError(token, ErrorCode::ForbiddenControlChar);
      }
    }
    return true;
  }

  auto parsePi(Token& token) -> bool {
    parseName(token.prefix, token.name, token.name_hash);
    if (token.name.empty()) {
      return makeError(token, ErrorCode::ExpectedPiTarget);
    }
    // Production [17]: PITarget excludes every case variant of "xml". Exact
    // lowercase "xml" names the XML declaration; "XML", "Xml", ... are reserved
    // and ill-formed as PI targets. (Cheap: runs only on processing
    // instructions, never on element/attribute content.)
    if (token.name == "xml") {
      token.type = TokenType::XmlDeclaration;
    } else {
      if (token.prefix.empty() && isReservedXmlTarget(token.name)) {
        return makeError(token, ErrorCode::ReservedPiTarget);
      }
      token.type = TokenType::ProcessingInstruction;
    }
    skipWhitespace();
    if (!scanToDelimiter(token, "?>", ErrorCode::UnterminatedPi)) {
      return false;
    }
    if constexpr (STRICT) {
      if (containsForbiddenControl(token.data.data(), token.data.data() + token.data.size())) {
        return makeError(token, ErrorCode::ForbiddenControlChar);
      }
    }
    return true;
  }

  // True for a case-insensitive but not-exactly-lowercase match of "xml".
  [[nodiscard]] static auto isReservedXmlTarget(std::string_view name) noexcept -> bool {
    return name.size() == 3 && (name[0] | 0x20) == 'x' && (name[1] | 0x20) == 'm' &&
           (name[2] | 0x20) == 'l';
  }

  // Fast-path: match and consume "<name>", "<name/>", or (only when
  // WITH_ATTRS) "<name attr...>" without tokenisation (no namespace prefix).
  // Returns false to fall through to the normal tokenisation path; a
  // malformed attribute list records the code so the caller's next read
  // fails. WITH_ATTRS is compile-time gated by anyElementTargetHasAttrs so
  // attribute-free schemas keep this matcher minimal and noexcept. LEN is the
  // compile-time name length (field names are constexpr), so the memcmp folds
  // to inline word compares instead of a libc call. E is the element's target
  // type, forwarded to the streamed attribute capture when it qualifies.
  template<size_t LEN, bool WITH_ATTRS, typename E = void>
  [[nodiscard]] auto tryBeginElement(const char* name) noexcept(!WITH_ATTRS) -> bool {
    const auto avail = static_cast<size_t>(end_ - cur_);
    if (avail < LEN + 2 || cur_[0] != '<') {
      return false;
    }
    if (std::memcmp(cur_ + 1, name, LEN) != 0) {
      return false;
    }
    const char after = cur_[1 + LEN];
    if (after == '>') {
      cur_ += LEN + 2;
      last_self_closing_ = false;
      has_peek_ = false;
      attributes_.clear();
      return true;
    }
    if (after == '/' && avail > LEN + 2 && cur_[LEN + 2] == '>') {
      cur_ += LEN + 3;
      last_self_closing_ = true;
      has_peek_ = false;
      attributes_.clear();
      return true;
    }
    if constexpr (WITH_ATTRS) {
      if (detail::isSpace(after)) {
        return beginAttributedElement<E>(LEN);
      }
    }
    return false;
  }

  // Attributed-tag continuation of tryBeginElement: the name already matched,
  // so skip re-scanning and hashing it and go straight to the attribute list.
  // Outlined to keep tryBeginElement's inline body small. E is the type the
  // element deserializes into; when it qualifies, attributes are captured in
  // one streamed typed pass instead of the attributes_ vector.
  template<typename E = void>
  [[nodiscard]] auto beginAttributedElement(size_t name_len) -> bool {
    cur_ += 1 + name_len;
    has_peek_ = false;
    bool self_closing = false;
    if constexpr (detail::streamsAttrs<E, STRICT>()) {
      if (!streamAttrs<E>(self_closing)) {
        return false;
      }
    } else {
      attributes_.clear();
      if (!parseAttributes(self_closing)) {
        return false;
      }
    }
    last_self_closing_ = self_closing;
    return true;
  }

  // Scans a quoted attribute value starting at cur_ (just past the opening
  // quote). Returns the value end, or nullptr on unterminated input (error
  // recorded). Duplicates parseAttributes' probe-then-memchr scan so neither
  // hot path gains a dispatch layer.
  [[nodiscard]] auto scanAttrValue(char quote) -> const char* {
    // Values are typically short: probe the first bytes inline before paying
    // the memchr call overhead in findByte (short runs favor the byte loop).
    const char* val_end = cur_;
    const char* const probe_limit = (end_ - cur_ < 16) ? end_ : cur_ + 16;
    while (val_end != probe_limit && *val_end != quote) {
      ++val_end;
    }
    if (val_end == probe_limit && (val_end == end_ || *val_end != quote)) {
      val_end = findByte(val_end, quote);
    }
    if (val_end == end_) {
      fail(ErrorCode::UnterminatedAttributeValue);
      return nullptr;
    }
    return val_end;
  }

  // Matches E's K-th attribute as `name="` / `name='` at cur_ with one
  // constant-length compare. On a hit consumes through the opening quote and
  // returns the quote character; returns 0 on a miss (cur_ untouched).
  template<typename E, size_t K>
  [[nodiscard]] auto matchAttrPattern() noexcept -> char {
    constexpr auto& pat = detail::ATTR_PATTERN<E, K>;
    constexpr size_t LEN = pat.size();  // name + '='
    if (static_cast<size_t>(end_ - cur_) < LEN + 2) {
      return 0;
    }
    if (std::memcmp(cur_, pat.data(), LEN) != 0) {
      return 0;
    }
    const char quote = cur_[LEN];
    if (quote != '"' && quote != '\'') {
      return 0;
    }
    cur_ += LEN + 1;
    return quote;
  }

  // One step of streamAttrs' unrolled document-order phase: consume leading
  // whitespace, recognize the tag terminator, then match ordinal K's pattern
  // and capture its value. Returns 0 to advance to the next ordinal, 1 when
  // the tag closed, 2 when the ordered run broke (this attribute goes to the
  // generic loop), and -1 on a recorded error.
  template<typename E, size_t K>
  [[nodiscard]] auto streamAttrOrdinal(bool& self_closing, size_t& count) -> int {
    skipWhitespace();
    if (atEnd()) {
      fail(ErrorCode::UnclosedTag);
      return -1;
    }
    const char c = *cur_;
    if (c == '>') {
      ++cur_;
      return 1;
    }
    if (c == '/' && cur_ + 1 < end_ && *(cur_ + 1) == '>') {
      cur_ += 2;
      self_closing = true;
      return 1;
    }
    if (!detail::isNameStart(c)) {
      fail(ErrorCode::ExpectedAttributeName);
      return -1;
    }
    if (++count > MAX_ATTRIBUTES_PER_ELEMENT) [[unlikely]] {
      fail(ErrorCode::TooManyAttributes);
      return -1;
    }
    const char quote = matchAttrPattern<E, K>();
    if (quote == 0) {
      --count;  // the generic loop re-counts this attribute
      return 2;
    }
    const char* const val_start = cur_;
    const char* const val_end = scanAttrValue(quote);
    if (val_end == nullptr) {
      return -1;
    }
    if constexpr (STRICT) {
      // Same checks and order as parseAttributes; phase 1 visits attributes in
      // document order, so the recorded error code matches the vector path.
      if (findByte(val_start, '<') < val_end) {
        fail(ErrorCode::LtInAttributeValue);
        return -1;
      }
      if (containsForbiddenControl(val_start, val_end)) {
        fail(ErrorCode::ForbiddenControlChar);
        return -1;
      }
    }
    attr_vals_[K] = {val_start, static_cast<size_t>(val_end - val_start)};
    attr_have_ |= uint32_t{1} << K;
    cur_ = val_end + 1;
    return 0;
  }

  // Streamed typed attribute capture: scans a start-tag's attribute list,
  // matching names against E's compile-time attribute patterns in document
  // order and storing raw value views into attr_vals_ by ordinal. Names that
  // miss the ordered pattern (out-of-order, prefixed, whitespace around '=',
  // unknown) take the generic arm, which resyncs the ordinal cursor exactly
  // like attr()'s document-order fallback. First match wins, matching the
  // vector path's first-match scan. Strict mode has no generic arm: any
  // deviation rewinds and re-parses through parseAttributes so its duplicate
  // and well-formedness semantics apply verbatim. cur_ must be just past the
  // element name.
  template<typename E>
  [[nodiscard]] [[gnu::noinline]] auto streamAttrs(bool& self_closing) -> bool {
    constexpr size_t NA = detail::N_ATTR_FIELDS<E>;
    [[maybe_unused]] const char* const attrs_start = cur_;  // strict rewind point
    attr_have_ = 0;
    size_t count = 0;  // parsed attributes, for the element-wide bound

    // Phase 1: unrolled document-order matching — each ordinal's compare sits
    // at its own code position, so a schema-ordered attribute list runs
    // straight through with no runtime ordinal dispatch.
    int st = 0;
    [&]<size_t... K>(std::index_sequence<K...>) {
      std::ignore = ((st = streamAttrOrdinal<E, K>(self_closing, count), st == 0) && ...);
    }(std::make_index_sequence<NA>{});
    if (st == 1) {
      attr_streamed_ = true;
      return true;
    }
    if (st < 0) {
      return false;
    }

    if constexpr (STRICT) {
      // All ordinals matched (each fold step checks the terminator before its
      // pattern, so a fully-populated in-order list leaves '>' unconsumed):
      // consume the terminator and finish streamed.
      skipWhitespace();
      if (cur_ < end_ && *cur_ == '>') {
        ++cur_;
        attr_streamed_ = true;
        return true;
      }
      if (end_ - cur_ >= 2 && cur_[0] == '/' && cur_[1] == '>') {
        cur_ += 2;
        self_closing = true;
        attr_streamed_ = true;
        return true;
      }
      // The list deviates from the schema-ordered shape (out-of-order,
      // unknown, prefixed, whitespace around '=', duplicate): re-parse it
      // through parseAttributes, where strict's duplicate and well-formedness
      // checks live. Error codes match the vector path by construction.
      cur_ = attrs_start;
      attr_have_ = 0;
      attributes_.clear();
      return parseAttributes(self_closing);
    } else {
      // Phase 2: generic loop for whatever remains — out-of-order, prefixed,
      // whitespace-shaped, or unknown attributes (and any extras after all
      // ordinals matched). Matches by local-name hash; first match wins.
      while (true) {
        skipWhitespace();
        if (atEnd()) {
          return fail(ErrorCode::UnclosedTag);
        }
        const char c = *cur_;
        if (c == '>') {
          ++cur_;
          attr_streamed_ = true;
          return true;
        }
        if (c == '/' && cur_ + 1 < end_ && *(cur_ + 1) == '>') {
          cur_ += 2;
          self_closing = true;
          attr_streamed_ = true;
          return true;
        }
        if (!detail::isNameStart(c)) {
          return fail(ErrorCode::ExpectedAttributeName);
        }
        if (++count > MAX_ATTRIBUTES_PER_ELEMENT) [[unlikely]] {
          return fail(ErrorCode::TooManyAttributes);
        }
        std::string_view prefix;
        std::string_view name;
        FieldHash hash{};
        parseName(prefix, name, hash);
        char quote = 0;
        if (end_ - cur_ >= 2 && cur_[0] == '=' && (cur_[1] == '"' || cur_[1] == '\'')) {
          // Common shape: name="value" with no whitespace around '='.
          quote = cur_[1];
          cur_ += 2;
        } else {
          skipWhitespace();
          if (!expect('=')) {
            return fail(ErrorCode::ExpectedEquals);
          }
          skipWhitespace();
          quote = peekChar();
          if (quote != '"' && quote != '\'') {
            return fail(ErrorCode::ExpectedQuotedValue);
          }
          ++cur_;
        }
        const char* const val_start = cur_;
        const char* const val_end = scanAttrValue(quote);
        if (val_end == nullptr) {
          return false;
        }
        cur_ = val_end + 1;
        static constexpr auto ATTR_HASHES = detail::makeAttrHashesByOrdinal<E>();
        for (size_t j = 0; j < NA; ++j) {
          if (ATTR_HASHES[j] == hash) {
            const auto bit = uint32_t{1} << j;
            if ((attr_have_ & bit) == 0) {
              attr_vals_[j] = {val_start, static_cast<size_t>(val_end - val_start)};
              attr_have_ |= bit;
            }
            break;
          }
        }
        // Unknown attribute: value already consumed, nothing stored.
      }
    }
  }

  // Post-consume dispatch: opening tag already consumed, route to the correct
  // readElement. arr_fill tracks fill position for fixed containers.
  template<typename T, size_t I>
  static auto readField(BasicParser& p, T& obj, uint16_t depth, std::span<size_t> arr_fill,
                        detail::RequiredMaskT<T>& parsed) -> bool {
    // Reaching here means field I's element matched, so it is present. Record
    // it for the required-field check; gated so types with no required field
    // never touch parsed (the arg dead-codes away). Failure paths below still
    // return false and the mask is then irrelevant.
    if constexpr (detail::HAS_REQUIRED_FIELDS<T>) {
      parsed.set(I);
    }
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr || f.kind == FieldKind::Value ||
                  f.kind == FieldKind::Variant) {
      // Attr/value/variant fields aren't matched as child elements, so this arm
      // is unreachable; it only has to compile (the readFieldAt fold spans
      // every field index).
      p.skipElement();
      return true;
    } else if constexpr (f.kind == FieldKind::List) {
      if (p.last_self_closing_) {
        return true;  // <name/> -> empty list
      }
      return p.readList(obj.*(f.member), f.xml_name);
    } else if constexpr (f.kind == FieldKind::Container) {
      using M = std::decay_t<decltype(obj.*(f.member))>;
      if constexpr (XmlFixedContainer<M>) {
        using Traits = XmlContainerTraits<M>;
        if (arr_fill[I] < Traits::capacity) {
          if (!p.readElement<f.xml_name.size()>(
                  f.xml_name, Traits::at(obj.*(f.member), arr_fill[I]), depth + 1)) {
            return false;
          }
          ++arr_fill[I];
        } else {
          p.skipElement();
        }
      } else {
        static_assert(XmlDynContainer<M>,
                      "container member requires XmlContainerTraits specialization");
        return detail::readIntoNew(obj.*(f.member), [&p, depth](auto& elem) {
          constexpr auto& fld = std::get<I>(XmlMetadata<T>::fields);
          return p.readElement<fld.xml_name.size()>(fld.xml_name, elem, depth + 1);
        });
      }
      return true;
    } else {
      return p.readElement<f.xml_name.size()>(f.xml_name, obj.*(f.member), depth + 1);
    }
  }

  template<typename T, size_t I>
  static auto applyAttr(BasicParser& p, T& obj, size_t& pos,
                        detail::RequiredMaskT<T>& parsed) -> void {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      if (p.attr(f.hash, obj.*(f.member), pos)) {
        parsed.set(I);
      }
    }
  }

  // Streamed counterpart of applyAttr: the value view was captured by ordinal
  // in streamAttrs, so matching is a bit test instead of a hash scan.
  template<typename T, size_t I>
  static auto applyStreamedAttr(BasicParser& p, T& obj, detail::RequiredMaskT<T>& parsed) -> void {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      constexpr size_t K = detail::ATTR_ORDINAL<T, I>;
      if ((p.attr_have_ & (uint32_t{1} << K)) != 0 &&
          p.assignAttrChecked(p.attr_vals_[K], obj.*(f.member))) {
        parsed.set(I);
      }
    }
  }

  template<typename T, size_t... I>
  static auto dispatchStreamedAttrs(BasicParser& p, T& obj, detail::RequiredMaskT<T>& parsed,
                                    std::index_sequence<I...>) -> void {
    (applyStreamedAttr<T, I>(p, obj, parsed), ...);
  }

  // Direct dispatch to readField<T, I> for a runtime field index: a
  // compile-time fold of compares the inliner can see through, instead of an
  // opaque function-pointer table. idx must be < sizeof...(I).
  template<typename T, size_t... I>
  static auto readFieldAt(size_t idx, BasicParser& p, T& obj, uint16_t depth,
                          std::span<size_t> arr_fill, detail::RequiredMaskT<T>& parsed,
                          std::index_sequence<I...> /*seq*/) -> bool {
    bool ok = false;
    std::ignore =
        ((idx == I && (ok = readField<T, I>(p, obj, depth, arr_fill, parsed), true)) || ...);
    return ok;
  }

  // Fused hint dispatch: match the hinted field's open tag with a
  // constant-length compare and, on a hit, read the field in one step. Only
  // element kinds participate (the hint tables never point elsewhere; the
  // other arms exist solely so the fold compiles). `matched` reports the tag
  // hit; the return value is the read result (true when the tag missed).
  template<typename T, size_t I>
  static auto tryReadField(BasicParser& p, T& obj, uint16_t depth, std::span<size_t> arr_fill,
                           detail::RequiredMaskT<T>& parsed, bool& matched) -> bool {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (detail::isElementKind(f.kind)) {
      constexpr bool WITH_ATTRS = detail::ANY_ELEM_TARGET_HAS_ATTRS<T>;
      using M = std::decay_t<decltype(obj.*(f.member))>;
      // Type this field's element deserializes into: drives the streamed
      // attribute capture. List fields bind token containers, not objects.
      constexpr auto target = [] {
        if constexpr (f.kind == FieldKind::Container) {
          return std::type_identity<
              detail::ElementTargetT<typename XmlContainerTraits<M>::value_type>>{};
        } else if constexpr (f.kind == FieldKind::Element) {
          return std::type_identity<detail::ElementTargetT<M>>{};
        } else {
          return std::type_identity<void>{};
        }
      }();
      using E = typename decltype(target)::type;
      if (!p.tryBeginElement<f.xml_name.size(), WITH_ATTRS, E>(f.xml_name.data())) {
        return true;
      }
      matched = true;
      return readField<T, I>(p, obj, depth, arr_fill, parsed);
    } else {
      return true;
    }
  }

  template<typename T, size_t... I>
  static auto tryReadFieldAt(size_t hint, BasicParser& p, T& obj, uint16_t depth,
                             std::span<size_t> arr_fill, detail::RequiredMaskT<T>& parsed,
                             bool& matched, std::index_sequence<I...> /*seq*/) -> bool {
    bool ok = true;
    std::ignore =
        ((hint == I && (ok = tryReadField<T, I>(p, obj, depth, arr_fill, parsed, matched), true)) ||
         ...);
    return ok;
  }

  // Handle a matched variant alternative: field FieldI's element matched its
  // alternative AltJ. Emplaces that alternative (into the variant, or into a
  // freshly pushed slot for a repeated/container choice) and reads into it.
  template<typename T, size_t FieldI, size_t AltJ>
  static auto readVariant(BasicParser& p, T& obj, uint16_t depth,
                          detail::RequiredMaskT<T>& parsed) -> bool {
    constexpr auto& f = std::get<FieldI>(XmlMetadata<T>::fields);
    using Member = std::decay_t<decltype(obj.*(f.member))>;
    if constexpr (detail::HAS_REQUIRED_FIELDS<T>) {
      parsed.set(FieldI);
    }
    if constexpr (detail::IsVariant<Member>::value) {
      auto& var = obj.*(f.member);
      var.template emplace<AltJ>();
      return p.readElement(f.names[AltJ], std::get<AltJ>(var), depth + 1);
    } else {
      return detail::readIntoNew(obj.*(f.member), [&p, depth](auto& slot) {
        slot.template emplace<AltJ>();
        return p.readElement(std::get<FieldI>(XmlMetadata<T>::fields).names[AltJ],
                             std::get<AltJ>(slot), depth + 1);
      });
    }
  }

  template<typename T, size_t... K>
  static constexpr auto buildVariantDispatch(std::index_sequence<K...>) noexcept {
    using Handler = bool (*)(BasicParser&, T&, uint16_t, detail::RequiredMaskT<T>&);
    constexpr auto m = detail::makeVariantMatchers<T>();
    return std::array<Handler, sizeof...(K)>{&readVariant<T, m[K].field_index, m[K].alt_index>...};
  }

  template<typename T, size_t... I>
  static auto dispatchAttrs(BasicParser& p, T& obj, detail::RequiredMaskT<T>& parsed,
                            std::index_sequence<I...>) -> void {
    size_t pos = 0;  // document-order cursor over attributes_
    (applyAttr<T, I>(p, obj, pos, parsed), ...);
  }

  template<XmlObject T>
  auto pull(T& object, uint16_t depth) -> bool;

  // Opening tag already consumed by caller. NAME_LEN, when nonzero, is the
  // compile-time length of expected_name and routes close-tag matching
  // through the constant-length matcher.
  template<size_t NAME_LEN = 0, typename T>
  auto readElement(std::string_view expected_name, T& out, uint16_t depth) -> bool;

  // Hot cursor state first: every scan touches cur_/end_ and the flags, so
  // they share the object's first cache line instead of sitting behind the
  // large reused buffers.
  const char* cur_;
  const char* end_;
  uint32_t attr_have_{};  // ordinal bitset over attr_vals_ (streamed capture)
  ErrorCode error_code_{ErrorCode::None};
  bool last_self_closing_{};
  bool has_peek_{false};
  bool attr_streamed_{false};  // current element's attributes are in attr_vals_
  std::string_view src_;
  Token current_token_;
  std::vector<Attribute> attributes_;
  std::string scalar_buf_;  // scratch for comment/PI-split or normalized scalar leaves
  std::array<std::string_view, 32> attr_vals_;  // streamed raw values by ordinal
};

/// @brief Default parser: raw, zero-copy output, and the fast non-validating
/// path (no reference expansion, normalization, or strict WFC checks). This is
/// the common case.
using Parser = BasicParser<>;

/// @brief Normalizing parser: owning std::string fields receive
/// reference-expanded, line-ending- and attribute-normalized text. See
/// BasicParser::NORMALIZE.
using NormalizingParser = BasicParser<ParserOptions{.normalize = true}>;

/// @brief Fully-conforming parser: normalizes (expands entity/character
/// references, folds line endings, normalizes attribute whitespace into owning
/// std::string fields) AND enforces the well-formedness constraints the default
/// fast path skips for speed ("]]>" in character data, '<' in attribute values,
/// duplicate attribute names). Combines NORMALIZE and STRICT.
using StrictParser = BasicParser<ParserOptions{.normalize = true, .strict = true}>;

/// @brief Deserializes the root element from parser into object.
/// @tparam T        XmlObject type with an XmlMetadata specialization.
/// @param parser    Parser positioned at the start of the XML input.
/// @param root_name Expected root element name.
/// @param object    Output object to populate.
/// @return True on success, false on any parse or structure error.
template<ParserOptions Opts, XmlObject T>
[[nodiscard]] auto deserialize(BasicParser<Opts>& parser, std::string_view root_name,
                               T& object) -> bool {
  if (!parser.beginElement(root_name)) [[unlikely]] {
    // beginElement() may have hit a tokenizer error (code already set); only
    // attribute a plain "root missing/mismatched" when nothing else did.
    return parser.fail(ErrorCode::RootElementNotFound);
  }
  const bool is_self_closing = parser.last_self_closing_;
  if (!parser.pull(object, 1)) [[unlikely]] {
    return false;
  }
  if (!is_self_closing && !parser.endElement(root_name)) [[unlikely]] {
    return false;
  }
  return true;
}

template<ParserOptions Opts>
inline auto BasicParser<Opts>::peek() -> const Token* {
  if (!has_peek_) {
    if (!nextFromSource(current_token_)) {
      return nullptr;
    }
    has_peek_ = true;
  }
  if (current_token_.type == TokenType::Error) {
    return nullptr;
  }
  return &current_token_;
}

template<ParserOptions Opts>
inline auto BasicParser<Opts>::next() -> const Token* {
  if (!has_peek_ && !nextFromSource(current_token_)) {
    return nullptr;
  }
  has_peek_ = false;
  updateSelfClosing();
  if (current_token_.type == TokenType::Error) {
    return nullptr;
  }
  return &current_token_;
}

template<ParserOptions Opts>
inline auto BasicParser<Opts>::nextFromSource(Token& token) -> bool {
  if (error() || atEnd()) {
    return false;
  }
  if (*cur_ == '<') {
    ++cur_;
    return parseMarkup(token);
  }
  const char* start = cur_;
  cur_ = findByte(cur_, '<');
  if constexpr (STRICT) {
    if (containsCdataEnd(start, cur_)) {
      return makeError(token, ErrorCode::CDataEndInContent);
    }
    if (containsForbiddenControl(start, cur_)) {
      return makeError(token, ErrorCode::ForbiddenControlChar);
    }
  }
  token.type = TokenType::Text;
  token.data = {start, static_cast<size_t>(cur_ - start)};
  return true;
}

template<ParserOptions Opts>
inline auto BasicParser<Opts>::parseMarkup(Token& token) -> bool {
  if (atEnd()) {
    return makeError(token, ErrorCode::UnexpectedEndAfterLt);
  }
  const char c = *cur_;
  if (c == '/') {
    ++cur_;
    return parseElementClose(token);
  }
  if (c == '!') {
    ++cur_;
    if (startsWith("--")) {
      cur_ += 2;
      return parseComment(token);
    }
    if (startsWith("[CDATA[")) {
      cur_ += 7;
      return parseCdata(token);
    }
    skipBangDecl();
    return nextFromSource(token);
  }
  if (c == '?') {
    ++cur_;
    return parsePi(token);
  }
  if (detail::isNameStart(c)) {
    return parseElementOpen(token);
  }
  return makeError(token, ErrorCode::UnexpectedCharAfterLt);
}

// Scans a start-tag's attribute list into attributes_ up to the closing '>'
// or '/>'. cur_ must be just past the element name; attributes_ must be
// cleared. Records the error code and returns false on malformed input.
template<ParserOptions Opts>
inline auto BasicParser<Opts>::parseAttributes(bool& self_closing) -> bool {
  // STRICT only: one bit per attribute name hash; a clear bit proves the name
  // is new, so the exact duplicate scan runs only on 1/64 bit collisions.
  [[maybe_unused]] uint64_t seen_name_bits = 0;  // NOLINT(misc-const-correctness)
  while (true) {
    skipWhitespace();
    if (atEnd()) {
      return fail(ErrorCode::UnclosedTag);
    }
    const char c = *cur_;
    if (c == '>') {
      ++cur_;
      return true;
    }
    if (c == '/' && cur_ + 1 < end_ && *(cur_ + 1) == '>') {
      cur_ += 2;
      self_closing = true;
      return true;
    }
    if (!detail::isNameStart(c)) {
      return fail(ErrorCode::ExpectedAttributeName);
    }
    if (attributes_.size() >= MAX_ATTRIBUTES_PER_ELEMENT) [[unlikely]] {
      return fail(ErrorCode::TooManyAttributes);
    }

    Attribute& a = attributes_.emplace_back();
    parseName(a.prefix, a.name, a.name_hash);
    if constexpr (STRICT) {
      // WFC: Unique Att Spec. The name-hash bit filter screens out new names;
      // on a bit collision the hash compares filter further, and the exact
      // name/prefix compare only runs on the (astronomically rare) hash match.
      const uint64_t name_bit = uint64_t{1} << (a.name_hash & 63U);
      if ((seen_name_bits & name_bit) != 0) [[unlikely]] {
        for (size_t i = 0; i + 1 < attributes_.size(); ++i) {
          if (attributes_[i].name_hash == a.name_hash && attributes_[i].name == a.name &&
              attributes_[i].prefix == a.prefix) {
            return fail(ErrorCode::DuplicateAttribute);
          }
        }
      }
      seen_name_bits |= name_bit;
    }
    char quote = 0;
    if (end_ - cur_ >= 2 && cur_[0] == '=' && (cur_[1] == '"' || cur_[1] == '\'')) {
      // Common shape: name="value" with no whitespace around '='.
      quote = cur_[1];
      cur_ += 2;
    } else {
      skipWhitespace();
      if (!expect('=')) {
        return fail(ErrorCode::ExpectedEquals);
      }
      skipWhitespace();
      quote = peekChar();
      if (quote != '"' && quote != '\'') {
        return fail(ErrorCode::ExpectedQuotedValue);
      }
      ++cur_;
    }
    const char* val_start = cur_;
    // Values are typically short: probe the first bytes inline before paying
    // the memchr call overhead in findByte (short runs favor the byte loop).
    const char* val_end = cur_;
    const char* const probe_limit = (end_ - cur_ < 16) ? end_ : cur_ + 16;
    while (val_end != probe_limit && *val_end != quote) {
      ++val_end;
    }
    if (val_end == probe_limit && (val_end == end_ || *val_end != quote)) {
      val_end = findByte(val_end, quote);
    }
    if (val_end == end_) {
      return fail(ErrorCode::UnterminatedAttributeValue);
    }
    if constexpr (STRICT) {
      // WFC: No '<' in attribute values (Production [10]). One short memchr
      // over the value; a '<' beyond val_end belongs to later markup.
      if (findByte(val_start, '<') < val_end) {
        return fail(ErrorCode::LtInAttributeValue);
      }
      if (containsForbiddenControl(val_start, val_end)) {
        return fail(ErrorCode::ForbiddenControlChar);
      }
    }
    a.value = {val_start, static_cast<size_t>(val_end - val_start)};
    cur_ = val_end + 1;
  }
}

template<ParserOptions Opts>
inline auto BasicParser<Opts>::parseElementOpen(Token& token) -> bool {
  token.type = TokenType::ElementOpen;
  token.self_closing = false;
  parseName(token.prefix, token.name, token.name_hash);
  if (token.name.empty()) {
    return makeError(token, ErrorCode::ExpectedElementName);
  }
  attributes_.clear();
  if (!parseAttributes(token.self_closing)) {
    token.type = TokenType::Error;
    return false;
  }
  return true;
}

template<ParserOptions Opts>
inline auto BasicParser<Opts>::parseElementClose(Token& token) -> bool {
  token.type = TokenType::ElementClose;
  FieldHash name_hash{};
  parseName(token.prefix, token.name, name_hash);
  if (token.name.empty()) {
    return makeError(token, ErrorCode::ExpectedNameInCloseTag);
  }
  skipWhitespace();
  if (!expect('>')) {
    return makeError(token, ErrorCode::ExpectedCloseTagEnd);
  }
  return true;
}

template<ParserOptions Opts>
template<typename T>
inline auto BasicParser<Opts>::parseNumeric(std::string_view text, T& out) noexcept -> bool {
  if constexpr (std::same_as<T, bool>) {
    detail::trimWhitespace(text);
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
    detail::trimWhitespace(text);

    if (text.empty()) {
      return false;
    }

    // protect from_chars from leading +
    if (text.front() == '+') {
      text.remove_prefix(1);
      if (text.empty()) {
        return false;
      }
    }

    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
  }
}

// Document-order fast path: attribute fields are typically declared in the
// same order the attributes appear, so try the cursor position first and
// fall back to a full first-match scan on miss.
template<ParserOptions Opts>
template<typename T>
inline auto BasicParser<Opts>::attr(const FieldHash hash, T& out, size_t& pos) -> bool {
  size_t idx{};
  if (pos < attributes_.size() && attributes_[pos].name_hash == hash) {
    idx = pos++;
  } else {
    const auto it = std::ranges::find(attributes_, hash, &Attribute::name_hash);
    if (it == attributes_.end()) {
      return false;
    }
    idx = static_cast<size_t>(it - attributes_.begin());
    pos = idx + 1;
  }
  return assignAttrChecked(attributes_[idx].value, out);
}

template<ParserOptions Opts>
inline auto BasicParser<Opts>::beginElement(std::string_view expected_name) -> bool {
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

template<ParserOptions Opts>
template<size_t NAME_LEN>
inline auto BasicParser<Opts>::endElement(std::string_view expected_name) -> bool {
  if (!has_peek_) {
    skipWhitespace();
    const char* past = nullptr;
    if constexpr (NAME_LEN != 0) {
      past = matchCloseTagN<NAME_LEN>(cur_, expected_name.data());
    } else {
      past = matchCloseTag(cur_, expected_name);
    }
    if (past != nullptr) {
      cur_ = past;
      return true;
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
template<ParserOptions Opts>
inline auto BasicParser<Opts>::skipElement() -> void {
  if (last_self_closing_) {
    return;
  }
  size_t depth = 1;
  while (depth > 0) {
    const char* lt = findByte(cur_, '<');
    if (lt == end_ || lt + 1 >= end_) {
      cur_ = end_;
      return;
    }
    cur_ = lt + 1;
    const char c = *cur_;
    if (c == '/') {
      const char* gt = findByte(cur_, '>');
      if (gt == end_) {
        cur_ = end_;
        return;
      }
      cur_ = gt + 1;
      --depth;
    } else if (c == '!') {
      ++cur_;
      if (startsWith("--")) {
        cur_ += 2;
        skipPast("-->");
      } else if (startsWith("[CDATA[")) {
        cur_ += 7;
        skipPast("]]>");
      } else {
        skipBangDecl();
      }
    } else if (c == '?') {
      ++cur_;
      skipPast("?>");
    } else if (detail::isNameStart(c)) {
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
          const char* q = findByte(cur_ + 1, ch);
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
      if (!self_closing && ++depth > static_cast<size_t>(MAX_DEPTH)) [[unlikely]] {
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

// Opening tag already consumed by the caller.
template<ParserOptions Opts>
template<size_t NAME_LEN, typename T>
inline auto BasicParser<Opts>::readElement(std::string_view expected_name, T& out,
                                           const uint16_t depth) -> bool {
  if (depth > MAX_DEPTH) {
    return fail(ErrorCode::DepthExceeded);
  }
  [[maybe_unused]] const bool is_self_closing = last_self_closing_;

  if constexpr (XmlUniquePtr<T>) {
    // Optional/recursive child: allocate, then parse the element into it. The
    // depth guard above bounds recursion; the unwrap keeps the same depth.
    out = std::make_unique<typename T::element_type>();
    return readElement<NAME_LEN>(expected_name, *out, depth);
  } else if constexpr (XmlOptional<T>) {
    // Element present -> engage the optional and parse the inner value/object.
    return readElement<NAME_LEN>(expected_name, out.emplace(), depth);
  } else if constexpr (XmlScalar<T>) {
    if (is_self_closing) {
      if constexpr (XmlStringLike<T>) {
        return true;
      }
      return fail(scalarError<T>());  // empty numeric/bool/enum
    }
    // Fast path: locate the closing tag and parse the enclosed run directly.
    // A normalizing non-string scalar only needs readChardata when its text
    // actually carries a reference ('&') or CR; those are the sole bytes text
    // normalization rewrites, so otherwise the raw run parses identically here.
    const char* found = findByte(cur_, '<');
    const char* past_close = nullptr;
    if (found != end_) {
      if constexpr (NAME_LEN != 0) {
        past_close = matchCloseTagN<NAME_LEN>(found, expected_name.data());
      } else {
        past_close = matchCloseTag(found, expected_name);
      }
    }
    if (past_close != nullptr) {
      const std::string_view text{cur_, static_cast<size_t>(found - cur_)};
      bool fast = true;  // NOLINT(misc-const-correctness): mutable only when normalizing
      if constexpr (NORMALIZE && !XmlStringLike<T>) {
        // Numeric/enum leaves are a handful of bytes: the branchless loop
        // beats the find_first_of libcall on the no-reference common case.
        for (const char ch : text) {
          fast &= ch != '&' && ch != '\r';
        }
      }
      if (fast) {
        if constexpr (STRICT) {
          if (containsCdataEnd(cur_, found)) {
            return fail(ErrorCode::CDataEndInContent);
          }
          if (containsForbiddenControl(cur_, found)) {
            return fail(ErrorCode::ForbiddenControlChar);
          }
        }
        cur_ = past_close;
        has_peek_ = false;
        if constexpr (XmlStringLike<T>) {
          return assignValue(out, text);
        } else {
          return parseScalar(text, out) ? true : fail(scalarError<T>());
        }
      }
    }
    return readChardata<true>(out, expected_name);
  } else {
    static_assert(XmlObject<T>, "field type must be XmlScalar or an XmlObject with XmlMetadata");
    const bool result = pull(out, depth);
    if (!is_self_closing && !endElement<NAME_LEN>(expected_name)) {
      return false;
    }
    return result;
  }
}

template<ParserOptions Opts>
template<XmlObject T>
inline auto BasicParser<Opts>::pull(T& object, const uint16_t depth) -> bool {
  constexpr size_t N = detail::FIELD_COUNT<T>;
  constexpr auto IDX_SEQ = detail::FIELD_SEQ<T>;
  static_assert(detail::allNamesUnique<T>(),
                "FNV-1a hash collision among element/attribute/variant names in "
                "XmlMetadata<T>");
  static_assert(detail::optionalsNotRequired<T>(),
                "a std::optional field cannot be marked required (an optional "
                "field is inherently optional)");

  // Per-field fill counters for fixed containers, indexed by field. The
  // counters are only reachable through the dispatch table's opaque span, so
  // without this gate the per-element zero-init would survive for every type;
  // types with no fixed container collapse it to one dead slot.
  constexpr size_t FILL_N = detail::HAS_FIXED_CONTAINER_FIELDS<T> ? N : 1;
  std::array<size_t, FILL_N> arr_fill{};

  // Presence tracking for required fields. When nothing is required (the
  // default) HAS_REQUIRED is false, so 'parsed' is never read: every write to
  // it is dead-store eliminated and check_required() compiles to `return true`.
  static constexpr auto REQUIRED_MASK = detail::makeRequiredMask<T>();
  constexpr bool HAS_REQUIRED = detail::HAS_REQUIRED_FIELDS<T>;
  [[maybe_unused]] detail::RequiredMaskT<T> parsed{};  // NOLINT(misc-const-correctness)
  const auto check_required = [](BasicParser& self, const detail::RequiredMaskT<T>& have) -> bool {
    if constexpr (HAS_REQUIRED) {
      if (!have.containsAll(REQUIRED_MASK)) {
        return self.fail(ErrorCode::MissingRequiredField);
      }
    }
    return true;
  };

  // Apply attribute fields only when the type actually has some. Streamed
  // capture (attr_streamed_) is per-element state set by streamAttrs and
  // consumed exactly once here.
  constexpr bool HAS_ATTRS = detail::HAS_ATTR_FIELDS<T>;
  if constexpr (HAS_ATTRS) {
    if constexpr (detail::streamsAttrs<T, STRICT>()) {
      if (attr_streamed_) {
        attr_streamed_ = false;
        dispatchStreamedAttrs<T>(*this, object, parsed, IDX_SEQ);
      } else {
        dispatchAttrs<T>(*this, object, parsed, IDX_SEQ);
      }
    } else {
      dispatchAttrs<T>(*this, object, parsed, IDX_SEQ);
    }
    // A string attribute may have carried a malformed/undefined reference
    if (error()) [[unlikely]] {
      return false;
    }
  }

  // simpleContent: the element's own text feeds a value field (its attributes
  // were handled above). Such a type has no child element fields, so capturing
  // the text and checking required fields completes it.
  if constexpr (detail::HAS_VALUE_FIELD<T>) {
    static_assert(!detail::HAS_ELEMENT_FIELDS<T>,
                  "a valueField cannot coexist with child element/container "
                  "fields (XSD simpleContent has no element children)");
    constexpr size_t VALUE_IDX = detail::VALUE_FIELD_INDEX<T>;
    constexpr auto& vf = std::get<VALUE_IDX>(XmlMetadata<T>::fields);
    using M = std::decay_t<decltype(object.*(vf.member))>;
    // A value field counts as present only when it carries non-empty text
    // (an empty <e/> or <e></e> satisfies a string but not a required field).
    bool present = false;
    if (last_self_closing_) {
      if constexpr (XmlStringLike<M>) {
        object.*(vf.member) = {};
      } else {
        return fail(scalarError<M>());  // empty number/enum is invalid
      }
    } else {
      if (!readChardata<false>(object.*(vf.member), {})) {
        return false;
      }
      if constexpr (XmlStringLike<M>) {
        present = !(object.*(vf.member)).empty();
      } else {
        present = true;  // a number/enum only parses from non-empty text
      }
    }
    if (present) {
      parsed.set(VALUE_IDX);
    }
    return check_required(*this, parsed);
  } else if (last_self_closing_) {
    return check_required(*this, parsed);
  }

  // Document-order hint: index of the field expected next. Schema-ordered
  // XML hits the memcmp fast path below on every element; out-of-order
  // documents miss once, re-sync at the dispatch site, and stay correct.
  constexpr bool HAS_ELEMS = detail::HAS_ELEMENT_FIELDS<T>;
  constexpr bool HAS_VARIANTS = detail::HAS_VARIANT_FIELDS<T>;
  static constexpr auto NEXT_ELEM = detail::makeNextElemTable<T>();
  [[maybe_unused]] size_t hint = detail::FIRST_ELEM_INDEX<T>;  // NOLINT(misc-const-correctness)

  while (true) {
    if (!has_peek_) {
      skipWhitespace();
      if (atEnd()) {
        return fail(ErrorCode::UnexpectedEof);
      }
      if (cur_[0] == '<' && cur_ + 1 < end_ && cur_[1] == '/') {
        return check_required(*this, parsed);
      }

      // Fast path: match the hinted open tag, bypassing full tokenisation
      // (parseName, hash, peek machinery). The per-field fused dispatch keeps
      // every tag compare a compile-time-length memcmp that folds to inline
      // word compares instead of a libc call.
      if constexpr (HAS_ELEMS) {
        bool matched = false;
        if (!tryReadFieldAt(hint, *this, object, depth, arr_fill, parsed, matched, IDX_SEQ)) {
          return false;
        }
        if (matched) {
          hint = NEXT_ELEM[hint];
          continue;
        }
      }

      cur_ = findByte(cur_, '<');
    }

    const Token* token = peek();
    if (!token || token->type == TokenType::Error) [[unlikely]] {
      return false;
    }
    if (token->type == TokenType::ElementClose) {
      return check_required(*this, parsed);
    }
    if (token->type != TokenType::ElementOpen) {
      consume();
      continue;
    }

    const size_t idx = detail::findFieldIndex<T>(token->name_hash);
    if (idx >= N) {
      // No named field matched. A variant (xs:choice) alternative might; this
      // path is compiled out entirely for types with no variant field.
      bool handled = false;  // NOLINT(misc-const-correctness)
      if constexpr (HAS_VARIANTS) {
        static constexpr auto VARIANT_MATCH = detail::makeVariantMatchers<T>();
        static constexpr auto variant_dispatch =
            buildVariantDispatch<T>(std::make_index_sequence<VARIANT_MATCH.size()>{});
        for (size_t k = 0; k < VARIANT_MATCH.size(); ++k) {
          if (VARIANT_MATCH[k].hash == token->name_hash) {
            consumePeeked();
            if (!variant_dispatch[k](*this, object, depth, parsed)) {
              return false;
            }
            handled = true;
            break;
          }
        }
      }
      if (!handled) {
        skipCurrent();
      }
      continue;
    }
    consumePeeked();
    if (!readFieldAt(idx, *this, object, depth, arr_fill, parsed, IDX_SEQ)) {
      return false;
    }
    if constexpr (HAS_ELEMS && N > 1) {
      hint = NEXT_ELEM[idx];
    }
  }
}

/// @brief XML serializer. Converts XmlObject instances to XML strings.
/// @tparam PRETTY If true, emits indented output; if false, compact.
template<bool PRETTY = true>
class Serializer {
 public:
  explicit Serializer(std::string& out) noexcept : out_(out) {}

  /// @brief Serializes obj as an XML element named tag, appending to the output
  /// string.
  template<XmlObject T>
  auto write(std::string_view tag, const T& obj) -> void {
    writeElement(tag, obj, 0);
  }

 private:
  std::string& out_;

  auto doIndent(int depth) -> void {
    if constexpr (PRETTY) {
      out_.append(static_cast<size_t>(depth) * 2, ' ');
    }
  }

  auto doNewline() -> void {
    if constexpr (PRETTY) {
      out_ += '\n';
    }
  }

  auto openTag(std::string_view tag) -> void { appendTag<false>(tag); }
  auto closeTag(std::string_view tag) -> void { appendTag<true>(tag); }

  // Assembles "<tag>" / "</tag>" in a stack buffer so each tag costs one
  // append (one capacity check) instead of three.
  template<bool CLOSING>
  auto appendTag(std::string_view tag) -> void {
    std::array<char, 64> buf{};
    const size_t n = tag.size() + (CLOSING ? 3 : 2);
    if (n <= buf.size()) [[likely]] {
      char* w = buf.data();
      *w++ = '<';
      if constexpr (CLOSING) {
        *w++ = '/';
      }
      std::memcpy(w, tag.data(), tag.size());
      w[tag.size()] = '>';
      out_.append(buf.data(), n);
    } else {
      out_ += '<';
      if constexpr (CLOSING) {
        out_ += '/';
      }
      out_ += tag;
      out_ += '>';
    }
  }

  // Escapes '&' and '<' always; attribute values additionally escape '"',
  // text content escapes '>'. Copies safe byte runs in bulk via append().
  template<bool kAttr>
  static auto escape(std::string& out, std::string_view s) -> void {
    static constexpr auto SPECIAL = [] {
      std::array<bool, 256> t{};
      t[static_cast<unsigned char>('&')] = true;
      t[static_cast<unsigned char>('<')] = true;
      if constexpr (!kAttr) {
        t[static_cast<unsigned char>('>')] = true;
      }
      if constexpr (kAttr) {
        t[static_cast<unsigned char>('"')] = true;
      }
      return t;
    }();
    const char* p = s.data();
    const char* const e = p + s.size();
    while (p < e) {
      // Position of the next byte needing an escape (see detail::findSpecial).
      const char* q = p + detail::findSpecial(p, static_cast<size_t>(e - p), SPECIAL,
                                              std::array{'&', '<', kAttr ? '"' : '>'});
      out.append(p, q);
      if (q == e) {
        break;
      }
      switch (*q) {
        case '&':
          out += "&amp;";
          break;
        case '<':
          out += "&lt;";
          break;
        case '>':
          out += "&gt;";
          break;
        default:
          out += "&quot;";
          break;
      }
      p = q + 1;
    }
  }

  template<typename V>
  static auto appendArithmetic(std::string& out, V v) -> void {
    if constexpr (std::same_as<V, bool>) {
      out += v ? "true" : "false";
    } else {
      std::array<char, 32> buf{};
      const auto [ptr, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), v);
      if (ec == std::errc()) {
        out.append(buf.data(), static_cast<size_t>(ptr - buf.data()));
      }
    }
  }

  // Appends a scalar's text form. Attr selects attribute- vs content-escaping;
  // custom-value traits must emit XML-safe text.
  template<bool Attr, typename V>
  auto writeScalar(const V& v) -> void {
    if constexpr (XmlStringLike<V>) {
      escape<Attr>(out_, v);
    } else if constexpr (XmlEnum<V>) {
      escape<Attr>(out_, detail::enumToString(v));
    } else if constexpr (XmlCustomValue<V>) {
      XmlValueTraits<V>::format(out_, v);
    } else {
      appendArithmetic(out_, v);
    }
  }

  template<typename V>
  auto writeAttrValue(std::string_view name, const V& v) -> void {
    out_ += ' ';
    out_ += name;
    out_ += "=\"";
    writeScalar<true>(v);
    out_ += '"';
  }

  template<typename V>
  auto writePrimElement(std::string_view tag, const V& v, int depth) -> void {
    doIndent(depth);
    openTag(tag);
    writeScalar<false>(v);
    closeTag(tag);
    doNewline();
  }

  template<typename V>
  auto writeFieldValue(std::string_view tag, const V& v, int depth) -> void {
    if constexpr (XmlUniquePtr<V>) {
      if (v) {  // null optional/recursive child: omit the element entirely
        writeElement(tag, *v, depth);
      }
    } else if constexpr (XmlOptional<V>) {
      if (v) {  // disengaged optional: omit the element entirely
        writeFieldValue(tag, *v, depth);
      }
    } else if constexpr (XmlScalar<V>) {
      writePrimElement(tag, v, depth);
    } else {
      static_assert(XmlObject<V>, "field type must be XmlScalar or XmlObject");
      writeElement(tag, v, depth);
    }
  }

  // Writes the items of a list/container, space-separated (XSD xs:list form).
  template<typename M>
  auto writeListItems(const M& container) -> void {
    bool first = true;
    detail::forEachItem(container, [this, &first](const auto& v) {
      if (!first) {
        out_ += ' ';
      }
      first = false;
      writeScalar<true>(v);
    });
  }

  template<typename T, size_t I>
  auto writeAttrIf(const T& obj) -> void {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      using M = std::remove_cvref_t<decltype(obj.*(f.member))>;
      if constexpr (XmlListContainer<M>) {
        out_ += ' ';
        out_ += f.xml_name;
        out_ += "=\"";
        writeListItems(obj.*(f.member));
        out_ += '"';
      } else if constexpr (XmlOptional<M>) {
        if (const auto& m = obj.*(f.member)) {  // omit absent optional attrs
          writeAttrValue(f.xml_name, *m);
        }
      } else {
        writeAttrValue(f.xml_name, obj.*(f.member));
      }
    }
  }

  template<typename T, size_t... I>
  auto writeAttrs(const T& obj, std::index_sequence<I...>) -> void {
    (..., writeAttrIf<T, I>(obj));
  }

  // Writes the active alternative of a variant under its bound element name.
  template<typename FieldT, typename Var>
  auto writeVariantActive(const FieldT& f, const Var& var, int depth) -> void {
    writeVariantAlt(f, var, depth, std::make_index_sequence<std::variant_size_v<Var>>{});
  }

  template<typename FieldT, typename Var, size_t... J>
  auto writeVariantAlt(const FieldT& f, const Var& var, int depth,
                       std::index_sequence<J...>) -> void {
    (..., (var.index() == J ? writeFieldValue(f.names[J], std::get<J>(var), depth) : void()));
  }

  template<typename T, size_t I>
  auto writeChild(const T& obj, int depth) -> void {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      // written on opening tag
    } else if constexpr (f.kind == FieldKind::Value) {
      // emitted inline by writeElement, not as a child
    } else if constexpr (f.kind == FieldKind::List) {
      doIndent(depth);
      openTag(f.xml_name);
      writeListItems(obj.*(f.member));
      closeTag(f.xml_name);
      doNewline();
    } else if constexpr (f.kind == FieldKind::Variant) {
      using M = std::decay_t<decltype(obj.*(f.member))>;
      if constexpr (detail::IsVariant<M>::value) {
        writeVariantActive(f, obj.*(f.member), depth);
      } else {
        for (const auto& item : obj.*(f.member)) {
          writeVariantActive(f, item, depth);
        }
      }
    } else if constexpr (f.kind == FieldKind::Container) {
      using M = std::decay_t<decltype(obj.*(f.member))>;
      detail::forEachItem(obj.*(f.member),
                          [this, depth](const typename XmlContainerTraits<M>::value_type& item) {
        writeFieldValue(std::get<I>(XmlMetadata<T>::fields).xml_name, item, depth);
      });
    } else {
      writeFieldValue(f.xml_name, obj.*(f.member), depth);
    }
  }

  template<typename T, size_t... I>
  auto writeChildren(const T& obj, int depth, std::index_sequence<I...>) -> void {
    (..., writeChild<T, I>(obj, depth));
  }

  template<typename T>
  auto writeElement(std::string_view tag, const T& obj, int depth) -> void {
    constexpr size_t N = detail::FIELD_COUNT<T>;
    using Seq = std::make_index_sequence<N>;

    doIndent(depth);
    out_ += '<';
    out_ += tag;
    writeAttrs<T>(obj, Seq{});

    if constexpr (detail::HAS_VALUE_FIELD<T>) {
      // simpleContent: <tag attrs>text</tag> on a single line.
      constexpr size_t VI = detail::VALUE_FIELD_INDEX<T>;
      constexpr auto& vf = std::get<VI>(XmlMetadata<T>::fields);
      out_ += '>';
      writeScalar<false>(obj.*(vf.member));
      closeTag(tag);
      doNewline();
    } else if constexpr (detail::HAS_ELEMENT_FIELDS<T> || detail::HAS_VARIANT_FIELDS<T>) {
      out_ += '>';
      doNewline();
      writeChildren<T>(obj, depth + 1, Seq{});
      doIndent(depth);
      closeTag(tag);
      doNewline();
    } else {
      out_ += "/>";
      doNewline();
    }
  }
};

/// @brief Serializes object to an XML string under root_name.
/// @tparam PRETTY  If true, emits indented output.
/// @tparam T        XmlObject type with an XmlMetadata specialization.
/// @param root_name Root element tag name.
/// @param object    Object to serialize.
/// @return XML string containing the serialized data.
template<bool PRETTY = true, XmlObject T>
[[nodiscard]] auto serialize(std::string_view root_name, const T& object) -> std::string {
  std::string out;
  // Skip the small-document growth reallocations; the buffer-reuse path
  // (constructing a Serializer over a caller-owned string) avoids them all.
  out.reserve(4096);
  Serializer<PRETTY> s{out};
  s.write(root_name, object);
  return out;
}

/// @brief Optional constraint validator for type T.
///
/// Specialize this (typically via xsdgen output) to enforce XSD facet
/// constraints on a deserialized object. Each specialization checks only the
/// object's own members; xmlight::validate() recurses through nested objects.
/// The default is a no-op.
/// @return nullopt if all constraints pass, or a violation message.
template<typename T>
struct XmlConstraints {
  [[nodiscard]] static auto check(const T&) noexcept -> std::optional<std::string> { return {}; }
};

namespace detail {

template<typename T>
auto deepValidate(const T& obj) -> std::optional<std::string>;

/// @brief Recurses into one field member for validate(): unwraps pointers,
/// optionals, variants, and containers, and validates nested XmlObjects.
/// Scalar leaves return nullopt; the owner's own check() covers them.
template<typename M>
auto validateMember(const M& m) -> std::optional<std::string> {
  if constexpr (IsUniquePtr<M>::value || IsOptional<M>::value) {
    return m ? validateMember(*m) : std::optional<std::string>{};
  } else if constexpr (IsVariant<M>::value) {
    return std::visit([](const auto& alt) { return validateMember(alt); }, m);
  } else if constexpr (XmlObject<M>) {
    return deepValidate(m);
  } else if constexpr (XmlListContainer<M>) {
    std::optional<std::string> err;
    forEachItem(m, [&err](const auto& item) {
      if (!err) {
        err = validateMember(item);
      }
    });
    return err;
  } else {
    return {};
  }
}

/// @brief Checks obj's own XmlConstraints, then every field member in
/// declaration order; returns the first violation found.
template<typename T>
auto deepValidate(const T& obj) -> std::optional<std::string> {
  if (auto msg = XmlConstraints<T>::check(obj)) {
    return msg;
  }
  if constexpr (XmlObject<T>) {
    std::optional<std::string> err;
    [&err, &obj]<size_t... I>(std::index_sequence<I...>) {
      ([&err, &obj] {
        if (!err) {
          err = validateMember(obj.*(std::get<I>(XmlMetadata<T>::fields).member));
        }
      }(), ...);
    }(FIELD_SEQ<T>);
    return err;
  } else {
    return {};
  }
}

}  // namespace detail

/// @brief Validates obj against its XmlConstraints specialization, then
/// recursively validates nested objects: fields that are XmlObjects, and the
/// contents of containers, optionals, unique_ptrs, and variants.
/// @return nullopt if valid, or a ValidationError describing the first
/// violation in declaration order.
template<typename T>
[[nodiscard]] auto validate(const T& obj) -> std::optional<ValidationError> {
  auto msg = detail::deepValidate(obj);
  if (!msg) {
    return {};
  }
  return ValidationError{std::move(*msg)};
}

}  // namespace xmlight

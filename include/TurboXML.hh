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
  Error,                  ///< Parse error; token name holds the message.
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
  std::string_view name;   ///< Element or PI target name (local part).
  std::string_view prefix; ///< Namespace prefix, or empty.
  std::string_view data;   ///< Text, CDATA, comment, or PI content.
  FieldHash name_hash{};   ///< FNV-1a hash of name.
  TokenType type{};        ///< Token kind.
  bool self_closing{};     ///< True for self-closing elements (<foo/>).
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
  FieldHash h = kFnvOffset;
  for (char c : s) {
    h = fnv1a_step(h, static_cast<unsigned char>(c));
  }
  return h;
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
constexpr auto make_field(std::string_view name, M C::* m)
    -> FieldBase<K, C, M> {
  return {name, m, hash_field_name(name)};
}

}  // namespace detail

/// @brief Creates an element field descriptor.
/// @param name XML element name.
/// @param m    Pointer to the target member.
template <typename C, typename M>
constexpr auto field(std::string_view name, M C::* m) -> Field<C, M> {
  return detail::make_field<FieldKind::Element>(name, m);
}

/// @brief Creates an attribute field descriptor.
/// @param name XML attribute name.
/// @param m    Pointer to the target member.
template <typename C, typename M>
constexpr auto attr_field(std::string_view name, M C::* m) -> AttrField<C, M> {
  return detail::make_field<FieldKind::Attr>(name, m);
}

/// @brief Creates a container field descriptor for dynamic containers (e.g., std::vector).
/// @param name XML element name for each item.
/// @param m    Pointer to the target member.
template <typename C, typename M>
constexpr auto vec_field(std::string_view name, M C::* m)
    -> ContainerField<C, M> {
  return detail::make_field<FieldKind::Container>(name, m);
}

/// @brief Creates a container field descriptor for fixed containers (e.g., std::array).
/// @param name XML element name for each item.
/// @param m    Pointer to the target member.
template <typename C, typename M>
constexpr auto arr_field(std::string_view name, M C::* m)
    -> ContainerField<C, M> {
  return detail::make_field<FieldKind::Container>(name, m);
}

// Metadata + concepts
/// @brief Specialize this to register XML field mappings for type T.
///
/// The specialization must provide `static constexpr auto fields` as a tuple
/// of Field, AttrField, or ContainerField descriptors.
/// @tparam T User-defined struct to describe.
template <typename T>
struct XmlMetadata;

/// @brief Satisfied when T has an XmlMetadata specialization with a fields tuple.
template <typename T>
concept XmlObject = requires { XmlMetadata<T>::fields; };

/// @brief Satisfied when T is std::string or std::string_view.
template <typename T>
concept XmlStringLike =
    std::same_as<T, std::string_view> || std::same_as<T, std::string>;

/// @brief Satisfied when T is a numeric type or a string-like type.
template <typename T>
concept XmlPrimitive = std::is_arithmetic_v<T> || XmlStringLike<T>;

/// @brief Adapts a container for use with ContainerField. Specialize for custom types.
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

/// @brief Satisfied when C supports dynamic insertion via XmlContainerTraits (e.g., std::vector).
template <typename C>
concept XmlDynContainer = requires(C& c) {
  typename XmlContainerTraits<C>::value_type;
  {
    XmlContainerTraits<C>::emplace(c)
  } -> std::same_as<typename XmlContainerTraits<C>::value_type&>;
  XmlContainerTraits<C>::pop(c);
};

/// @brief Satisfied when C has a fixed capacity and indexed access via XmlContainerTraits (e.g., std::array).
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

template <typename T, size_t... I>
constexpr auto make_field_hashes_impl(std::index_sequence<I...>) noexcept {
  return std::array<FieldHash, sizeof...(I)>{
      {std::get<I>(XmlMetadata<T>::fields).hash...}};
}

template <typename T>
constexpr auto make_field_hashes() noexcept {
  return make_field_hashes_impl<T>(
      std::make_index_sequence<
          std::tuple_size_v<decltype(XmlMetadata<T>::fields)>>{});
}

template <typename T>
inline size_t find_field_index(FieldHash hash) noexcept {
  constexpr auto hashes = make_field_hashes<T>();
  const auto it = std::ranges::find(hashes, hash);
  return static_cast<size_t>(std::distance(hashes.begin(), it));
}

template <typename T, size_t... I>
constexpr auto make_field_names_impl(std::index_sequence<I...>) noexcept {
  return std::array<std::string_view, sizeof...(I)>{
      {std::get<I>(XmlMetadata<T>::fields).xml_name...}};
}

template <typename T>
constexpr auto make_field_names() noexcept {
  return make_field_names_impl<T>(
      std::make_index_sequence<
          std::tuple_size_v<decltype(XmlMetadata<T>::fields)>>{});
}

template <typename T, size_t... I>
constexpr auto make_field_kinds_impl(std::index_sequence<I...>) noexcept {
  return std::array<FieldKind, sizeof...(I)>{
      {std::get<I>(XmlMetadata<T>::fields).kind...}};
}

template <typename T>
constexpr auto make_field_kinds() noexcept {
  return make_field_kinds_impl<T>(
      std::make_index_sequence<
          std::tuple_size_v<decltype(XmlMetadata<T>::fields)>>{});
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

}  // namespace detail

/// @brief Pull parser for XML deserialization.
///
/// Parses a string_view in a single forward pass with no heap allocation
/// beyond the attribute vector. Not copyable. Use deserialize() to drive it.
class Parser {
 public:
  static constexpr int kMaxDepth = 256;

  /// @brief Constructs a parser over src. src must outlive the Parser.
  explicit Parser(std::string_view src) noexcept
      : src_(src), cur_(src.data()), end_(src.data() + src.size()) {}

  Parser(const Parser&) = delete;
  auto operator=(const Parser&) -> Parser& = delete;
  Parser(Parser&&) noexcept = default;
  auto operator=(Parser&&) noexcept -> Parser& = default;

  template <typename T>
  friend auto deserialize(Parser& parser, std::string_view root_name, T& object)
      -> bool;

  /// @brief Resets the parser to the beginning of the source string.
  void reset() {
    cur_ = src_.data();
    end_ = src_.data() + src_.size();
    has_peek_ = false;
    attributes_.clear();
    error_ = false;
    last_self_closing_ = false;
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
  [[nodiscard]] auto attr(FieldHash hash, T& out) -> bool;

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

  auto make_error(Token& token, std::string_view msg) noexcept -> bool {
    error_ = true;
    token.type = TokenType::Error;
    token.name = msg;
    return false;
  }

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

  // Scan forward until `delim` is found. Sets token.data to the content
  // before the delimiter and advances past it.
  auto scan_to_delimiter(Token& token, std::string_view delim,
                         std::string_view error_msg) -> bool {
    const char* start = cur_;
    const char first = delim[0];
    while (cur_ < end_) {
      const char* hit = static_cast<const char*>(
          std::memchr(cur_, first, static_cast<size_t>(end_ - cur_)));
      if (!hit) {
        cur_ = end_;
        break;
      }
      cur_ = hit;
      if (starts_with(delim)) {
        token.data = {start, static_cast<size_t>(cur_ - start)};
        cur_ += delim.size();
        return true;
      }
      ++cur_;
    }
    return make_error(token, error_msg);
  }

  auto parse_comment(Token& token) -> bool {
    token.type = TokenType::Comment;
    return scan_to_delimiter(token, "-->", "unterminated comment");
  }

  auto parse_cdata(Token& token) -> bool {
    token.type = TokenType::CData;
    return scan_to_delimiter(token, "]]>", "unterminated CDATA section");
  }

  auto parse_pi(Token& token) -> bool {
    parse_name(token.prefix, token.name, token.name_hash);
    if (token.name.empty()) {
      return make_error(token, "expected PI target");
    }
    token.type = (token.name == "xml") ? TokenType::XmlDeclaration
                                       : TokenType::ProcessingInstruction;
    skip_whitespace();
    return scan_to_delimiter(token, "?>",
                             "unterminated processing instruction");
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
  static bool read_field(Parser& p, T& obj, uint16_t depth, size_t* arr_fill) {
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
  static void apply_attr(Parser& p, T& obj) {
    constexpr auto& f = std::get<I>(XmlMetadata<T>::fields);
    if constexpr (f.kind == FieldKind::Attr) {
      std::ignore = p.attr(f.hash, obj.*(f.member));
    }
  }

  template <typename T, size_t... I>
  static constexpr auto build_elem_dispatch(
      std::index_sequence<I...>) noexcept {
    using Handler = bool (*)(Parser&, T&, uint16_t, size_t*);
    return std::array<Handler, sizeof...(I)>{&read_field<T, I>...};
  }

  template <typename T, size_t... I>
  static void dispatch_attrs(Parser& p, T& obj, std::index_sequence<I...>) {
    (apply_attr<T, I>(p, obj), ...);
  }

  template <typename T, size_t... I>
  static constexpr bool has_attr_fields(std::index_sequence<I...>) noexcept {
    return (... ||
            (std::get<I>(XmlMetadata<T>::fields).kind == FieldKind::Attr));
  }

  template <typename T, size_t... I>
  static constexpr bool has_elem_fields(std::index_sequence<I...>) noexcept {
    return (... ||
            (std::get<I>(XmlMetadata<T>::fields).kind != FieldKind::Attr));
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
  bool error_{};
  bool last_self_closing_{};
  bool has_peek_{false};
};

/// @brief Deserializes the root element from parser into object.
/// @tparam T        XmlObject type with an XmlMetadata specialization.
/// @param parser    Parser positioned at the start of the XML input.
/// @param root_name Expected root element name.
/// @param object    Output object to populate.
/// @return True on success, false on any parse or structure error.
template <typename T>
auto deserialize(Parser& parser, std::string_view root_name, T& object)
    -> bool {
  if (!parser.begin_element(root_name)) [[unlikely]] {
    return false;
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
inline auto Parser::peek() -> const Token* {
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

inline auto Parser::next() -> const Token* {
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

inline auto Parser::next_from_source(Token& token) -> bool {
  if (error_ || at_end()) {
    return false;
  }
  if (*cur_ == '<') {
    ++cur_;
    return parse_markup(token);
  }
  const char* start = cur_;
  const char* next_tag = static_cast<const char*>(
      std::memchr(cur_, '<', static_cast<size_t>(end_ - cur_)));
  cur_ = next_tag ? next_tag : end_;
  token.type = TokenType::Text;
  token.data = {start, static_cast<size_t>(cur_ - start)};
  return true;
}

inline auto Parser::parse_markup(Token& token) -> bool {
  if (at_end()) {
    return make_error(token, "unexpected end after '<'");
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
    const char* found = static_cast<const char*>(
        std::memchr(cur_, '>', static_cast<size_t>(end_ - cur_)));
    cur_ = found ? found + 1 : end_;
    return next_from_source(token);
  }
  if (c == '?') {
    ++cur_;
    return parse_pi(token);
  }
  if (is_name_start(c)) {
    return parse_element_open(token);
  }
  return make_error(token, "unexpected character after '<'");
}

inline auto Parser::parse_element_open(Token& token) -> bool {
  token.type = TokenType::ElementOpen;
  token.self_closing = false;
  parse_name(token.prefix, token.name, token.name_hash);
  if (token.name.empty()) {
    return make_error(token, "expected element name");
  }

  attributes_.clear();
  while (true) {
    skip_whitespace();
    if (at_end()) {
      return make_error(token, "unclosed element tag");
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
      return make_error(token, "expected attribute name");
    }

    Attribute& a = attributes_.emplace_back();
    parse_name(a.prefix, a.name, a.name_hash);
    skip_whitespace();
    if (!expect('=')) {
      return make_error(token, "expected '='");
    }
    skip_whitespace();
    const char quote = peek_char();
    if (quote != '"' && quote != '\'') {
      return make_error(token, "expected quoted attribute value");
    }
    ++cur_;
    const char* val_start = cur_;
    const char* val_end = static_cast<const char*>(
        std::memchr(cur_, quote, static_cast<size_t>(end_ - cur_)));
    if (!val_end) {
      return make_error(token, "malformed attribute value");
    }
    a.value = {val_start, static_cast<size_t>(val_end - val_start)};
    cur_ = val_end + 1;
  }
}

inline auto Parser::parse_element_close(Token& token) -> bool {
  token.type = TokenType::ElementClose;
  FieldHash name_hash;
  parse_name(token.prefix, token.name, name_hash);
  if (token.name.empty()) {
    return make_error(token, "expected name in close");
  }
  skip_whitespace();
  if (!expect('>')) {
    return make_error(token, "expected '>' in close tag");
  }
  return true;
}

template <typename T>
inline auto Parser::parse_numeric(std::string_view text, T& out) noexcept
    -> bool {
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

template <typename T>
inline auto Parser::value(std::string_view expected_name, T& out) -> bool {
  if constexpr (XmlStringLike<T>) {
    while (const Token* token = next()) {
      if (token->type == TokenType::Text) {
        out = token->data;
      }
      if (token->type == TokenType::ElementClose) {
        return token->name == expected_name;
      }
    }
    return false;
  } else {
    std::string_view text;
    if (!value(expected_name, text)) {
      return false;
    }
    return parse_numeric(text, out);
  }
}

template <typename T>
inline auto Parser::attr(const FieldHash hash, T& out) -> bool {
  const auto it = std::ranges::find_if(
      attributes_, [hash](const Attribute& a) { return a.name_hash == hash; });
  if (it == attributes_.end()) {
    return false;
  }
  if constexpr (XmlStringLike<T>) {
    out = it->value;
    return true;
  } else {
    return parse_numeric(it->value, out);
  }
}

inline auto Parser::begin_element(std::string_view expected_name) -> bool {
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

inline auto Parser::end_element(std::string_view expected_name) -> bool {
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
          return false;
        }
        consume();
        return true;
      case TokenType::ElementOpen:
        return false;
      default:
        consume();
        break;
    }
  }
  return false;
}

inline void Parser::skip_element() {
  if (last_self_closing_) {
    return;
  }
  size_t depth = 1;
  while (depth > 0) {
    const Token* t = next();
    if (!t) {
      break;
    }
    if (t->type == TokenType::ElementOpen && !t->self_closing) {
      ++depth;
    } else if (t->type == TokenType::ElementClose) {
      --depth;
    }
  }
}

// Opening tag already consumed by caller (handle_element, try_begin_element,
// or consume_peeked in the N==1 inline path).
template <typename T>
inline auto Parser::read_element(std::string_view expected_name, T& out,
                                 const uint16_t depth) -> bool {
  if (depth > kMaxDepth) {
    return false;
  }
  const bool is_self_closing = last_self_closing_;

  if constexpr (XmlPrimitive<T>) {
    if (is_self_closing) {
      if constexpr (XmlStringLike<T>) {
        return true;
      }
      return false;
    }
    // Fast path: locate closing tag directly via memchr.
    const char* found = static_cast<const char*>(
        std::memchr(cur_, '<', static_cast<size_t>(end_ - cur_)));
    if (found && found + 3 + expected_name.size() <= end_ && found[1] == '/' &&
        std::memcmp(found + 2, expected_name.data(), expected_name.size()) ==
            0 &&
        found[2 + expected_name.size()] == '>') {
      std::string_view text{cur_, static_cast<size_t>(found - cur_)};
      cur_ = found + 3 + expected_name.size();
      has_peek_ = false;
      if constexpr (XmlStringLike<T>) {
        out = text;
        return true;
      } else {
        return parse_numeric(text, out);
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

template <typename T>
inline auto Parser::pull(T& object, const uint16_t depth) -> bool {
  constexpr size_t N = std::tuple_size_v<decltype(XmlMetadata<T>::fields)>;
  constexpr auto kIdxSeq = std::make_index_sequence<N>{};
  static_assert(detail::all_unique(detail::make_field_hashes<T>()),
                "FNV-1a hash collision among field names in XmlMetadata<T>");

  // Per-field fill counters for fixed containers. Only those entries are
  // touched; the compiler elides the rest.
  std::array<size_t, (N != 0 ? N : 1)> arr_fill{};

  // Apply attribute fields only when the type actually has some.
  constexpr bool kHasAttrs = has_attr_fields<T>(kIdxSeq);
  if constexpr (kHasAttrs) {
    dispatch_attrs<T>(*this, object, kIdxSeq);
  }

  if (last_self_closing_) {
    return true;
  }

  // Document-order hint: index of the field expected next. Schema-ordered
  // XML hits the memcmp fast path below on every element; out-of-order
  // documents miss once, re-sync at the dispatch site, and stay correct.
  constexpr bool kHasElems = has_elem_fields<T>(kIdxSeq);
  static constexpr auto dispatch = build_elem_dispatch<T>(kIdxSeq);
  static constexpr auto kNames = detail::make_field_names<T>();
  static constexpr auto kNextElem = detail::make_next_elem_table<T>();
  [[maybe_unused]] size_t hint = detail::first_elem_index<T>();

  while (true) {
    if (!has_peek_) {
      skip_whitespace();
      if (at_end()) {
        return false;
      }
      if (cur_[0] == '<' && cur_ + 1 < end_ && cur_[1] == '/') {
        return true;
      }

      // Fast path: match the hinted open tag via memcmp, bypassing full
      // tokenisation (parse_name, hash, peek machinery).
      if constexpr (kHasElems && N == 1) {
        // Single-field types: compile-time tag name and direct call.
        if (try_begin_element(kNames[0])) {
          if (!read_field<T, 0>(*this, object, depth, arr_fill.data())) {
            return false;
          }
          continue;
        }
      } else if constexpr (kHasElems) {
        if (try_begin_element(kNames[hint])) {
          if (!dispatch[hint](*this, object, depth, arr_fill.data())) {
            return false;
          }
          hint = kNextElem[hint];
          continue;
        }
      }

      const char* found = static_cast<const char*>(
          std::memchr(cur_, '<', static_cast<size_t>(end_ - cur_)));
      cur_ = found ? found : end_;
    }

    const Token* token = peek();
    if (!token || token->type == TokenType::Error) [[unlikely]] {
      return false;
    }
    if (token->type == TokenType::ElementClose) {
      return true;
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
    if (!dispatch[idx](*this, object, depth, arr_fill.data())) {
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

  /// @brief Serializes obj as an XML element named tag, appending to the output string.
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
    if constexpr (kPretty) out_ += '\n';
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
      if (ec == std::errc()) out.append(buf, static_cast<size_t>(ptr - buf));
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

  template <typename T, size_t... I>
  static constexpr bool has_children(std::index_sequence<I...>) noexcept {
    return (... ||
            (std::get<I>(XmlMetadata<T>::fields).kind != FieldKind::Attr));
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
    constexpr size_t N = std::tuple_size_v<decltype(XmlMetadata<T>::fields)>;
    using Seq = std::make_index_sequence<N>;

    do_indent(depth);
    out_ += '<';
    out_ += tag;
    write_attrs<T>(obj, Seq{});

    if constexpr (has_children<T>(Seq{})) {
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

/// @file XSDCodegen.hh
/// @brief Generates LightningXML XmlMetadata definitions from an XSD schema.
///
/// The schema is itself parsed with LightningXML (XSD is XML; the xs: prefix is
/// matched by local name). A practical subset is supported; anything outside it
/// is recorded as a note rather than dropped silently or treated as fatal.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "LightningXML.hh"

namespace xsd {

// Parsed XSD model, populated by LightningXML.
struct Facet {
  std::string value;
};

struct Restriction {
  std::string base;
  std::vector<Facet> enumerations;
  std::optional<Facet> min_length;
  std::optional<Facet> max_length;
  std::optional<Facet> length;
  std::optional<Facet> pattern;
  std::optional<Facet> min_inclusive;
  std::optional<Facet> max_inclusive;
  std::optional<Facet> min_exclusive;
  std::optional<Facet> max_exclusive;
  std::optional<Facet> fraction_digits;
  std::optional<Facet> total_digits;
};

struct List {
  std::string item_type;
};

struct SimpleType {
  std::string name;
  std::optional<Restriction> restriction;
  std::optional<List> list;
};

struct Attribute {
  std::string name;
  std::string type;
  std::string use;
  std::string default_;  // NOLINT(readability-identifier-naming)
  std::string fixed;
  std::optional<SimpleType> simple_type;
};

struct AttributeGroupRef {
  std::string ref;
};

struct AttributeGroupDef {
  std::string name;
  std::vector<Attribute> attributes;
};

struct GroupRef {
  std::string ref;
};

struct Element;  // recursive via the content-model containers below

struct Choice {
  std::optional<std::string> max_occurs;
  std::vector<Element> elements;
};

struct Sequence {
  std::vector<Element> elements;
  std::vector<Choice> choices;
  std::vector<GroupRef> group_refs;
};

struct GroupDef {
  std::string name;
  std::optional<Sequence> sequence;
  std::optional<Choice> choice;
};

struct Extension {
  std::string base;
  std::vector<Attribute> attributes;
};

struct SimpleContent {
  std::optional<Extension> extension;
};

struct ComplexExtension {
  std::string base;
  std::optional<Sequence> sequence;
  std::optional<Choice> choice;
  std::vector<Attribute> attributes;
  std::vector<AttributeGroupRef> attribute_group_refs;
};

struct ComplexContent {
  std::optional<ComplexExtension> extension;
};

struct ComplexType {
  std::string name;
  bool mixed{};
  std::optional<Sequence> sequence;
  std::optional<Choice> choice;
  std::optional<SimpleContent> simple_content;
  std::optional<ComplexContent> complex_content;
  std::vector<Attribute> attributes;
  std::vector<AttributeGroupRef> attribute_group_refs;
};

struct Element {
  std::string name;
  std::string type;
  std::optional<int> min_occurs;
  std::optional<std::string> max_occurs;
  std::optional<ComplexType> complex_type;
  std::optional<SimpleType> simple_type;
};

struct Include {
  std::string schema_location;
};

struct Schema {
  std::vector<Element> elements;
  std::vector<ComplexType> complex_types;
  std::vector<SimpleType> simple_types;
  std::vector<AttributeGroupDef> attribute_groups;
  std::vector<GroupDef> groups;
  std::vector<Include> includes;
};

}  // namespace xsd

template<>
struct xmlight::XmlMetadata<xsd::Facet> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("value", &xsd::Facet::value));
};
template<>
struct xmlight::XmlMetadata<xsd::Restriction> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("base", &xsd::Restriction::base),
                      xmlight::vecField("enumeration", &xsd::Restriction::enumerations),
                      xmlight::field("minLength", &xsd::Restriction::min_length),
                      xmlight::field("maxLength", &xsd::Restriction::max_length),
                      xmlight::field("length", &xsd::Restriction::length),
                      xmlight::field("pattern", &xsd::Restriction::pattern),
                      xmlight::field("minInclusive", &xsd::Restriction::min_inclusive),
                      xmlight::field("maxInclusive", &xsd::Restriction::max_inclusive),
                      xmlight::field("minExclusive", &xsd::Restriction::min_exclusive),
                      xmlight::field("maxExclusive", &xsd::Restriction::max_exclusive),
                      xmlight::field("fractionDigits", &xsd::Restriction::fraction_digits),
                      xmlight::field("totalDigits", &xsd::Restriction::total_digits));
};
template<>
struct xmlight::XmlMetadata<xsd::List> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("itemType", &xsd::List::item_type));
};
template<>
struct xmlight::XmlMetadata<xsd::SimpleType> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("name", &xsd::SimpleType::name),
                      xmlight::field("restriction", &xsd::SimpleType::restriction),
                      xmlight::field("list", &xsd::SimpleType::list));
};
template<>
struct xmlight::XmlMetadata<xsd::Attribute> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("name", &xsd::Attribute::name),
                      xmlight::attrField("type", &xsd::Attribute::type),
                      xmlight::attrField("use", &xsd::Attribute::use),
                      xmlight::attrField("default", &xsd::Attribute::default_),
                      xmlight::attrField("fixed", &xsd::Attribute::fixed),
                      xmlight::field("simpleType", &xsd::Attribute::simple_type));
};
template<>
struct xmlight::XmlMetadata<xsd::AttributeGroupRef> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("ref", &xsd::AttributeGroupRef::ref));
};
template<>
struct xmlight::XmlMetadata<xsd::AttributeGroupDef> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("name", &xsd::AttributeGroupDef::name),
                      xmlight::vecField("attribute", &xsd::AttributeGroupDef::attributes));
};
template<>
struct xmlight::XmlMetadata<xsd::GroupRef> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("ref", &xsd::GroupRef::ref));
};
template<>
struct xmlight::XmlMetadata<xsd::Choice> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("maxOccurs", &xsd::Choice::max_occurs),
                      xmlight::vecField("element", &xsd::Choice::elements));
};
template<>
struct xmlight::XmlMetadata<xsd::Sequence> {
  static constexpr auto fields =
      std::make_tuple(xmlight::vecField("element", &xsd::Sequence::elements),
                      xmlight::vecField("choice", &xsd::Sequence::choices),
                      xmlight::vecField("group", &xsd::Sequence::group_refs));
};
template<>
struct xmlight::XmlMetadata<xsd::GroupDef> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("name", &xsd::GroupDef::name),
                      xmlight::field("sequence", &xsd::GroupDef::sequence),
                      xmlight::field("choice", &xsd::GroupDef::choice));
};
template<>
struct xmlight::XmlMetadata<xsd::Extension> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("base", &xsd::Extension::base),
                      xmlight::vecField("attribute", &xsd::Extension::attributes));
};
template<>
struct xmlight::XmlMetadata<xsd::SimpleContent> {
  static constexpr auto fields =
      std::make_tuple(xmlight::field("extension", &xsd::SimpleContent::extension));
};
template<>
struct xmlight::XmlMetadata<xsd::ComplexExtension> {
  static constexpr auto fields = std::make_tuple(
      xmlight::attrField("base", &xsd::ComplexExtension::base),
      xmlight::field("sequence", &xsd::ComplexExtension::sequence),
      xmlight::field("choice", &xsd::ComplexExtension::choice),
      xmlight::vecField("attribute", &xsd::ComplexExtension::attributes),
      xmlight::vecField("attributeGroup", &xsd::ComplexExtension::attribute_group_refs));
};
template<>
struct xmlight::XmlMetadata<xsd::ComplexContent> {
  static constexpr auto fields =
      std::make_tuple(xmlight::field("extension", &xsd::ComplexContent::extension));
};
template<>
struct xmlight::XmlMetadata<xsd::ComplexType> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("name", &xsd::ComplexType::name),
                      xmlight::attrField("mixed", &xsd::ComplexType::mixed),
                      xmlight::field("sequence", &xsd::ComplexType::sequence),
                      xmlight::field("choice", &xsd::ComplexType::choice),
                      xmlight::field("simpleContent", &xsd::ComplexType::simple_content),
                      xmlight::field("complexContent", &xsd::ComplexType::complex_content),
                      xmlight::vecField("attribute", &xsd::ComplexType::attributes),
                      xmlight::vecField("attributeGroup", &xsd::ComplexType::attribute_group_refs));
};
template<>
struct xmlight::XmlMetadata<xsd::Element> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("name", &xsd::Element::name),
                      xmlight::attrField("type", &xsd::Element::type),
                      xmlight::attrField("minOccurs", &xsd::Element::min_occurs),
                      xmlight::attrField("maxOccurs", &xsd::Element::max_occurs),
                      xmlight::field("complexType", &xsd::Element::complex_type),
                      xmlight::field("simpleType", &xsd::Element::simple_type));
};
template<>
struct xmlight::XmlMetadata<xsd::Include> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("schemaLocation", &xsd::Include::schema_location));
};
template<>
struct xmlight::XmlMetadata<xsd::Schema> {
  static constexpr auto fields =
      std::make_tuple(xmlight::vecField("element", &xsd::Schema::elements),
                      xmlight::vecField("complexType", &xsd::Schema::complex_types),
                      xmlight::vecField("simpleType", &xsd::Schema::simple_types),
                      xmlight::vecField("attributeGroup", &xsd::Schema::attribute_groups),
                      xmlight::vecField("group", &xsd::Schema::groups),
                      xmlight::vecField("include", &xsd::Schema::includes));
};

namespace xsd {

struct Options {
  // If provided, called with each xs:include schemaLocation to load external
  // schema text. Return nullopt to silently skip an include.
  std::function<std::optional<std::string>(std::string_view)> loader;
};

struct GenResult {
  std::string code;
  std::vector<std::string> notes;
  bool ok{};
};

namespace detail {

inline auto localName(std::string_view s) -> std::string_view {
  const auto pos = s.rfind(':');
  return pos == std::string_view::npos ? s : s.substr(pos + 1);
}

inline auto isIdentStart(char c) -> bool {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline auto isIdentChar(char c) -> bool {
  return isIdentStart(c) || (c >= '0' && c <= '9');
}

// Identifiers that the C standard headers (and GoogleTest, which generated
// headers routinely meet in a test translation unit) define as macros. An
// enumerator or member spelled this way is macro-expanded wherever those
// headers came first, so it takes a trailing '_'. Only the C++ spelling
// changes: the XML name in the metadata and enum tables is untouched. Extend
// the list for a toolchain that defines more (windows.h adds ERROR, DELETE,
// TRUE, min, max, ...).
inline auto isMacroName(std::string_view name) -> bool {
  static const std::unordered_set<std::string_view> k_macro_names = {
      "SIGABRT", "SIGALRM",  "SIGBUS",   "SIGCHLD",        "SIGCONT",   "SIGFPE",   "SIGHUP",
      "SIGILL",  "SIGINT",   "SIGIO",    "SIGKILL",        "SIGPIPE",   "SIGPROF",  "SIGQUIT",
      "SIGSEGV", "SIGSTOP",  "SIGSYS",   "SIGTERM",        "SIGTRAP",   "SIGTSTP",  "SIGTTIN",
      "SIGTTOU", "SIGURG",   "SIGUSR1",  "SIGUSR2",        "SIGVTALRM", "SIGWINCH", "SIGXCPU",
      "SIGXFSZ", "EOF",      "NULL",     "BUFSIZ",         "SEEK_SET",  "SEEK_CUR", "SEEK_END",
      "stdin",   "stdout",   "stderr",   "errno",          "EDOM",      "ERANGE",   "EILSEQ",
      "NAN",     "INFINITY", "HUGE_VAL", "CLOCKS_PER_SEC", "assert",    "offsetof", "TEST",
      "FAIL",    "SUCCEED"};
  return k_macro_names.contains(name);
}

// Turn an XML name into a valid C++ identifier (invalid chars -> '_').
inline auto sanitize(std::string_view name) -> std::string {
  std::string out;
  out.reserve(name.size() + 1);
  for (const char c : name) {
    out.push_back(isIdentChar(c) ? c : '_');
  }
  if (out.empty() || !isIdentStart(out.front())) {
    out.insert(out.begin(), '_');
  }
  if (isMacroName(out)) {
    out.push_back('_');
  }
  return out;
}

inline auto capitalize(std::string_view name) -> std::string {
  std::string out = sanitize(name);
  if (!out.empty() && out.front() >= 'a' && out.front() <= 'z') {
    out.front() = static_cast<char>(out.front() - 'a' + 'A');
  }
  return out;
}

// XSD built-in -> C++ type, or "" if not a known built-in.
inline auto builtinType(std::string_view xsd_type) -> std::string_view {
  static const std::unordered_map<std::string_view, std::string_view> k_map = {
      {"string", "std::string"},
      {"normalizedString", "std::string"},
      {"token", "std::string"},
      {"anyURI", "std::string"},
      {"NMTOKEN", "std::string"},
      {"Name", "std::string"},
      {"NCName", "std::string"},
      {"ID", "std::string"},
      {"IDREF", "std::string"},
      {"language", "std::string"},
      {"boolean", "bool"},
      {"int", "int"},
      {"integer", "long"},
      {"long", "long"},
      {"short", "short"},
      {"byte", "signed char"},
      {"unsignedInt", "unsigned"},
      {"unsignedLong", "unsigned long"},
      {"unsignedShort", "unsigned short"},
      {"nonNegativeInteger", "long"},
      {"positiveInteger", "long"},
      {"decimal", "double"},
      {"double", "double"},
      {"float", "float"},
      {"date", "xmlight::Date"},
      {"time", "xmlight::Time"},
      {"dateTime", "xmlight::DateTime"}};
  const auto it = k_map.find(localName(xsd_type));
  return it == k_map.end() ? std::string_view{} : it->second;
}

}  // namespace detail

class Generator {
 public:
  explicit Generator(const Schema& schema) : schema_(schema) {}

  auto run() -> GenResult {
    indexNamedTypes();
    for (const auto& st : schema_.simple_types) {
      collectSimpleType(st.name, st);
    }
    for (const auto& ag : schema_.attribute_groups) {
      for (const auto& a : ag.attributes) {
        collectAttrInline(a);
      }
    }
    for (const auto& g : schema_.groups) {
      collectContentInline(g.sequence ? &*g.sequence : nullptr, g.choice ? &*g.choice : nullptr);
    }
    for (const auto& ct : schema_.complex_types) {
      collectComplexType(detail::capitalize(ct.name), ct);
    }
    for (const auto& el : schema_.elements) {
      collectElementInline(el);
    }
    for (auto& s : structs_) {
      s.fields = buildFields(s);
    }
    orderStructs();
    // Build constraint specializations before emit() so needs_regex_ is known.
    for (const auto& s : structs_) {
      emitStructConstraints(s);
    }
    emit();
    GenResult r;
    r.code = std::move(out_);
    r.notes = std::move(notes_);
    r.ok = true;
    return r;
  }

 private:
  struct FieldOut {
    std::string member;    // C++ member declaration, e.g. "std::string name;"
    std::string name;      // the member identifier the declaration introduces
    std::string metadata;  // metadata call, e.g. xmlight::field("name", &T::name)
    bool inherited{};      // from a base class; omit from struct body but keep in metadata
    // Struct types this member needs complete at its point of declaration: a
    // by-value or optional element type, or a variant alternative. Vector and
    // unique_ptr members hold an incomplete type happily and record nothing.
    std::vector<std::string> deps{};
  };

  // The two struct names a field is emitted against. `ptr` names the type the
  // metadata takes a pointer-to-member of, which for an inherited field is the
  // derived struct; `owner` is the struct that declares the member, and is what
  // the member name must not collide with.
  struct FieldCtx {
    std::string ptr;
    std::string owner;
  };
  struct StructDef {
    std::string cpp_name;
    const ComplexType* ct;
    std::string parent_xsd;        // XSD key (named_complex_ key) of the base type; "" if none
    std::vector<FieldOut> fields;  // built once by run(), then read everywhere
  };
  struct EnumDef {
    std::string cpp_name;
    std::vector<std::string> tokens;  // original XML token spellings
  };

  // A complexType's effective content, regardless of where the schema puts it
  // (simpleContent extension, complexContent extension, or the type body).
  // Every consumer of a ComplexType walks this one shape.
  struct Content {
    const Extension* simple{};  // simpleContent extension, if any
    std::span<const Attribute> attributes;
    std::span<const AttributeGroupRef> attr_groups;
    const Sequence* sequence{};
    const Choice* choice{};
  };

  static auto contentOf(const ComplexType& ct) -> Content {
    if (ct.simple_content && ct.simple_content->extension) {
      const Extension& ext = *ct.simple_content->extension;
      return {.simple = &ext,
              .attributes = ext.attributes,
              .attr_groups = {},
              .sequence = nullptr,
              .choice = nullptr};
    }
    if (ct.complex_content && ct.complex_content->extension) {
      const ComplexExtension& ext = *ct.complex_content->extension;
      return {.attributes = ext.attributes,
              .attr_groups = ext.attribute_group_refs,
              .sequence = ext.sequence ? &*ext.sequence : nullptr,
              .choice = ext.choice ? &*ext.choice : nullptr};
    }
    return {.attributes = ct.attributes,
            .attr_groups = ct.attribute_group_refs,
            .sequence = ct.sequence ? &*ct.sequence : nullptr,
            .choice = ct.choice ? &*ct.choice : nullptr};
  }

  // Deduplicated: the same diagnostic can arise from more than one struct
  // (e.g. a missing attributeGroup referenced by several types).
  auto note(std::string msg) -> void {
    if (note_seen_.insert(msg).second) {
      notes_.push_back(std::move(msg));
    }
  }

  auto indexNamedTypes() -> void {
    for (const auto& ct : schema_.complex_types) {
      if (!ct.name.empty()) {
        named_complex_[ct.name] = &ct;
      }
    }
    for (const auto& st : schema_.simple_types) {
      if (!st.name.empty()) {
        named_simple_[st.name] = &st;
      }
    }
    for (const auto& ag : schema_.attribute_groups) {
      if (!ag.name.empty()) {
        named_attr_groups_[ag.name] = &ag;
      }
    }
    for (const auto& g : schema_.groups) {
      if (!g.name.empty()) {
        named_groups_[g.name] = &g;
      }
    }
  }

  auto collectSimpleType(std::string_view xml_name, const SimpleType& st) -> std::string {
    if (!st.restriction || st.restriction->enumerations.empty()) {
      return {};
    }
    const std::string cpp = uniqueName(detail::capitalize(xml_name));
    EnumDef e;
    e.cpp_name = cpp;
    for (const auto& en : st.restriction->enumerations) {
      e.tokens.push_back(en.value);
    }
    enums_.push_back(std::move(e));
    enum_names_.insert(cpp);
    return cpp;
  }

  auto collectComplexType(const std::string& cpp_name, const ComplexType& ct) -> void {
    if (emitted_.contains(cpp_name)) {
      return;
    }
    emitted_.insert(cpp_name);

    std::string parent_xsd;
    if (ct.complex_content && ct.complex_content->extension) {
      const std::string& base = ct.complex_content->extension->base;
      parent_xsd = std::string{detail::localName(base)};
      if (auto it = named_complex_.find(parent_xsd); it != named_complex_.end()) {
        collectComplexType(detail::capitalize(parent_xsd), *it->second);
      } else {
        note("complexContent base '" + base + "' not found in schema");
      }
    }

    structs_.push_back({cpp_name, &ct, parent_xsd, {}});

    if (ct.mixed) {
      note("mixed content on complexType '" + ct.name +
           "' is unsupported; text between child elements is ignored");
    }

    // Collect inline types from wherever they live in this CT.
    const Content c = contentOf(ct);
    for (const auto& a : c.attributes) {
      collectAttrInline(a);
    }
    collectContentInline(c.sequence, c.choice);
  }

  auto collectContentInline(const Sequence* seq, const Choice* choice) -> void {
    if (seq != nullptr) {
      for (const auto& el : seq->elements) {
        collectElementInline(el);
      }
      for (const auto& ch : seq->choices) {
        for (const auto& el : ch.elements) {
          collectElementInline(el);
        }
      }
    }
    if (choice != nullptr) {
      for (const auto& el : choice->elements) {
        collectElementInline(el);
      }
    }
  }

  auto collectElementInline(const Element& el) -> void {
    if (el.complex_type) {
      collectComplexType(uniqueName(detail::capitalize(el.name)), *el.complex_type);
    } else if (el.simple_type) {
      collectSimpleType(el.name, *el.simple_type);
    }
  }

  auto collectAttrInline(const Attribute& a) -> void {
    if (a.simple_type) {
      collectSimpleType(a.name, *a.simple_type);
    }
  }

  auto uniqueName(const std::string& base) -> std::string {
    std::string name = base;
    int n = 2;
    while (used_names_.contains(name)) {
      name = std::format("{}{}", base, n++);
    }
    used_names_.insert(name);
    return name;
  }

  // type resolution

  struct Resolved {
    std::string cpp;
    bool is_struct{};
    bool is_list{};
    bool is_enum{};
  };

  auto resolveListItem(std::string_view item_type) const -> std::string {
    if (const std::string_view b = detail::builtinType(item_type); !b.empty()) {
      return std::string{b};
    }
    const std::string key{detail::localName(item_type)};
    if (auto it = named_simple_.find(key); it != named_simple_.end()) {
      if (it->second->restriction && !it->second->restriction->enumerations.empty()) {
        return detail::capitalize(key);
      }
      return baseBuiltin(it->second->restriction ? it->second->restriction->base : "string");
    }
    return "std::string";
  }

  static auto builtinList(std::string_view type) -> bool {
    const auto n = detail::localName(type);
    return n == "NMTOKENS" || n == "IDREFS" || n == "ENTITIES";
  }

  // Resolves an inline <simpleType> (on an element or attribute) to its C++
  // type: a list's item type, the enum generated from its enumerations, or
  // the restriction base.
  auto resolveInlineSimple(const SimpleType& st, std::string_view name) -> Resolved {
    if (st.list) {
      return {resolveListItem(st.list->item_type), false, true};
    }
    if (st.restriction && !st.restriction->enumerations.empty()) {
      return {
          .cpp = detail::capitalize(name), .is_struct = false, .is_list = false, .is_enum = true};
    }
    if (st.restriction) {
      return {baseBuiltin(st.restriction->base), false, false};
    }
    note("untyped inline simpleType on '" + std::string{name} + "' -> std::string");
    return {"std::string", false, false};
  }

  auto resolveElementType(const Element& el) -> Resolved {
    if (el.complex_type) {
      return {detail::capitalize(el.name), true, false};
    }
    if (el.simple_type) {
      return resolveInlineSimple(*el.simple_type, el.name);
    }
    return resolveNamedType(el.type, el.name);
  }

  auto resolveAttrType(const Attribute& a) -> Resolved {
    if (a.simple_type) {
      return resolveInlineSimple(*a.simple_type, a.name);
    }
    return resolveNamedType(a.type, a.name);
  }

  auto baseBuiltin(std::string_view base) const -> std::string {
    if (const std::string_view b = detail::builtinType(base); !b.empty()) {
      return std::string{b};
    }
    if (auto it = named_simple_.find(std::string{detail::localName(base)});
        it != named_simple_.end() && it->second->restriction) {
      return baseBuiltin(it->second->restriction->base);
    }
    return "std::string";
  }

  auto resolveNamedType(std::string_view type, std::string_view ctx) -> Resolved {
    if (type.empty()) {
      note("element '" + std::string{ctx} + "' has no type -> std::string");
      return {"std::string", false, false};
    }
    if (builtinList(type)) {
      return {"std::string", false, true};
    }
    if (const std::string_view b = detail::builtinType(type); !b.empty()) {
      return {std::string{b}, false, false};
    }
    const std::string key{detail::localName(type)};
    if (auto it = named_complex_.find(key); it != named_complex_.end()) {
      return {detail::capitalize(key), true, false};
    }
    if (auto it = named_simple_.find(key); it != named_simple_.end()) {
      if (it->second->list) {
        return {resolveListItem(it->second->list->item_type), false, true};
      }
      if (it->second->restriction && !it->second->restriction->enumerations.empty()) {
        return {
            .cpp = detail::capitalize(key), .is_struct = false, .is_list = false, .is_enum = true};
      }
      return {baseBuiltin(it->second->restriction ? it->second->restriction->base : "string"),
              false, false};
    }
    note("unknown type '" + std::string{type} + "' on '" + std::string{ctx} + "' -> std::string");
    return {"std::string", false, false};
  }

  // cardinality

  static auto isUnbounded(const Element& el) -> bool {
    return el.max_occurs &&
           (*el.max_occurs == "unbounded" ||
            (!el.max_occurs->empty() && *el.max_occurs != "0" && *el.max_occurs != "1"));
  }
  static auto minOccursOf(const Element& el) -> int { return el.min_occurs.value_or(1); }

  // Returns the finite max_occurs bound when it's an integer > 1; empty otherwise.
  static auto finiteMaxOccurs(const Element& el) -> std::optional<int> {
    if (!el.max_occurs) {
      return {};
    }
    const std::string_view s = *el.max_occurs;
    int n{};
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (ec != std::errc{} || ptr != s.data() + s.size() || n <= 1) {
      return {};
    }
    return n;
  }

  // field generation

  // A data member may repeat neither the name of the struct declaring it nor
  // the name of its own type: both change what that name means inside the
  // class. The XML name stays in the metadata; the member takes a trailing '_'.
  static auto memberName(const FieldCtx& ctx, std::string_view xml_name,
                         std::string_view type) -> std::string {
    std::string mem = detail::sanitize(xml_name);
    if (mem == ctx.owner || mem == type) {
      mem += '_';
    }
    return mem;
  }

  auto addAttrField(const FieldCtx& ctx, const Attribute& a, std::vector<FieldOut>& out) -> void {
    const Resolved r = resolveAttrType(a);
    const std::string mem = memberName(ctx, a.name, r.cpp);
    const std::string suffix = (a.use == "required") ? ", true)" : ")";
    std::string decl;
    const std::string& init_val = !a.fixed.empty() ? a.fixed : a.default_;
    if (r.is_list) {
      decl = "std::vector<" + r.cpp + "> " + mem + ";";
    } else if (!init_val.empty()) {
      if (isStringCpp(r.cpp)) {
        decl = r.cpp + " " + mem + "{\"" + escapeStrLiteral(init_val) + "\"};";
      } else if (r.is_enum) {
        decl = r.cpp + " " + mem + "{" + r.cpp + "::" + detail::sanitize(init_val) + "};";
      } else if (r.cpp.starts_with("xmlight::")) {
        note("default/fixed value on '" + a.name + "' (" + r.cpp +
             ") is unsupported; the initializer was dropped");
        decl = r.cpp + " " + mem + "{};";
      } else {
        decl = r.cpp + " " + mem + "{" + init_val + "};";
      }
    } else {
      decl = r.cpp + " " + mem + "{};";
    }
    out.push_back(
        {.member = decl,
         .name = mem,
         .metadata = "xmlight::attrField(\"" + a.name + "\", &" + ctx.ptr + "::" + mem + suffix});
  }

  auto addElementField(const FieldCtx& ctx, const Element& el, std::vector<FieldOut>& out) -> void {
    const Resolved r = resolveElementType(el);
    const std::string mem = memberName(ctx, el.name, r.cpp);
    const std::string cpp = r.cpp;
    if (r.is_list) {
      if (isUnbounded(el)) {
        note("repeated xs:list element '" + el.name +
             "' (maxOccurs>1) is unsupported; treated as a single list");
      }
      out.push_back({.member = "std::vector<" + cpp + "> " + mem + ";",
                     .name = mem,
                     .metadata = "xmlight::listField(\"" + el.name + "\", &" + ctx.ptr +
                                 "::" + mem + (minOccursOf(el) >= 1 ? ", true)" : ")")});
      return;
    }
    if (isUnbounded(el)) {
      out.push_back({.member = "std::vector<" + cpp + "> " + mem + ";",
                     .name = mem,
                     .metadata = "xmlight::vecField(\"" + el.name + "\", &" + ctx.ptr + "::" + mem +
                                 (minOccursOf(el) >= 1 ? ", true)" : ")")});
    } else if (r.is_struct && r.cpp == ctx.owner) {
      // Direct self-reference: break the cycle with unique_ptr.
      out.push_back(
          {.member = "std::unique_ptr<" + cpp + "> " + mem + ";",
           .name = mem,
           .metadata = "xmlight::field(\"" + el.name + "\", &" + ctx.ptr + "::" + mem + ")"});
    } else if (minOccursOf(el) == 0) {
      out.push_back(
          {.member = "std::optional<" + cpp + "> " + mem + ";",
           .name = mem,
           .metadata = "xmlight::field(\"" + el.name + "\", &" + ctx.ptr + "::" + mem + ")",
           .deps = structDep(r)});
    } else {
      // A struct member is declared without "{}": value-initializing it here
      // would instantiate its destructor, and a struct holding a vector of a
      // type defined further down the header cannot supply one yet. Its own
      // members carry the initializers, so default-construction is unchanged.
      out.push_back(
          {.member = cpp + " " + mem + (r.is_struct ? ";" : "{};"),
           .name = mem,
           .metadata = "xmlight::field(\"" + el.name + "\", &" + ctx.ptr + "::" + mem + ", true)",
           .deps = structDep(r)});
    }
  }

  static auto structDep(const Resolved& r) -> std::vector<std::string> {
    if (!r.is_struct) {
      return {};
    }
    return {r.cpp};
  }

  auto addChoiceFields(const FieldCtx& ctx, const Choice& ch, std::vector<FieldOut>& out) -> void {
    std::vector<std::string> types;
    std::vector<std::string> alts;
    std::vector<std::string> deps;
    std::unordered_set<std::string> seen;
    for (const auto& el : ch.elements) {
      const Resolved r = resolveElementType(el);
      if (!seen.insert(r.cpp).second) {
        note("xs:choice in '" + ctx.ptr + "' has two branches of the same C++ type ('" + r.cpp +
             "'); the choice was skipped (std::variant needs distinct types)");
        return;
      }
      types.push_back(r.cpp);
      alts.push_back("xmlight::alt<" + r.cpp + ">(\"" + el.name + "\")");
      // std::variant instantiates every alternative, so a branch struct must be
      // complete here even when the variant itself sits inside a vector.
      if (r.is_struct) {
        deps.push_back(r.cpp);
      }
    }
    if (types.empty()) {
      return;
    }

    std::string variant = "std::variant<";
    for (size_t i = 0; i < types.size(); ++i) {
      variant += types[i] + (i + 1 < types.size() ? ", " : "");
    }
    variant += ">";

    const bool repeated = ch.max_occurs && (*ch.max_occurs == "unbounded" || *ch.max_occurs != "1");
    const std::string member_type = repeated ? "std::vector<" + variant + ">" : variant;
    std::string alts_joined;
    for (size_t i = 0; i < alts.size(); ++i) {
      alts_joined += "\n          " + alts[i] + (i + 1 < alts.size() ? "," : "");
    }

    // A type may hold several choices (own and inherited); number the members
    // so they don't collide: choice, choice2, choice3, ...
    const auto prior = static_cast<size_t>(std::ranges::count_if(out, [](const FieldOut& f) {
      return f.metadata.find("variantField(") != std::string::npos;
    }));
    const std::string mem = prior == 0 ? "choice" : std::format("choice{}", prior + 1);
    out.push_back(
        {.member = member_type + " " + mem + ";",
         .name = mem,
         .metadata = "xmlight::variantField(&" + ctx.ptr + "::" + mem + "," + alts_joined + ")",
         .deps = std::move(deps)});
  }

  auto expandAttrGroups(const FieldCtx& ctx, std::span<const AttributeGroupRef> refs,
                        std::vector<FieldOut>& out) -> void {
    for (const auto& agr : refs) {
      const std::string ref_key{detail::localName(agr.ref)};
      if (auto it = named_attr_groups_.find(ref_key); it != named_attr_groups_.end()) {
        for (const auto& a : it->second->attributes) {
          addAttrField(ctx, a, out);
        }
      } else {
        note("attributeGroup ref='" + agr.ref + "' not found in schema");
      }
    }
  }

  auto expandGroupRef(const FieldCtx& ctx, const GroupRef& gr, std::vector<FieldOut>& out) -> void {
    const std::string ref_key{detail::localName(gr.ref)};
    if (auto it = named_groups_.find(ref_key); it != named_groups_.end()) {
      const GroupDef& gd = *it->second;
      if (gd.sequence) {
        for (const auto& el : gd.sequence->elements) {
          addElementField(ctx, el, out);
        }
        for (const auto& ch : gd.sequence->choices) {
          addChoiceFields(ctx, ch, out);
        }
      }
      if (gd.choice) {
        addChoiceFields(ctx, *gd.choice, out);
      }
    } else {
      note("group ref='" + gr.ref + "' not found in schema");
    }
  }

  // Appends the own (non-inherited) fields of a complexType to out.
  auto ownFieldsOf(const FieldCtx& ctx, const ComplexType& ct, std::vector<FieldOut>& out) -> void {
    const Content c = contentOf(ct);
    if (c.simple != nullptr) {
      out.push_back({.member = baseBuiltin(c.simple->base) + " value{};",
                     .name = "value",
                     .metadata = "xmlight::valueField(&" + ctx.ptr + "::value)"});
    }
    for (const auto& a : c.attributes) {
      addAttrField(ctx, a, out);
    }
    expandAttrGroups(ctx, c.attr_groups, out);
    if (c.sequence != nullptr) {
      for (const auto& el : c.sequence->elements) {
        addElementField(ctx, el, out);
      }
      for (const auto& gr : c.sequence->group_refs) {
        expandGroupRef(ctx, gr, out);
      }
      for (const auto& ch : c.sequence->choices) {
        addChoiceFields(ctx, ch, out);
      }
    }
    if (c.choice != nullptr) {
      addChoiceFields(ctx, *c.choice, out);
    }
  }

  // Appends the inherited fields from the complexContent extension chain, each
  // marked inherited=true. The pointer-to-member names stay on the derived
  // struct so &Child::inherited_field is emitted (valid C++ via derived-class
  // access), while member names follow the base that declares them.
  auto appendInheritedFields(const std::string& derived, const ComplexType& ct,
                             std::vector<FieldOut>& out) -> void {
    if (!ct.complex_content || !ct.complex_content->extension) {
      return;
    }
    const std::string base_key{detail::localName(ct.complex_content->extension->base)};
    const auto it = named_complex_.find(base_key);
    if (it == named_complex_.end()) {
      return;
    }
    appendInheritedFields(derived, *it->second, out);
    const size_t first_own = out.size();
    ownFieldsOf({.ptr = derived, .owner = detail::capitalize(base_key)}, *it->second, out);
    for (size_t i = first_own; i < out.size(); ++i) {
      out[i].inherited = true;
    }
  }

  // Builds the full field list (inherited then own) for StructDef::fields;
  // run() calls this once per struct and every later pass reads the cache.
  auto buildFields(const StructDef& s) -> std::vector<FieldOut> {
    std::vector<FieldOut> result;
    appendInheritedFields(s.cpp_name, *s.ct, result);
    ownFieldsOf({.ptr = s.cpp_name, .owner = s.cpp_name}, *s.ct, result);
    return result;
  }

  // emission

  auto emit() -> void {
    out_ += "/// @file Generated by LightningXML xsdgen. Do not edit by hand.\n";
    if (!notes_.empty()) {
      out_ += "///\n/// Unsupported XSD constructs (skipped):\n";
      for (const auto& n : notes_) {
        out_ += "///   - " + n + "\n";
      }
    }
    out_ += "#pragma once\n\n";
    out_ += "#include <memory>\n#include <optional>\n#include <string>\n";
    if (needs_regex_) {
      out_ += "#include <regex>\n";
    }
    out_ += "#include <variant>\n#include <vector>\n\n";
    out_ += "#include \"LightningXML.hh\"\n\n";

    for (const auto& e : enums_) {
      emitEnum(e);
    }

    if (!structs_.empty()) {
      for (const auto& s : structs_) {
        out_ += "struct " + s.cpp_name + ";\n";
      }
      out_ += "\n";
    }
    for (const auto& s : structs_) {
      emitStructDef(s);
    }
    for (const auto& s : structs_) {
      emitStructMetadata(s);
    }

    if (!constraints_out_.empty()) {
      out_ += constraints_out_;
    }

    for (const auto& el : schema_.elements) {
      const Resolved r = resolveElementType(el);
      out_ += "// root: xmlight::deserialize(parser, \"" + el.name + "\", obj);  // ";
      out_ += r.cpp + "\n";
    }
  }

  auto emitEnum(const EnumDef& e) -> void {
    out_ += "enum class " + e.cpp_name + " {\n";
    for (const auto& t : e.tokens) {
      out_ += "  " + detail::sanitize(t) + ",\n";
    }
    out_ += "};\n";
    out_ += "template <>\nstruct xmlight::XmlEnumTraits<" + e.cpp_name + "> {\n";
    out_ += "  static constexpr auto values = xmlight::enumTable<" + e.cpp_name + ">({\n";
    for (const auto& t : e.tokens) {
      out_ += "      {\"" + t + "\", " + e.cpp_name + "::" + detail::sanitize(t) + "},\n";
    }
    out_ += "  });\n};\n\n";
  }

  // Member names that name a generated type, mapped to the elaborated-specifier
  // keyword that gets past the hiding. Inherited members hide names in the
  // derived scope too, so the whole field list is considered.
  auto hiddenTypeNames(const StructDef& s) const -> std::unordered_map<std::string, std::string> {
    std::unordered_map<std::string, std::string> hidden;
    for (const auto& f : s.fields) {
      if (emitted_.contains(f.name)) {
        hidden.emplace(f.name, "struct ");
      } else if (enum_names_.contains(f.name)) {
        hidden.emplace(f.name, "enum ");
      }
    }
    return hidden;
  }

  // A member declaration hides same-named types from every member after it, so
  // a later member of such a type is written in elaborated form ("struct Foo
  // bar;"). The member's own identifier is left alone: it is not in scope until
  // its declarator ends.
  static auto elaborateHidden(const FieldOut& f,
                              const std::unordered_map<std::string, std::string>& hidden)
      -> std::string {
    std::string out;
    for (size_t i = 0; i < f.member.size();) {
      if (!detail::isIdentStart(f.member[i])) {
        out.push_back(f.member[i++]);
        continue;
      }
      const size_t start = i;
      while (i < f.member.size() && detail::isIdentChar(f.member[i])) {
        ++i;
      }
      const std::string id = f.member.substr(start, i - start);
      if (id != f.name) {
        if (const auto it = hidden.find(id); it != hidden.end()) {
          out += it->second;
        }
      }
      out += id;
    }
    return out;
  }

  auto emitStructDef(const StructDef& s) -> void {
    out_ += "struct " + s.cpp_name;
    if (!s.parent_xsd.empty()) {
      out_ += " : " + detail::capitalize(s.parent_xsd);
    }
    out_ += " {\n";
    const auto hidden = hiddenTypeNames(s);
    for (const auto& f : s.fields) {
      if (!f.inherited && !f.member.empty()) {
        out_ += "  " + (hidden.empty() ? f.member : elaborateHidden(f, hidden)) + "\n";
      }
    }
    out_ += "};\n\n";
  }

  auto emitStructMetadata(const StructDef& s) -> void {
    const auto& fields = s.fields;
    out_ += "template <>\nstruct xmlight::XmlMetadata<" + s.cpp_name + "> {\n";
    out_ += "  static constexpr auto fields = std::make_tuple(\n";
    for (size_t i = 0; i < fields.size(); ++i) {
      out_ += "      " + fields[i].metadata + (i + 1 < fields.size() ? ",\n" : "\n");
    }
    if (fields.empty()) {
      out_ += "      /* no fields */\n";
    }
    out_ += "  );\n};\n\n";
  }

  // constraint generation

  static auto hasValueFacets(const Restriction& r) -> bool {
    return r.min_length || r.max_length || r.length || r.pattern || r.min_inclusive ||
           r.max_inclusive || r.min_exclusive || r.max_exclusive || r.fraction_digits ||
           r.total_digits;
  }

  static auto isStringCpp(const std::string& cpp) -> bool {
    return cpp == "std::string" || cpp == "std::string_view";
  }

  // XSD's regex flavour has constructs ECMAScript lacks: the \i \I \c \C name
  // classes, \p{...} / \P{...} categories, and character-class subtraction
  // ([a-z-[aeiou]]). std::regex reads those as something else entirely, so a
  // pattern carrying one is reported rather than enforced as a different rule.
  static auto isEcmaPattern(std::string_view pattern) -> bool {
    static constexpr std::string_view k_xsd_escapes = "iIcCpP";
    bool in_class = false;
    for (size_t i = 0; i < pattern.size(); ++i) {
      if (pattern[i] == '\\') {
        if (i + 1 < pattern.size() &&
            k_xsd_escapes.find(pattern[i + 1]) != std::string_view::npos) {
          return false;
        }
        ++i;
        continue;
      }
      if (pattern[i] == '[') {
        if (in_class) {
          return false;  // a class opening inside a class is subtraction
        }
        in_class = true;
      } else if (pattern[i] == ']') {
        in_class = false;
      }
    }
    return true;
  }

  static auto escapeStrLiteral(const std::string& s) -> std::string {
    std::string out;
    for (const char c : s) {
      if (c == '\\') {
        out += "\\\\";
      } else if (c == '"') {
        out += "\\\"";
      } else {
        out += c;
      }
    }
    return out;
  }

  auto typeRestriction(std::string_view type) const -> const Restriction* {
    const std::string key{detail::localName(type)};
    if (auto it = named_simple_.find(key); it != named_simple_.end()) {
      if (it->second->restriction && hasValueFacets(*it->second->restriction)) {
        return &*it->second->restriction;
      }
    }
    return nullptr;
  }

  auto elementRestriction(const Element& el) const -> const Restriction* {
    if (el.simple_type && el.simple_type->restriction &&
        hasValueFacets(*el.simple_type->restriction)) {
      return &*el.simple_type->restriction;
    }
    return typeRestriction(el.type);
  }

  auto attrRestriction(const Attribute& a) const -> const Restriction* {
    if (a.simple_type && a.simple_type->restriction &&
        hasValueFacets(*a.simple_type->restriction)) {
      return &*a.simple_type->restriction;
    }
    return typeRestriction(a.type);
  }

  auto buildCheckCode(const std::string& mem, const std::string& cpp, bool is_opt,
                      const Restriction& r) -> std::string {
    std::string out;
    const std::string a = is_opt ? "v." + mem + "->" : "v." + mem + ".";
    const std::string val = is_opt ? "*v." + mem : "v." + mem;
    const std::string g = is_opt ? "v." + mem + " && " : "";

    auto emit_if = [&out, &g, &mem](const std::string& cond, const std::string& msg) {
      out += "    if (" + g + cond + ") return \"" + mem + ": " + msg + "\";\n";
    };

    if (isStringCpp(cpp)) {
      if (r.min_length) {
        emit_if(a + "size() < " + r.min_length->value,
                "minLength violation (min=" + r.min_length->value + ")");
      }
      if (r.max_length) {
        emit_if(a + "size() > " + r.max_length->value,
                "maxLength violation (max=" + r.max_length->value + ")");
      }
      if (r.length) {
        emit_if(a + "size() != " + r.length->value,
                "length violation (expected=" + r.length->value + ")");
      }
      if (r.pattern && !r.pattern->value.empty()) {
        if (isEcmaPattern(r.pattern->value)) {
          needs_regex_ = true;
          out += "    { static const std::regex pat(\"" + escapeStrLiteral(r.pattern->value) +
                 "\"); if (" + g + "!std::regex_match(" + val + ", pat)) return \"" + mem +
                 ": pattern violation\"; }\n";
        } else {
          note("pattern '" + r.pattern->value +
               "' uses XSD-only regex syntax; the constraint is not enforced");
        }
      }
    } else {
      if (r.min_inclusive && !r.min_inclusive->value.empty()) {
        emit_if(val + " < " + r.min_inclusive->value,
                "minInclusive violation (min=" + r.min_inclusive->value + ")");
      }
      if (r.max_inclusive && !r.max_inclusive->value.empty()) {
        emit_if(val + " > " + r.max_inclusive->value,
                "maxInclusive violation (max=" + r.max_inclusive->value + ")");
      }
      if (r.min_exclusive && !r.min_exclusive->value.empty()) {
        emit_if(val + " <= " + r.min_exclusive->value,
                "minExclusive violation (min=" + r.min_exclusive->value + ")");
      }
      if (r.max_exclusive && !r.max_exclusive->value.empty()) {
        emit_if(val + " >= " + r.max_exclusive->value,
                "maxExclusive violation (max=" + r.max_exclusive->value + ")");
      }
    }
    return out;
  }

  // XmlConstraints is looked up on the static type, so a derived struct has to
  // repeat the checks its bases declare. Base first, matching the field order.
  auto structConstraintBody(const StructDef& s) -> std::string {
    return chainConstraintBody(s.cpp_name, *s.ct, s.cpp_name);
  }

  auto chainConstraintBody(const std::string& derived, const ComplexType& ct,
                           const std::string& owner) -> std::string {
    std::string body;
    if (ct.complex_content && ct.complex_content->extension) {
      const std::string base_key{detail::localName(ct.complex_content->extension->base)};
      if (const auto it = named_complex_.find(base_key); it != named_complex_.end()) {
        body += chainConstraintBody(derived, *it->second, detail::capitalize(base_key));
      }
    }
    return body + ownConstraintBody(ct, owner);
  }

  auto ownConstraintBody(const ComplexType& ct, const std::string& owner) -> std::string {
    std::string body;
    const FieldCtx ctx{.ptr = owner, .owner = owner};

    auto process_attr = [this, &body, &ctx](const Attribute& a) {
      const Resolved res = resolveAttrType(a);
      const std::string mem = memberName(ctx, a.name, res.cpp);
      if (!a.fixed.empty()) {
        // Custom value types (xmlight::Date etc.) have no literal spelling; the
        // dropped-initializer note was already emitted by addAttrField.
        if (isStringCpp(res.cpp)) {
          body += "    if (v." + mem + " != \"" + escapeStrLiteral(a.fixed) + "\") return \"" +
                  mem + ": fixed value violation\";\n";
        } else if (res.is_enum) {
          body += "    if (v." + mem + " != " + res.cpp + "::" + detail::sanitize(a.fixed) +
                  ") return \"" + mem + ": fixed value violation\";\n";
        } else if (!res.cpp.starts_with("xmlight::")) {
          body += "    if (v." + mem + " != " + a.fixed + ") return \"" + mem +
                  ": fixed value violation\";\n";
        }
      }
      if (a.simple_type && a.simple_type->list) {
        return;  // list values carry no scalar facet checks (as with elements)
      }
      const Restriction* r = attrRestriction(a);
      if (r == nullptr) {
        return;
      }
      body += buildCheckCode(mem, res.cpp, false, *r);
    };
    auto process_element = [this, &body, &ctx](const Element& el) {
      const Resolved res = resolveElementType(el);
      const std::string mem = memberName(ctx, el.name, res.cpp);
      if (const auto n = finiteMaxOccurs(el)) {
        body +=
            std::format("    if (v.{}.size() > {}) return \"{}: maxOccurs violation (max={})\";\n",
                        mem, *n, mem, *n);
      }
      if (isUnbounded(el) || (el.simple_type && el.simple_type->list)) {
        return;
      }
      const Restriction* r = elementRestriction(el);
      if (r == nullptr) {
        return;
      }
      body += buildCheckCode(mem, res.cpp, minOccursOf(el) == 0, *r);
    };

    // Choice branches live in a std::variant member, not in members named
    // after the branch elements, so their facets cannot be checked per-member.
    auto note_choice_facets = [this](const Choice& ch) {
      for (const auto& el : ch.elements) {
        if (elementRestriction(el) != nullptr) {
          note("facet constraints on xs:choice branch '" + el.name +
               "' are not enforced by XmlConstraints");
        }
      }
    };

    const Content c = contentOf(ct);
    if (c.simple != nullptr) {
      if (const Restriction* r = typeRestriction(c.simple->base)) {
        body += buildCheckCode("value", baseBuiltin(c.simple->base), false, *r);
      }
    }
    for (const auto& a : c.attributes) {
      process_attr(a);
    }
    if (c.sequence != nullptr) {
      for (const auto& el : c.sequence->elements) {
        process_element(el);
      }
      for (const auto& ch : c.sequence->choices) {
        note_choice_facets(ch);
      }
    }
    if (c.choice != nullptr) {
      note_choice_facets(*c.choice);
    }
    return body;
  }

  auto emitStructConstraints(const StructDef& s) -> void {
    const std::string body = structConstraintBody(s);
    if (body.empty()) {
      return;
    }
    constraints_out_ += "template <>\nstruct xmlight::XmlConstraints<" + s.cpp_name + "> {\n";
    constraints_out_ +=
        "  static auto check(const " + s.cpp_name + "& v) -> std::optional<std::string> {\n";
    constraints_out_ += body;
    constraints_out_ += "    return {};\n  }\n};\n\n";
  }

  // Topologically order structs so every type that has to be complete where a
  // struct is defined - its base class, its by-value and optional members, and
  // its variant alternatives - is defined first. Inherited fields are skipped:
  // they are declared by the base, which is itself a dependency.
  auto orderStructs() -> void {
    std::unordered_map<std::string, size_t> index;
    for (size_t i = 0; i < structs_.size(); ++i) {
      index[structs_[i].cpp_name] = i;
    }

    std::vector<std::vector<size_t>> deps(structs_.size());
    for (size_t i = 0; i < structs_.size(); ++i) {
      const auto add = [&deps, &index, i](const std::string& name) {
        if (const auto it = index.find(name); it != index.end() && it->second != i) {
          deps[i].push_back(it->second);
        }
      };
      if (!structs_[i].parent_xsd.empty()) {
        add(detail::capitalize(structs_[i].parent_xsd));
      }
      for (const auto& f : structs_[i].fields) {
        if (f.inherited) {
          continue;
        }
        for (const auto& dep : f.deps) {
          add(dep);
        }
      }
    }
    std::vector<int> state(structs_.size(), 0);  // 0=new,1=active,2=done
    std::vector<StructDef> ordered;
    auto visit = [this, &state, &deps, &ordered](auto&& self, size_t i) -> void {
      if (state[i] == 2) {
        return;
      }
      if (state[i] == 1) {
        return;
      }  // cycle: forward decl covers it
      state[i] = 1;
      for (const size_t j : deps[i]) {
        self(self, j);
      }
      state[i] = 2;
      ordered.push_back(std::move(structs_[i]));
    };
    for (size_t i = 0; i < structs_.size(); ++i) {
      visit(visit, i);
    }
    structs_ = std::move(ordered);
  }

  const Schema& schema_;
  std::unordered_map<std::string, const ComplexType*> named_complex_;
  std::unordered_map<std::string, const SimpleType*> named_simple_;
  std::unordered_map<std::string, const AttributeGroupDef*> named_attr_groups_;
  std::unordered_map<std::string, const GroupDef*> named_groups_;
  std::vector<StructDef> structs_;
  std::vector<EnumDef> enums_;
  std::unordered_set<std::string> enum_names_;
  std::unordered_set<std::string> emitted_;
  std::unordered_set<std::string> used_names_;
  std::vector<std::string> notes_;
  std::unordered_set<std::string> note_seen_;
  std::string out_;
  std::string constraints_out_;
  bool needs_regex_{false};
};

/// @brief Generates LightningXML metadata source from XSD schema text.
/// @param xsd_text Full text of the XSD document.
/// @param opts Options including an optional include-file loader callback.
[[nodiscard]] inline auto generate(std::string_view xsd_text,
                                   const Options& opts = {}) -> GenResult {
  // Normalizing: schema text carries character references (UCI writes its
  // pattern facets with &#x20;), and a raw pattern would compile into a regex
  // matching the reference itself.
  xmlight::NormalizingParser parser{xsd_text};
  Schema schema;
  if (!xmlight::deserialize(parser, "schema", schema)) {
    GenResult r;
    r.ok = false;
    r.notes.emplace_back("failed to parse the XSD as <schema> XML");
    return r;
  }

  if (opts.loader) {
    const auto merge = [](auto& dst, auto& src) {
      dst.insert(dst.end(), std::make_move_iterator(src.begin()),
                 std::make_move_iterator(src.end()));
    };
    // Worklist over schemaLocations so includes of included schemas are also
    // loaded; the seen set breaks include cycles.
    std::vector<std::string> pending;
    std::unordered_set<std::string> seen;
    for (const auto& inc : schema.includes) {
      if (seen.insert(inc.schema_location).second) {
        pending.push_back(inc.schema_location);
      }
    }
    for (size_t i = 0; i < pending.size(); ++i) {
      auto src = opts.loader(pending[i]);
      if (!src) {
        continue;
      }
      xmlight::NormalizingParser ip{*src};
      Schema included;
      if (!xmlight::deserialize(ip, "schema", included)) {
        continue;
      }
      for (const auto& inc : included.includes) {
        if (seen.insert(inc.schema_location).second) {
          pending.push_back(inc.schema_location);
        }
      }
      merge(schema.complex_types, included.complex_types);
      merge(schema.simple_types, included.simple_types);
      merge(schema.elements, included.elements);
      merge(schema.attribute_groups, included.attribute_groups);
      merge(schema.groups, included.groups);
    }
  }

  return Generator{schema}.run();
}

}  // namespace xsd

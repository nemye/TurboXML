/// @file XsdCodegen.hh
/// @brief Generates TurboXML XmlMetadata definitions from an XSD schema.
///
/// The schema is itself parsed with TurboXML (XSD is XML; the xs: prefix is
/// matched by local name). A practical subset is supported; anything outside it
/// is recorded as a note rather than dropped silently or treated as fatal.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "TurboXML.hh"

namespace xsd {

// ---- Parsed XSD model (populated by TurboXML) ----

struct Enumeration {
  std::string value;
};

struct Restriction {
  std::string base;
  std::vector<Enumeration> enumerations;
};

struct SimpleType {
  std::string name;
  std::optional<Restriction> restriction;
};

struct Attribute {
  std::string name;
  std::string type;
  std::string use;
};

struct Element;  // recursive via the content-model containers below

struct Choice {
  std::optional<std::string> maxOccurs;
  std::vector<Element> elements;
};

struct Sequence {
  std::vector<Element> elements;
  std::vector<Choice> choices;
};

struct Extension {
  std::string base;
  std::vector<Attribute> attributes;
};

struct SimpleContent {
  std::optional<Extension> extension;
};

struct ComplexType {
  std::string name;
  bool mixed{};
  std::optional<Sequence> sequence;
  std::optional<Choice> choice;
  std::optional<SimpleContent> simpleContent;
  std::vector<Attribute> attributes;
};

struct Element {
  std::string name;
  std::string type;
  std::optional<int> minOccurs;
  std::optional<std::string> maxOccurs;
  std::optional<ComplexType> complexType;
  std::optional<SimpleType> simpleType;
};

struct Schema {
  std::vector<Element> elements;
  std::vector<ComplexType> complexTypes;
  std::vector<SimpleType> simpleTypes;
};

}  // namespace xsd

template <>
struct xml::XmlMetadata<xsd::Enumeration> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("value", &xsd::Enumeration::value));
};
template <>
struct xml::XmlMetadata<xsd::Restriction> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("base", &xsd::Restriction::base),
                      xml::vec_field("enumeration", &xsd::Restriction::enumerations));
};
template <>
struct xml::XmlMetadata<xsd::SimpleType> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("name", &xsd::SimpleType::name),
                      xml::field("restriction", &xsd::SimpleType::restriction));
};
template <>
struct xml::XmlMetadata<xsd::Attribute> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("name", &xsd::Attribute::name),
                      xml::attr_field("type", &xsd::Attribute::type),
                      xml::attr_field("use", &xsd::Attribute::use));
};
template <>
struct xml::XmlMetadata<xsd::Choice> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("maxOccurs", &xsd::Choice::maxOccurs),
                      xml::vec_field("element", &xsd::Choice::elements));
};
template <>
struct xml::XmlMetadata<xsd::Sequence> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("element", &xsd::Sequence::elements),
                      xml::vec_field("choice", &xsd::Sequence::choices));
};
template <>
struct xml::XmlMetadata<xsd::Extension> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("base", &xsd::Extension::base),
                      xml::vec_field("attribute", &xsd::Extension::attributes));
};
template <>
struct xml::XmlMetadata<xsd::SimpleContent> {
  static constexpr auto fields = std::make_tuple(
      xml::field("extension", &xsd::SimpleContent::extension));
};
template <>
struct xml::XmlMetadata<xsd::ComplexType> {
  static constexpr auto fields = std::make_tuple(
      xml::attr_field("name", &xsd::ComplexType::name),
      xml::attr_field("mixed", &xsd::ComplexType::mixed),
      xml::field("sequence", &xsd::ComplexType::sequence),
      xml::field("choice", &xsd::ComplexType::choice),
      xml::field("simpleContent", &xsd::ComplexType::simpleContent),
      xml::vec_field("attribute", &xsd::ComplexType::attributes));
};
template <>
struct xml::XmlMetadata<xsd::Element> {
  static constexpr auto fields = std::make_tuple(
      xml::attr_field("name", &xsd::Element::name),
      xml::attr_field("type", &xsd::Element::type),
      xml::attr_field("minOccurs", &xsd::Element::minOccurs),
      xml::attr_field("maxOccurs", &xsd::Element::maxOccurs),
      xml::field("complexType", &xsd::Element::complexType),
      xml::field("simpleType", &xsd::Element::simpleType));
};
template <>
struct xml::XmlMetadata<xsd::Schema> {
  static constexpr auto fields = std::make_tuple(
      xml::vec_field("element", &xsd::Schema::elements),
      xml::vec_field("complexType", &xsd::Schema::complexTypes),
      xml::vec_field("simpleType", &xsd::Schema::simpleTypes));
};

namespace xsd {

struct Options {};

struct GenResult {
  std::string code;
  std::vector<std::string> notes;
  bool ok{};
};

namespace detail {

// ---- small string helpers ----

inline auto local_name(std::string_view s) -> std::string_view {
  const auto pos = s.rfind(':');
  return pos == std::string_view::npos ? s : s.substr(pos + 1);
}

inline auto is_ident_start(char c) -> bool {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline auto is_ident_char(char c) -> bool {
  return is_ident_start(c) || (c >= '0' && c <= '9');
}

// Turn an XML name into a valid C++ identifier (invalid chars -> '_').
inline auto sanitize(std::string_view name) -> std::string {
  std::string out;
  out.reserve(name.size() + 1);
  for (char c : name) {
    out.push_back(is_ident_char(c) ? c : '_');
  }
  if (out.empty() || !is_ident_start(out.front())) {
    out.insert(out.begin(), '_');
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
inline auto builtin_type(std::string_view xsd_type) -> std::string {
  static const std::unordered_map<std::string_view, std::string_view> kMap = {
      {"string", "std::string"},        {"normalizedString", "std::string"},
      {"token", "std::string"},         {"anyURI", "std::string"},
      {"NMTOKEN", "std::string"},       {"Name", "std::string"},
      {"NCName", "std::string"},        {"ID", "std::string"},
      {"IDREF", "std::string"},         {"language", "std::string"},
      {"boolean", "bool"},              {"int", "int"},
      {"integer", "long"},              {"long", "long"},
      {"short", "short"},               {"byte", "signed char"},
      {"unsignedInt", "unsigned"},      {"unsignedLong", "unsigned long"},
      {"unsignedShort", "unsigned short"}, {"nonNegativeInteger", "long"},
      {"positiveInteger", "long"},      {"decimal", "double"},
      {"double", "double"},             {"float", "float"},
      {"date", "xml::Date"},            {"time", "xml::Time"},
      {"dateTime", "xml::DateTime"}};
  const auto it = kMap.find(local_name(xsd_type));
  return it == kMap.end() ? std::string{} : std::string{it->second};
}

}  // namespace detail

// ---- Generator ----

class Generator {
 public:
  explicit Generator(const Schema& schema) : schema_(schema) {}

  auto run() -> GenResult {
    index_named_types();
    // Collect every struct/enum to emit (named + inline, recursively).
    for (const auto& st : schema_.simpleTypes) {
      collect_simple_type(st.name, st);
    }
    for (const auto& ct : schema_.complexTypes) {
      collect_complex_type(detail::capitalize(ct.name), ct);
    }
    for (const auto& el : schema_.elements) {
      collect_element_inline(el);
    }
    emit();
    GenResult r;
    r.code = std::move(out_);
    r.notes = std::move(notes_);
    r.ok = true;
    return r;
  }

 private:
  // A struct to emit: its C++ name and the complexType it came from.
  struct StructDef {
    std::string cpp_name;
    const ComplexType* ct;
  };
  struct EnumDef {
    std::string cpp_name;
    std::vector<std::string> tokens;  // original XML token spellings
  };

  // Deduplicated: fields_of() is computed several times (ordering, struct, and
  // metadata emission), so the same diagnostic can be produced more than once.
  void note(std::string msg) {
    if (note_seen_.insert(msg).second) notes_.push_back(std::move(msg));
  }

  void index_named_types() {
    for (const auto& ct : schema_.complexTypes) {
      if (!ct.name.empty()) named_complex_[ct.name] = &ct;
    }
    for (const auto& st : schema_.simpleTypes) {
      if (!st.name.empty()) named_simple_[st.name] = &st;
    }
  }

  // Register an enum (named or inline). Returns its C++ name, or "" if the
  // simpleType isn't an enumeration we can map.
  auto collect_simple_type(std::string_view xml_name, const SimpleType& st)
      -> std::string {
    if (!st.restriction || st.restriction->enumerations.empty()) {
      return {};  // not an enum; callers fall back to the base built-in
    }
    const std::string cpp = unique_name(detail::capitalize(xml_name));
    EnumDef e;
    e.cpp_name = cpp;
    for (const auto& en : st.restriction->enumerations) {
      e.tokens.push_back(en.value);
    }
    enums_.push_back(std::move(e));
    return cpp;
  }

  void collect_complex_type(std::string cpp_name, const ComplexType& ct) {
    if (emitted_.count(cpp_name)) return;
    emitted_.insert(cpp_name);
    structs_.push_back({cpp_name, &ct});
    if (ct.mixed) {
      note("mixed content on complexType '" + ct.name +
           "' is unsupported; text between child elements is ignored");
    }
    if (ct.sequence) {
      for (const auto& el : ct.sequence->elements) collect_element_inline(el);
      for (const auto& ch : ct.sequence->choices) {
        for (const auto& el : ch.elements) collect_element_inline(el);
      }
    }
    if (ct.choice) {
      for (const auto& el : ct.choice->elements) collect_element_inline(el);
    }
  }

  // Register inline struct/enum types declared directly on an element.
  void collect_element_inline(const Element& el) {
    if (el.complexType) {
      collect_complex_type(unique_name(detail::capitalize(el.name)),
                           *el.complexType);
    } else if (el.simpleType) {
      collect_simple_type(el.name, *el.simpleType);
    }
  }

  auto unique_name(std::string base) -> std::string {
    std::string name = base;
    int n = 2;
    while (used_names_.count(name)) name = base + std::to_string(n++);
    used_names_.insert(name);
    return name;
  }

  // ---- type resolution ----

  // The C++ type for an element's value (resolving builtins, named refs, and
  // inline declarations), and whether it names an emitted struct.
  struct Resolved {
    std::string cpp;
    bool is_struct{};
  };

  auto resolve_element_type(const Element& el) -> Resolved {
    if (el.complexType) {
      return {detail::capitalize(el.name), true};
    }
    if (el.simpleType) {
      if (std::string e = enum_name_for_inline(el); !e.empty()) {
        return {e, false};
      }
      if (el.simpleType->restriction) {
        return {base_builtin(el.simpleType->restriction->base), false};
      }
      note("untyped inline simpleType on '" + el.name + "' -> std::string");
      return {"std::string", false};
    }
    return resolve_named_type(el.type, el.name);
  }

  auto enum_name_for_inline(const Element& el) -> std::string {
    if (!el.simpleType || !el.simpleType->restriction ||
        el.simpleType->restriction->enumerations.empty()) {
      return {};
    }
    // Inline enums were registered under capitalize(element name).
    return detail::capitalize(el.name);
  }

  auto base_builtin(std::string_view base) -> std::string {
    if (std::string b = detail::builtin_type(base); !b.empty()) return b;
    // base might itself be a named simpleType
    if (auto it = named_simple_.find(std::string{detail::local_name(base)});
        it != named_simple_.end() && it->second->restriction) {
      return base_builtin(it->second->restriction->base);
    }
    return "std::string";
  }

  auto resolve_named_type(std::string_view type, std::string_view ctx)
      -> Resolved {
    if (type.empty()) {
      note("element '" + std::string{ctx} +
           "' has no type -> std::string");
      return {"std::string", false};
    }
    if (std::string b = detail::builtin_type(type); !b.empty()) {
      return {b, false};
    }
    const std::string key{detail::local_name(type)};
    if (auto it = named_complex_.find(key); it != named_complex_.end()) {
      return {detail::capitalize(key), true};
    }
    if (auto it = named_simple_.find(key); it != named_simple_.end()) {
      if (it->second->restriction && !it->second->restriction->enumerations.empty()) {
        return {detail::capitalize(key), false};
      }
      return {base_builtin(it->second->restriction ? it->second->restriction->base
                                                    : "string"),
              false};
    }
    note("unknown type '" + std::string{type} + "' on '" + std::string{ctx} +
         "' -> std::string");
    return {"std::string", false};
  }

  // ---- cardinality ----

  static auto is_unbounded(const Element& el) -> bool {
    return el.maxOccurs && (*el.maxOccurs == "unbounded" ||
                            (!el.maxOccurs->empty() && *el.maxOccurs != "0" &&
                             *el.maxOccurs != "1"));
  }
  static auto min_occurs(const Element& el) -> int {
    return el.minOccurs.value_or(1);
  }

  // ---- emission ----

  void emit() {
    out_ += "/// @file Generated by TurboXML xsdgen. Do not edit by hand.\n";
    if (!notes_.empty()) {
      out_ += "///\n/// Unsupported XSD constructs (skipped):\n";
      for (const auto& n : notes_) out_ += "///   - " + n + "\n";
    }
    out_ += "#pragma once\n\n";
    out_ += "#include <memory>\n#include <optional>\n#include <string>\n";
    out_ += "#include <variant>\n#include <vector>\n\n";
    out_ += "#include \"TurboXML.hh\"\n\n";

    for (const auto& e : enums_) emit_enum(e);

    order_structs();
    if (!structs_.empty()) {
      for (const auto& s : structs_) out_ += "struct " + s.cpp_name + ";\n";
      out_ += "\n";
    }
    for (const auto& s : structs_) emit_struct_def(s);
    for (const auto& s : structs_) emit_struct_metadata(s);

    for (const auto& el : schema_.elements) {
      const Resolved r = resolve_element_type(el);
      out_ += "// root: xml::deserialize(parser, \"" + el.name + "\", obj);  // ";
      out_ += r.cpp + "\n";
    }
  }

  void emit_enum(const EnumDef& e) {
    out_ += "enum class " + e.cpp_name + " {\n";
    for (const auto& t : e.tokens) out_ += "  " + detail::sanitize(t) + ",\n";
    out_ += "};\n";
    out_ += "template <>\nstruct xml::XmlEnumTraits<" + e.cpp_name + "> {\n";
    out_ += "  static constexpr auto values = xml::enum_table<" + e.cpp_name +
            ">({\n";
    for (const auto& t : e.tokens) {
      out_ += "      {\"" + t + "\", " + e.cpp_name + "::" + detail::sanitize(t) +
              "},\n";
    }
    out_ += "  });\n};\n\n";
  }

  // A field destined for a struct: its members and metadata line.
  struct FieldOut {
    std::string member;    // e.g. "std::string name;"
    std::string metadata;  // e.g. xml::field("name", &T::name)
  };

  void emit_struct_def(const StructDef& s) {
    out_ += "struct " + s.cpp_name + " {\n";
    for (const auto& f : fields_of(s)) out_ += "  " + f.member + "\n";
    out_ += "};\n\n";
  }

  void emit_struct_metadata(const StructDef& s) {
    const auto fields = fields_of(s);
    out_ += "template <>\nstruct xml::XmlMetadata<" + s.cpp_name + "> {\n";
    out_ += "  static constexpr auto fields = std::make_tuple(\n";
    for (size_t i = 0; i < fields.size(); ++i) {
      out_ += "      " + fields[i].metadata +
              (i + 1 < fields.size() ? ",\n" : "\n");
    }
    if (fields.empty()) out_ += "      /* no fields */\n";
    out_ += "  );\n};\n\n";
  }

  // Build the field list for a complexType (attributes, elements, simpleContent,
  // choice). Pure with respect to out_; called for both def and metadata so the
  // two stay in lockstep.
  auto fields_of(const StructDef& s) -> std::vector<FieldOut> {
    std::vector<FieldOut> out;
    const ComplexType& ct = *s.ct;

    auto add_attrs = [&](const std::vector<Attribute>& attrs) {
      for (const auto& a : attrs) {
        const std::string cpp = base_builtin(a.type);
        const std::string mem = detail::sanitize(a.name);
        const bool req = a.use == "required";
        out.push_back(
            {cpp + " " + mem + "{};",
             "xml::attr_field(\"" + a.name + "\", &" + s.cpp_name + "::" + mem +
                 (req ? ", true)" : ")")});
      }
    };

    add_attrs(ct.attributes);

    if (ct.simpleContent && ct.simpleContent->extension) {
      // simpleContent: element text -> value_field; carry the attributes.
      const auto& ext = *ct.simpleContent->extension;
      const std::string cpp = base_builtin(ext.base);
      out.push_back({cpp + " value{};",
                     "xml::value_field(&" + s.cpp_name + "::value)"});
      add_attrs(ext.attributes);
      return out;
    }

    auto add_element = [&](const Element& el) {
      const Resolved r = resolve_element_type(el);
      const std::string mem = detail::sanitize(el.name);
      std::string cpp = r.cpp;
      if (is_unbounded(el)) {
        out.push_back({"std::vector<" + cpp + "> " + mem + ";",
                       "xml::vec_field(\"" + el.name + "\", &" + s.cpp_name +
                           "::" + mem + (min_occurs(el) >= 1 ? ", true)" : ")")});
      } else if (r.is_struct && r.cpp == s.cpp_name) {
        // Direct self-reference: break the cycle with unique_ptr.
        out.push_back({"std::unique_ptr<" + cpp + "> " + mem + ";",
                       "xml::field(\"" + el.name + "\", &" + s.cpp_name +
                           "::" + mem + ")"});
      } else if (min_occurs(el) == 0) {
        out.push_back({"std::optional<" + cpp + "> " + mem + ";",
                       "xml::field(\"" + el.name + "\", &" + s.cpp_name +
                           "::" + mem + ")"});
      } else {
        out.push_back({cpp + " " + mem + "{};",
                       "xml::field(\"" + el.name + "\", &" + s.cpp_name +
                           "::" + mem + ", true)"});
      }
    };

    if (ct.sequence) {
      for (const auto& el : ct.sequence->elements) add_element(el);
      for (const auto& ch : ct.sequence->choices) add_choice(ct, s, ch, out);
    }
    if (ct.choice) add_choice(ct, s, *ct.choice, out);
    return out;
  }

  void add_choice(const ComplexType&, const StructDef& s, const Choice& ch,
                  std::vector<FieldOut>& out) {
    std::vector<std::string> types;
    std::vector<std::string> alts;
    std::unordered_set<std::string> seen;
    for (const auto& el : ch.elements) {
      const Resolved r = resolve_element_type(el);
      if (!seen.insert(r.cpp).second) {
        note("xs:choice in '" + s.cpp_name +
             "' has two branches of the same C++ type ('" + r.cpp +
             "'); the choice was skipped (std::variant needs distinct types)");
        return;
      }
      types.push_back(r.cpp);
      alts.push_back("xml::alt<" + r.cpp + ">(\"" + el.name + "\")");
    }
    if (types.empty()) return;

    std::string variant = "std::variant<";
    for (size_t i = 0; i < types.size(); ++i)
      variant += types[i] + (i + 1 < types.size() ? ", " : "");
    variant += ">";

    const bool repeated =
        ch.maxOccurs && (*ch.maxOccurs == "unbounded" || *ch.maxOccurs != "1");
    const std::string member_type =
        repeated ? "std::vector<" + variant + ">" : variant;
    std::string alts_joined;
    for (size_t i = 0; i < alts.size(); ++i)
      alts_joined += "\n          " + alts[i] + (i + 1 < alts.size() ? "," : "");

    out.push_back(
        {member_type + " choice;",
         "xml::variant_field(&" + s.cpp_name + "::choice," + alts_joined + ")"});
  }

  // Topologically order structs so a by-value/optional member's type is defined
  // first. Cycles via vector/unique_ptr need only the forward declaration.
  void order_structs() {
    std::unordered_map<std::string, size_t> index;
    for (size_t i = 0; i < structs_.size(); ++i)
      index[structs_[i].cpp_name] = i;

    std::vector<std::vector<size_t>> deps(structs_.size());
    for (size_t i = 0; i < structs_.size(); ++i) {
      for (const auto& f : fields_of(structs_[i])) {
        // Only by-value/optional members create an ordering edge. Detect them
        // by the absence of vector</unique_ptr< wrappers in the member type.
        if (f.member.rfind("std::vector<", 0) == 0 ||
            f.member.rfind("std::unique_ptr<", 0) == 0) {
          continue;
        }
        for (const auto& [name, j] : index) {
          if (j != i && member_references(f.member, name)) {
            deps[i].push_back(j);
          }
        }
      }
    }
    std::vector<int> state(structs_.size(), 0);  // 0=new,1=active,2=done
    std::vector<StructDef> ordered;
    auto visit = [&](auto&& self, size_t i) -> void {
      if (state[i] == 2) return;
      if (state[i] == 1) return;  // cycle: forward decl covers it
      state[i] = 1;
      for (size_t j : deps[i]) self(self, j);
      state[i] = 2;
      ordered.push_back(structs_[i]);
    };
    for (size_t i = 0; i < structs_.size(); ++i) visit(visit, i);
    structs_ = std::move(ordered);
  }

  static auto member_references(const std::string& member,
                                const std::string& type) -> bool {
    // Matches the leading "<type> " or "std::optional<type>" of a member decl.
    const std::string opt = "std::optional<" + type + ">";
    return member.rfind(type + " ", 0) == 0 || member.rfind(opt, 0) == 0;
  }

  const Schema& schema_;
  std::unordered_map<std::string, const ComplexType*> named_complex_;
  std::unordered_map<std::string, const SimpleType*> named_simple_;
  std::vector<StructDef> structs_;
  std::vector<EnumDef> enums_;
  std::unordered_set<std::string> emitted_;
  std::unordered_set<std::string> used_names_;
  std::vector<std::string> notes_;
  std::unordered_set<std::string> note_seen_;
  std::string out_;
};

/// @brief Generates TurboXML metadata source from XSD schema text.
inline auto generate(std::string_view xsd_text, const Options& = {})
    -> GenResult {
  xml::Parser parser{xsd_text};
  Schema schema;
  if (!xml::deserialize(parser, "schema", schema)) {
    GenResult r;
    r.ok = false;
    r.notes.push_back("failed to parse the XSD as <schema> XML");
    return r;
  }
  return Generator{schema}.run();
}

}  // namespace xsd

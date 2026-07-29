/// @file fuzz_LightningXML.cc
/// @brief libFuzzer entry point driving every parser mode over one input.
///
/// The parser is a hand-written scanner built on raw pointer arithmetic,
/// bounded memchr runs, and SWAR chunking, so the inputs worth testing are the
/// ones nobody thought to write down. Every mode sees the same buffer: the
/// default parser exercises the zero-copy fast paths, the normalizing parser
/// the reference-expansion and line-ending paths, and the strict parser the
/// well-formedness checks and the wide-attribute probe table.
///
/// A parse returning false is a normal outcome, not a finding. What this
/// catches is a crash, a sanitizer report, or a round-trip that loses data.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "LightningXML.hh"

namespace {

enum class Colour : uint8_t { Red, Green, Blue };

struct Inner {
  std::string text;
  int weight{};
};

struct Alt {
  std::string label;
};

/// Recursive through unique_ptr so the depth guard and the allocate-then-parse
/// path are both reachable from crafted nesting.
struct Node {
  int id{};
  std::string name;
  std::optional<std::string> note;
  Colour colour{};
  Inner inner;
  std::vector<Inner> items;
  std::vector<int> numbers;
  std::variant<Alt, int> choice;
  std::unique_ptr<Node> child;
};

}  // namespace

template<>
struct xmlight::XmlEnumTraits<Colour> {
  static constexpr auto values =
      xmlight::enumTable<Colour>({{"red", Colour::Red}, {"green", Colour::Green},
                                  {"blue", Colour::Blue}});
};

template<>
struct xmlight::XmlMetadata<Inner> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("w", &Inner::weight),
                                                 xmlight::valueField(&Inner::text));
};

template<>
struct xmlight::XmlMetadata<Alt> {
  static constexpr auto fields = std::make_tuple(xmlight::valueField(&Alt::label));
};

template<>
struct xmlight::XmlMetadata<Node> {
  static constexpr auto fields = std::make_tuple(
      xmlight::attrField("id", &Node::id), xmlight::attrField("name", &Node::name),
      xmlight::attrField("note", &Node::note), xmlight::attrField("colour", &Node::colour),
      xmlight::field("Inner", &Node::inner), xmlight::vecField("Item", &Node::items),
      xmlight::listField("Numbers", &Node::numbers),
      xmlight::variantField(&Node::choice, xmlight::alt<Alt>("Alt"), xmlight::alt<int>("Num")),
      xmlight::field("Child", &Node::child));
};

namespace {

template<typename ParserT>
auto parseWith(std::string_view src, Node& out) -> bool {
  ParserT parser{src};
  return xmlight::deserialize(parser, "Root", out);
}

/// Serializing a parsed object and reading it back must reproduce it. This is
/// the check that catches escaping that a conforming reader would undo -- the
/// failure mode a crash-only fuzzer never sees.
auto checkRoundTrip(const Node& node) -> void {
  const std::string xml = xmlight::serialize<false>("Root", node);
  Node reparsed;
  if (!parseWith<xmlight::StrictParser>(xml, reparsed)) {
    __builtin_trap();  // our own output must always parse
  }
  const std::string again = xmlight::serialize<false>("Root", reparsed);
  if (again != xml) {
    __builtin_trap();  // a second pass must be a fixed point
  }
}

}  // namespace

extern "C" auto LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) -> int {
  const std::string_view src{reinterpret_cast<const char*>(data), size};

  Node basic;
  const bool ok = parseWith<xmlight::Parser>(src, basic);

  Node normalized;
  std::ignore = parseWith<xmlight::NormalizingParser>(src, normalized);

  Node strict;
  const bool strict_ok = parseWith<xmlight::StrictParser>(src, strict);

  // The default parser accepts a superset of what the strict parser does, so a
  // document the strict parser takes must also satisfy the default one.
  if (strict_ok && !ok) {
    __builtin_trap();
  }
  if (strict_ok) {
    checkRoundTrip(strict);
  }
  return 0;
}

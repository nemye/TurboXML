/// @file bench_TurboXML.cc
/// @brief Performance benchmarks for TurboXML's pull parser.

#include <benchmark/benchmark.h>

#include <string>
#include <string_view>
#include <vector>

#include "Helpers.hh"
#include "TurboXML.hh"

// ---- Benchmark record types ----
//
// Record/metadata declarations used by the field-handling benchmarks below
// (required-vs-optional tracking and string normalization). Each pairs an
// xml::XmlMetadata specialization with its struct; the per-benchmark comments
// further down explain how they are compared.

// Optional fields (default): no presence tracking is emitted.
struct OptItem {
  int id{};
  std::string_view title;
  std::string_view desc;
  int status{};
};

template <>
struct xml::XmlMetadata<OptItem> {
  static constexpr auto fields = std::make_tuple(
      xml::attr_field("id", &OptItem::id), xml::field("title", &OptItem::title),
      xml::field("desc", &OptItem::desc),
      xml::field("status", &OptItem::status));
};

struct OptItemList {
  std::vector<OptItem> items;
};

template <>
struct xml::XmlMetadata<OptItemList> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("Item", &OptItemList::items));
};

// Required fields: same layout, every field marked required (worst case).
struct ReqItem {
  int id{};
  std::string_view title;
  std::string_view desc;
  int status{};
};

template <>
struct xml::XmlMetadata<ReqItem> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("id", &ReqItem::id, true),
                      xml::field("title", &ReqItem::title, true),
                      xml::field("desc", &ReqItem::desc, true),
                      xml::field("status", &ReqItem::status, true));
};

struct ReqItemList {
  std::vector<ReqItem> items;
};

template <>
struct xml::XmlMetadata<ReqItemList> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("Item", &ReqItemList::items, true));
};

// Owning std::string fields: exercises the raw-copy and normalization paths.
struct NormItem {
  int id{};
  std::string title;
  std::string desc;
  int status{};
};

template <>
struct xml::XmlMetadata<NormItem> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("id", &NormItem::id),
                      xml::field("title", &NormItem::title),
                      xml::field("desc", &NormItem::desc),
                      xml::field("status", &NormItem::status));
};

struct NormItemList {
  std::vector<NormItem> items;
};

template <>
struct xml::XmlMetadata<NormItemList> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("Item", &NormItemList::items));
};

// Helper functions
static void build_tree_xml(std::string& xml, int depth, int branching) {
  if (depth <= 0) {
    xml += "<Node/>";
    return;
  }
  xml += "<Node>";
  for (int i = 0; i < branching; ++i) {
    build_tree_xml(xml, depth - 1, branching);
  }
  xml += "</Node>";
}

// Payload generators
static auto GenerateLargeXml(size_t count) -> std::string {
  std::string xml = "<?xml version=\"1.0\"?>\n<Users>\n";
  xml.reserve(count * 100 + 30);
  for (size_t i = 0; i < count; ++i) {
    xml += "  <User id=\"" + std::to_string(i) + "\">\n";
    xml += "    <Name>Benchmark User " + std::to_string(i) + "</Name>\n";
    xml += "    <Email>user" + std::to_string(i) + "@example.com</Email>\n";
    xml += "  </User>\n";
  }
  xml += "</Users>";
  return xml;
}

static auto GenerateFlatXml(size_t count) -> std::string {
  std::string xml = "<?xml version=\"1.0\"?>\n<FlatList>\n";
  xml.reserve(count * 150 + 50);
  for (size_t i = 0; i < count; ++i) {
    const std::string idx = std::to_string(i);
    xml += "  <Item id=\"" + idx + "\">\n";
    xml += "    <title>Item Title " + idx + "</title>\n";
    xml += "    <desc>Some relatively short description text here.</desc>\n";
    xml += "    <status>1</status>\n";
    xml += "  </Item>\n";
  }
  xml += "</FlatList>";
  return xml;
}

static auto GenerateDeepXml(size_t count) -> std::string {
  std::string xml = "<?xml version=\"1.0\"?>\n<DeepList>\n";
  xml.reserve(count * 150 + 50);
  for (size_t i = 0; i < count; ++i) {
    xml += "  <L1><L2><L3><L4><L5>\n";
    xml += "    <v>" + std::to_string(i) + "</v>\n";
    xml += "  </L5></L4></L3></L2></L1>\n";
  }
  xml += "</DeepList>";
  return xml;
}

static auto GenerateAttrXml(size_t count) -> std::string {
  std::string xml = "<?xml version=\"1.0\"?>\n<AttrList>\n";
  xml.reserve(count * 200 + 50);
  for (size_t i = 0; i < count; ++i) {
    const std::string idx = std::to_string(i);
    xml += "  <Item a1=\"" + idx + "\" a2=\"2\" a3=\"3\" a4=\"4\" a5=\"5\" ";
    xml += "s1=\"str1\" s2=\"str2\" s3=\"str3\" s4=\"str4\" s5=\"str5\"/>\n";
  }
  xml += "</AttrList>";
  return xml;
}

static auto GenerateOrgXml(size_t teams, size_t members) -> std::string {
  std::string xml = "<?xml version=\"1.0\"?>\n";
  xml += R"(<Organization id="1" name="Acme Corp">)"
         "\n";
  for (size_t d = 0; d < 2; ++d) {
    xml += "  <Department id=\"" + std::to_string(d) + "\" name=\"Dept" +
           std::to_string(d) + "\">\n";
    for (size_t t = 0; t < teams; ++t) {
      size_t tid = d * teams + t;
      xml += "    <Team id=\"" + std::to_string(tid) + "\" name=\"Team" +
             std::to_string(tid) + "\">\n";
      for (size_t m = 0; m < members; ++m) {
        std::string mid = std::to_string(tid * members + m);
        xml += "      <Member id=\"" + mid + "\" role=\"Engineer\">\n";
        xml += "        <FullName>Member " + mid + "</FullName>\n";
        xml += "        <Email>m" + mid + "@acme.com</Email>\n";
        xml +=
            "        <Skills><Skill>C++</Skill><Skill>Python</Skill>"
            "<Skill>Rust</Skill></Skills>\n";
        xml += "      </Member>\n";
      }
      xml += "    </Team>\n";
    }
    xml += "  </Department>\n";
  }
  xml += "</Organization>";
  return xml;
}

static auto GenerateTreeXml(int depth, int branching) -> std::string {
  std::string xml;
  xml.reserve(1 << (depth + 4));
  build_tree_xml(xml, depth, branching);
  return xml;
}

static auto GenerateCommentHeavyXml(size_t count) -> std::string {
  // ~500 byte comments to exercise memchr-accelerated scan_to_delimiter.
  const std::string filler(480, '=');
  std::string xml = "<?xml version=\"1.0\"?>\n<Users>\n";
  xml.reserve(count * 700);
  for (size_t i = 0; i < count; ++i) {
    xml += "  <!-- comment " + std::to_string(i) + " " + filler + " -->\n";
    xml += "  <User id=\"" + std::to_string(i) + "\">\n";
    xml += "    <Name>User " + std::to_string(i) + "</Name>\n";
    xml += "    <Email>u" + std::to_string(i) + "@e.com</Email>\n";
    xml += "  </User>\n";
  }
  xml += "</Users>";
  return xml;
}

static auto GenerateUnknownHeavyXml(size_t count) -> std::string {
  // Each User carries a large unmapped <Meta> subtree the parser must skip:
  // nested elements, attributes (including a quoted '>'), comments, CDATA.
  std::string xml = "<?xml version=\"1.0\"?>\n<Users>\n";
  xml.reserve(count * 700);
  for (size_t i = 0; i < count; ++i) {
    const std::string idx = std::to_string(i);
    xml += "  <User id=\"" + idx + "\">\n";
    xml += "    <Name>User " + idx + "</Name>\n";
    xml += "    <Meta source=\"import\" rev=\"4\">\n";
    xml += "      <Created by=\"sys\">2026-01-01T00:00:00Z</Created>\n";
    xml += "      <Tags><Tag v=\"a\"/><Tag v=\"b\"/><Tag v=\"c\"/></Tags>\n";
    xml += "      <Note label=\"x > y\">free text of moderate length here";
    xml += " to give the scanner something to chew on</Note>\n";
    xml += "      <!-- audit: imported > converted -->\n";
    xml += "      <![CDATA[ raw <blob> data ]]>\n";
    xml += "      <Nested><Deep><Deeper attr=\"q\">zzz</Deeper></Deep>";
    xml += "</Nested>\n";
    xml += "    </Meta>\n";
    xml += "    <Email>u" + idx + "@e.com</Email>\n";
    xml += "  </User>\n";
  }
  xml += "</Users>";
  return xml;
}

// Pre-generate payloads
const std::string kFlatXml = GenerateFlatXml(2000);
const std::string kDeepXml = GenerateDeepXml(2000);
const std::string kAttrXml = GenerateAttrXml(2000);
const std::string kSmallXml = GenerateLargeXml(1);
const std::string kLargeXml = GenerateLargeXml(10000);
const std::string kOrgXml = GenerateOrgXml(20, 10);
const std::string kTreeXml = GenerateTreeXml(14, 2);
const std::string kCommentXml = GenerateCommentHeavyXml(1000);
const std::string kUnknownXml = GenerateUnknownHeavyXml(2000);
static const std::string kCatalogXml = R"(<?xml version="1.0"?>
<catalog>
   <book id="bk101">
      <author>Gambardella, Matthew</author>
      <title>XML Developer's Guide</title>
      <genre>Computer</genre>
      <price>44.95</price>
      <publish_date>2000-10-01</publish_date>
      <description>An in-depth look at creating applications
      with XML.</description>
   </book>
   <book id="bk102">
      <author>Ralls, Kim</author>
      <title>Midnight Rain</title>
      <genre>Fantasy</genre>
      <price>5.95</price>
      <publish_date>2000-12-16</publish_date>
      <description>A former architect battles corporate zombies,
      an evil sorceress, and her own childhood to become queen
      of the world.</description>
   </book>
   <book id="bk103">
      <author>Corets, Eva</author>
      <title>Maeve Ascendant</title>
      <genre>Fantasy</genre>
      <price>5.95</price>
      <publish_date>2000-11-17</publish_date>
      <description>After the collapse of a nanotechnology
      society in England, the young survivors lay the
      foundation for a new society.</description>
   </book>
   <book id="bk104">
      <author>Corets, Eva</author>
      <title>Oberon's Legacy</title>
      <genre>Fantasy</genre>
      <price>5.95</price>
      <publish_date>2001-03-10</publish_date>
      <description>In post-apocalypse England, the mysterious
      agent known only as Oberon helps to create a new life
      for the inhabitants of London. Sequel to Maeve
      Ascendant.</description>
   </book>
   <book id="bk105">
      <author>Corets, Eva</author>
      <title>The Sundered Grail</title>
      <genre>Fantasy</genre>
      <price>5.95</price>
      <publish_date>2001-09-10</publish_date>
      <description>The two daughters of Maeve, half-sisters,
      battle one another for control of England. Sequel to
      Oberon's Legacy.</description>
   </book>
   <book id="bk106">
      <author>Randall, Cynthia</author>
      <title>Lover Birds</title>
      <genre>Romance</genre>
      <price>4.95</price>
      <publish_date>2000-09-02</publish_date>
      <description>When Carla meets Paul at an ornithology
      conference, tempers fly as feathers get ruffled.</description>
   </book>
   <book id="bk107">
      <author>Thurman, Paula</author>
      <title>Splish Splash</title>
      <genre>Romance</genre>
      <price>4.95</price>
      <publish_date>2000-11-02</publish_date>
      <description>A deep sea diver finds true love twenty
      thousand leagues beneath the sea.</description>
   </book>
   <book id="bk108">
      <author>Knorr, Stefan</author>
      <title>Creepy Crawlies</title>
      <genre>Horror</genre>
      <price>4.95</price>
      <publish_date>2000-12-06</publish_date>
      <description>An anthology of horror stories about roaches,
      centipedes, scorpions  and other insects.</description>
   </book>
   <book id="bk109">
      <author>Kress, Peter</author>
      <title>Paradox Lost</title>
      <genre>Science Fiction</genre>
      <price>6.95</price>
      <publish_date>2000-11-02</publish_date>
      <description>After an inadvertant trip through a Heisenberg
      Uncertainty Device, James Salway discovers the problems
      of being quantum.</description>
   </book>
   <book id="bk110">
      <author>O'Brien, Tim</author>
      <title>Microsoft .NET: The Programming Bible</title>
      <genre>Computer</genre>
      <price>36.95</price>
      <publish_date>2000-12-09</publish_date>
      <description>Microsoft's .NET initiative is explored in
      detail in this deep programmer's reference.</description>
   </book>
   <book id="bk111">
      <author>O'Brien, Tim</author>
      <title>MSXML3: A Comprehensive Guide</title>
      <genre>Computer</genre>
      <price>36.95</price>
      <publish_date>2000-12-01</publish_date>
      <description>The Microsoft MSXML3 parser is covered in
      detail, with attention to XML DOM interfaces, XSLT processing,
      SAX and more.</description>
   </book>
   <book id="bk112">
      <author>Galos, Mike</author>
      <title>Visual Studio 7: A Comprehensive Guide</title>
      <genre>Computer</genre>
      <price>49.95</price>
      <publish_date>2001-04-16</publish_date>
      <description>Microsoft Visual Studio 7 is explored in depth,
      looking at how Visual Basic, Visual C++, C#, and ASP+ are
      integrated into a comprehensive development
      environment.</description>
   </book>
</catalog>)";

static void BM_ParseSmallXml(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kSmallXml};
    Users users;
    bool ok = xml::deserialize(parser, "Users", users);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(users);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kSmallXml.size()));
}

static void BM_ParseLargeXml(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kLargeXml};
    Users users;
    bool ok = xml::deserialize(parser, "Users", users);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(users);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kLargeXml.size()));
}

static void BM_ParseFlatXml(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kFlatXml};
    FlatList list;
    bool ok = xml::deserialize(parser, "FlatList", list);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(list);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kFlatXml.size()));
}

static void BM_ParseDeepXml(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kDeepXml};
    DeepList list;
    bool ok = xml::deserialize(parser, "DeepList", list);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(list);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kDeepXml.size()));
}

static void BM_ParseAttrXml(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kAttrXml};
    AttrList list;
    bool ok = xml::deserialize(parser, "AttrList", list);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(list);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kAttrXml.size()));
}

static void BM_ParseOrgXml(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kOrgXml};
    Organization org;
    bool ok = xml::deserialize(parser, "Organization", org);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(org);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kOrgXml.size()));
}

static void BM_ParseTreeXml(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kTreeXml};
    TreeNode root;
    bool ok = xml::deserialize(parser, "Node", root);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(root);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kTreeXml.size()));
}

static void BM_ParseCommentHeavyXml(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kCommentXml};
    Users users;
    bool ok = xml::deserialize(parser, "Users", users);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(users);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kCommentXml.size()));
}

static void BM_ParseUnknownHeavyXml(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kUnknownXml};
    Users users;
    bool ok = xml::deserialize(parser, "Users", users);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(users);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kUnknownXml.size()));
}

static void BM_ParseCatalog(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kCatalogXml};
    Catalog catalog;
    bool ok = xml::deserialize(parser, "catalog", catalog);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(catalog);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kCatalogXml.size()));
}

// ---- Required vs optional fields ----
//
// Two records with identical members and the identical payload (kFlatXml, a
// 2000-element <FlatList> of <Item id><title><desc><status>); the only
// difference is whether the fields are declared required. Marking a field
// required turns on per-element presence tracking (a bitmask OR at each matched
// field) plus a mask comparison at each closing tag in Parser::pull(); the
// optional variant compiles all of that away (kHasRequired == false). Parsing
// the same bytes both ways isolates that tracking overhead. Both records mark
// every field required (the worst case for the required path).

static void BM_ParseOptionalFields(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kFlatXml};
    OptItemList list;
    bool ok = xml::deserialize(parser, "FlatList", list);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(list);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kFlatXml.size()));
}

static void BM_ParseRequiredFields(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kFlatXml};
    ReqItemList list;
    bool ok = xml::deserialize(parser, "FlatList", list);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(list);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kFlatXml.size()));
}

// ---- Normalizing string content ----
//
// Same kFlatXml payload, but the record holds owning std::string fields and is
// parsed with xml::NormalizingParser. On that parser, std::string fields route
// every text run through the normalization scan (reference expansion, CR/CRLF
// folding, attribute whitespace) instead of a single zero-copy view assignment.
//
// Comparison axis vs the two benchmarks above: those use std::string_view (raw,
// zero-copy), so the delta here bundles two costs — the owning std::string copy
// AND the per-byte normalization scan. This payload is entity-free, so it
// measures the steady-state cost of the normalization machinery on plain
// content; documents carrying many &entities;/&#refs; would add expansion work
// on top. BM_ParseOwnedRawStrings isolates the owning-copy half so the
// normalization half can be read off as the difference.

// Owning std::string fields parsed with the default (non-normalizing) parser:
// the copy cost without the normalization scan, as a baseline for the delta.
static void BM_ParseOwnedRawStrings(benchmark::State& state) {
  for (auto _ : state) {
    xml::Parser parser{kFlatXml};
    NormItemList list;
    bool ok = xml::deserialize(parser, "FlatList", list);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(list);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kFlatXml.size()));
}

static void BM_ParseNormalizedFields(benchmark::State& state) {
  for (auto _ : state) {
    xml::NormalizingParser parser{kFlatXml};
    NormItemList list;
    bool ok = xml::deserialize(parser, "FlatList", list);
    benchmark::DoNotOptimize(ok);
    benchmark::DoNotOptimize(list);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kFlatXml.size()));
}

BENCHMARK(BM_ParseFlatXml);
BENCHMARK(BM_ParseDeepXml);
BENCHMARK(BM_ParseAttrXml);
BENCHMARK(BM_ParseSmallXml);
BENCHMARK(BM_ParseLargeXml);
BENCHMARK(BM_ParseOrgXml);
BENCHMARK(BM_ParseTreeXml);
BENCHMARK(BM_ParseCommentHeavyXml);
BENCHMARK(BM_ParseUnknownHeavyXml);
BENCHMARK(BM_ParseCatalog);
BENCHMARK(BM_ParseOptionalFields);
BENCHMARK(BM_ParseRequiredFields);
BENCHMARK(BM_ParseOwnedRawStrings);
BENCHMARK(BM_ParseNormalizedFields);

#ifdef TURBOXML_HAS_PUGIXML
#include <pugixml.hpp>

static auto child_as_int(const pugi::xml_node& node, const char* name) -> int {
  const char* text = node.child_value(name);
  int out{};
  std::from_chars(text, text + std::strlen(text), out);
  return out;
}

static void BM_Pugi_ParseSmallXml(benchmark::State& state) {
  for (auto _ : state) {
    pugi::xml_document doc;
    doc.load_buffer(kSmallXml.data(), kSmallXml.size());
    std::vector<int> ids;
    std::vector<std::string_view> names, emails;
    for (const auto& user : doc.child("Users").children("User")) {
      ids.push_back(user.attribute("id").as_int());
      names.push_back(user.child_value("Name"));
      emails.push_back(user.child_value("Email"));
    }
    benchmark::DoNotOptimize(ids);
    benchmark::DoNotOptimize(names);
    benchmark::DoNotOptimize(emails);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kSmallXml.size()));
}

static void BM_Pugi_ParseLargeXml(benchmark::State& state) {
  for (auto _ : state) {
    pugi::xml_document doc;
    doc.load_buffer(kLargeXml.data(), kLargeXml.size());
    std::vector<int> ids;
    std::vector<std::string_view> names, emails;
    for (const auto& user : doc.child("Users").children("User")) {
      ids.push_back(user.attribute("id").as_int());
      names.push_back(user.child_value("Name"));
      emails.push_back(user.child_value("Email"));
    }
    benchmark::DoNotOptimize(ids);
    benchmark::DoNotOptimize(names);
    benchmark::DoNotOptimize(emails);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kLargeXml.size()));
}

static void BM_Pugi_ParseFlatXml(benchmark::State& state) {
  for (auto _ : state) {
    pugi::xml_document doc;
    doc.load_buffer(kFlatXml.data(), kFlatXml.size());
    FlatList list;
    for (const auto& item : doc.child("FlatList").children("Item")) {
      FlatItem it;
      it.id = item.attribute("id").as_int();
      it.title = item.child_value("title");
      it.description = item.child_value("desc");
      it.status = child_as_int(item, "status");
      list.items.push_back(it);
    }
    benchmark::DoNotOptimize(list);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kFlatXml.size()));
}

static void BM_Pugi_ParseDeepXml(benchmark::State& state) {
  for (auto _ : state) {
    pugi::xml_document doc;
    doc.load_buffer(kDeepXml.data(), kDeepXml.size());
    std::vector<int> values;
    for (const auto& l1 : doc.child("DeepList").children("L1")) {
      values.push_back(child_as_int(
          l1.child("L2").child("L3").child("L4").child("L5"), "v"));
    }
    benchmark::DoNotOptimize(values);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kDeepXml.size()));
}

static void BM_Pugi_ParseAttrXml(benchmark::State& state) {
  for (auto _ : state) {
    pugi::xml_document doc;
    doc.load_buffer(kAttrXml.data(), kAttrXml.size());
    std::vector<int> a1s, a2s, a3s, a4s, a5s;
    std::vector<std::string_view> s1s, s2s, s3s, s4s, s5s;
    for (const auto& item : doc.child("AttrList").children("Item")) {
      a1s.push_back(item.attribute("a1").as_int());
      a2s.push_back(item.attribute("a2").as_int());
      a3s.push_back(item.attribute("a3").as_int());
      a4s.push_back(item.attribute("a4").as_int());
      a5s.push_back(item.attribute("a5").as_int());
      s1s.push_back(item.attribute("s1").value());
      s2s.push_back(item.attribute("s2").value());
      s3s.push_back(item.attribute("s3").value());
      s4s.push_back(item.attribute("s4").value());
      s5s.push_back(item.attribute("s5").value());
    }
    benchmark::DoNotOptimize(a1s);
    benchmark::DoNotOptimize(s1s);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kAttrXml.size()));
}

static void BM_Pugi_ParseOrgXml(benchmark::State& state) {
  for (auto _ : state) {
    pugi::xml_document doc;
    doc.load_buffer(kOrgXml.data(), kOrgXml.size());
    Organization org;
    auto org_node = doc.child("Organization");
    org.id = org_node.attribute("id").as_int();
    org.name = org_node.attribute("name").value();
    for (const auto& dn : org_node.children("Department")) {
      OrgDepartment dept;
      dept.id = dn.attribute("id").as_int();
      dept.name = dn.attribute("name").value();
      for (const auto& tn : dn.children("Team")) {
        OrgTeam team;
        team.id = tn.attribute("id").as_int();
        team.name = tn.attribute("name").value();
        for (const auto& mn : tn.children("Member")) {
          OrgMember member;
          member.id = mn.attribute("id").as_int();
          member.role = mn.attribute("role").value();
          member.full_name = mn.child_value("FullName");
          member.email = mn.child_value("Email");
          for (const auto& sn : mn.child("Skills").children("Skill")) {
            member.skills.items.push_back(sn.child_value());
          }
          team.members.push_back(member);
        }
        dept.teams.push_back(team);
      }
      org.departments.push_back(dept);
    }
    benchmark::DoNotOptimize(org);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kOrgXml.size()));
}

static void pugi_build_tree(TreeNode& node, const pugi::xml_node& xn) {
  for (const auto& child : xn.children("Node")) {
    node.children.emplace_back();
    pugi_build_tree(node.children.back(), child);
  }
}

static void BM_Pugi_ParseTreeXml(benchmark::State& state) {
  for (auto _ : state) {
    pugi::xml_document doc;
    doc.load_buffer(kTreeXml.data(), kTreeXml.size());
    TreeNode root;
    pugi_build_tree(root, doc.child("Node"));
    benchmark::DoNotOptimize(root);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kTreeXml.size()));
}

static void BM_Pugi_ParseCommentHeavyXml(benchmark::State& state) {
  for (auto _ : state) {
    pugi::xml_document doc;
    doc.load_buffer(kCommentXml.data(), kCommentXml.size());
    std::vector<int> ids;
    std::vector<std::string_view> names, emails;
    for (const auto& user : doc.child("Users").children("User")) {
      ids.push_back(user.attribute("id").as_int());
      names.push_back(user.child_value("Name"));
      emails.push_back(user.child_value("Email"));
    }
    benchmark::DoNotOptimize(ids);
    benchmark::DoNotOptimize(names);
    benchmark::DoNotOptimize(emails);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kCommentXml.size()));
}

static void BM_Pugi_ParseCatalog(benchmark::State& state) {
  for (auto _ : state) {
    pugi::xml_document doc;
    doc.load_buffer(kCatalogXml.data(), kCatalogXml.size());
    Catalog catalog;
    for (const auto& node : doc.child("catalog").children("book")) {
      Book& b = catalog.books.emplace_back();
      b.id = node.attribute("id").value();
      b.author = node.child_value("author");
      b.title = node.child_value("title");
      b.genre = node.child_value("genre");
      b.price = node.child_value("price");
      b.publish_date = node.child_value("publish_date");
      b.description = node.child_value("description");
    }
    benchmark::DoNotOptimize(catalog);
    benchmark::ClobberMemory();
  }
  state.SetBytesProcessed(state.iterations() *
                          static_cast<int64_t>(kCatalogXml.size()));
}

BENCHMARK(BM_Pugi_ParseFlatXml);
BENCHMARK(BM_Pugi_ParseDeepXml);
BENCHMARK(BM_Pugi_ParseAttrXml);
BENCHMARK(BM_Pugi_ParseSmallXml);
BENCHMARK(BM_Pugi_ParseLargeXml);
BENCHMARK(BM_Pugi_ParseOrgXml);
BENCHMARK(BM_Pugi_ParseTreeXml);
BENCHMARK(BM_Pugi_ParseCommentHeavyXml);
BENCHMARK(BM_Pugi_ParseCatalog);
#endif  // TURBOXML_HAS_PUGIXML

BENCHMARK_MAIN();
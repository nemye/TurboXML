#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "LightningXML.hh"
#include "test_Helpers.hh"

class LightningBasicTests : public ::testing::Test {};

TEST_F(LightningBasicTests, ParsingNested) {
  const std::string_view xml_src = R"(
<person>
  <name>Alice</name>
  <age>30</age>
  <address>
    <street>Main St</street>
    <zip>12345</zip>
  </address>
</person>
)";
  xmlight::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xmlight::deserialize(parser, "employee", person));
  EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::RootElementNotFound);

  xmlight::Parser correct_parser{xml_src};
  ASSERT_TRUE(xmlight::deserialize(correct_parser, "person", person));
  EXPECT_EQ(person.name, "Alice");
  EXPECT_EQ(person.age, 30);
  EXPECT_EQ(person.address.street, "Main St");
  EXPECT_EQ(person.address.zip, 12345);
}

TEST_F(LightningBasicTests, SkipsUnknownFields) {
  const std::string_view xml_src = R"(
<person>
  <name>Bob</name>
  <age>25</age>
  <nickname>Bobby</nickname>
  <address>
    <street>Elm Ave</street>
    <zip>99999</zip>
  </address>
</person>
)";
  xmlight::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Bob");
  EXPECT_EQ(person.age, 25);
  EXPECT_EQ(person.address.street, "Elm Ave");
  EXPECT_EQ(person.address.zip, 99999);
}

TEST_F(LightningBasicTests, MissingFieldsRetainDefaults) {
  const std::string_view xml_src = R"(
<person>
  <name>Carol</name>
</person>
)";
  xmlight::Parser parser{xml_src};
  Person person;
  std::ignore = xmlight::deserialize(parser, "person", person);
  EXPECT_EQ(person.name, "Carol");
  EXPECT_EQ(person.age, 0);
}

/// @brief Malformed, truncated, or mismatched documents fail cleanly with the
/// error code specific to the defect.
TEST_F(LightningBasicTests, MalformedDocumentsFail) {
  constexpr auto CASES = std::to_array<std::pair<std::string_view, xmlight::ErrorCode>>({
      {R"(<person><name>Dave</name>)", xmlight::ErrorCode::UnexpectedEof},
      {"", xmlight::ErrorCode::RootElementNotFound},
      {R"(<alien><name>Zorg</name></alien>)", xmlight::ErrorCode::RootElementNotFound},
      // A stray close tag before the root open tag.
      {R"(</person><person></person>)", xmlight::ErrorCode::RootElementNotFound},
      {"<person>\n  <name>Alice</age> </person>\n", xmlight::ErrorCode::ElementMismatch},
      // A mismatch deep in the hierarchy propagates failure all the way out.
      {R"(<person><address><street>123 Ave</zip></address></person>)",
       xmlight::ErrorCode::ElementMismatch},
      // A close tag with no name (</>).
      {R"(<person></>)", xmlight::ErrorCode::ExpectedNameInCloseTag},
      // Tag names cannot start with a digit.
      {R"(<person><123name>Bob</123name></person>)", xmlight::ErrorCode::UnexpectedCharAfterLt},
      // Garbage characters where attributes are expected.
      {R"(<person !@#$gar> <name>Alice</name> </person>)",
       xmlight::ErrorCode::ExpectedAttributeName},
      // Truncated close tag on the primitive fast path: `</name` with no `>`.
      {"<person><name>Alice</name", xmlight::ErrorCode::ExpectedCloseTagEnd},
  });
  for (const auto& [src, code] : CASES) {
    xmlight::Parser parser{src};
    Person person;
    EXPECT_FALSE(xmlight::deserialize(parser, "person", person)) << src;
    EXPECT_EQ(parser.errorCode(), code) << src;
  }
}

TEST_F(LightningBasicTests, ParsesFieldsOutOfOrder) {
  const std::string_view xml_src = R"(
<person>
  <age>42</age>
  <address>
    <zip>11111</zip>
    <street>Oak Rd</street>
  </address>
  <name>Eve</name>
</person>
)";
  xmlight::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Eve");
  EXPECT_EQ(person.age, 42);
  EXPECT_EQ(person.address.street, "Oak Rd");
  EXPECT_EQ(person.address.zip, 11111);
}

TEST_F(LightningBasicTests, PullsUserDataAndIgnoresUnknownTags) {
  const std::string_view xml_src = R"(
<Users>
  <User id="42">
    <Name>Ada Lovelace</Name>
    <UnknownTag><Nested>Ignore me</Nested></UnknownTag>
    <Email>ada@example.com</Email>
  </User>
  <User id="99">
    <Name>Grace Hopper</Name>
    <Email>grace@example.com</Email>
  </User>
</Users>
)";
  xmlight::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xmlight::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 2U);
  EXPECT_EQ(users.items[0].id, 42);
  EXPECT_EQ(users.items[0].name, "Ada Lovelace");
  EXPECT_EQ(users.items[0].email, "ada@example.com");
  EXPECT_EQ(users.items[1].id, 99);
  EXPECT_EQ(users.items[1].name, "Grace Hopper");
  EXPECT_EQ(users.items[1].email, "grace@example.com");
}

TEST_F(LightningBasicTests, DeserializesFullOrganizationHierarchy) {
  const std::string_view xml_src = R"(<?xml version="1.0"?>
<Organization id="1" name="Acme Corp">
  <Department id="10" name="Engineering">
    <Team id="100" name="Platform">
      <Member id="1001" role="Engineer">
        <FullName>Ada Lovelace</FullName>
        <Email>ada@acme.com</Email>
        <Skills>
          <Skill>C++</Skill>
          <Skill>Algorithms</Skill>
        </Skills>
      </Member>
      <Member id="1002" role="Lead">
        <FullName>Grace Hopper</FullName>
        <Email>grace@acme.com</Email>
        <Skills>
          <Skill>Compilers</Skill>
          <Skill>Leadership</Skill>
          <Skill>COBOL</Skill>
        </Skills>
      </Member>
    </Team>
  </Department>
</Organization>)";

  xmlight::Parser parser{xml_src};
  Organization org;
  ASSERT_TRUE(xmlight::deserialize(parser, "Organization", org));
  EXPECT_EQ(org.id, 1);
  EXPECT_EQ(org.name, "Acme Corp");
  ASSERT_EQ(org.departments.size(), 1U);

  const auto& eng = org.departments[0];
  EXPECT_EQ(eng.id, 10);
  EXPECT_EQ(eng.name, "Engineering");
  ASSERT_EQ(eng.teams.size(), 1U);

  const auto& platform = eng.teams[0];
  EXPECT_EQ(platform.id, 100);
  EXPECT_EQ(platform.name, "Platform");
  ASSERT_EQ(platform.members.size(), 2U);

  const auto& ada = platform.members[0];
  EXPECT_EQ(ada.id, 1001);
  EXPECT_EQ(ada.role, "Engineer");
  EXPECT_EQ(ada.full_name, "Ada Lovelace");
  EXPECT_EQ(ada.email, "ada@acme.com");
  ASSERT_EQ(ada.skills.items.size(), 2U);
  EXPECT_EQ(ada.skills.items[0], "C++");
  EXPECT_EQ(ada.skills.items[1], "Algorithms");
}

/// @brief Tests that empty string attributes are parsed as empty string_views.
TEST_F(LightningBasicTests, EmptyStringAttribute) {
  const std::string_view xml_src = R"(<Organization id="1" name=""></Organization>)";
  xmlight::Parser parser{xml_src};
  Organization org;

  ASSERT_TRUE(xmlight::deserialize(parser, "Organization", org));
  EXPECT_EQ(org.id, 1);
  EXPECT_TRUE(org.name.empty());
}

/// @brief Tests that empty numeric attributes and empty child elements
/// fall back to default values safely without failing the parse.
TEST_F(LightningBasicTests, EmptyNumericAttributeErrors) {
  // An empty value on a non-optional numeric attribute ("") is not valid
  // lexical space for the type, so it is a hard error rather than a silently
  // dropped default. (Use std::optional<int> to allow an empty/absent value.)
  // This mirrors how an empty numeric *element* is already rejected.
  const std::string_view xml_src = R"(
<Users>
  <User id="">
    <Name>Ghost</Name>
    <Email></Email>
  </User>
</Users>
)";

  xmlight::Parser parser{xml_src};
  Users users;

  EXPECT_FALSE(xmlight::deserialize(parser, "Users", users));
  EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::InvalidNumericValue);
}

/// @brief Tests that malformed numeric data fails gracefully rather than
/// throwing or crashing.
TEST_F(LightningBasicTests, MalformedNumericDataFailsGracefully) {
  const std::string_view xml_src = R"(
<person>
  <name>John</name>
  <age>not-a-number</age>
  <address><street>123 Ave</street><zip>NaN</zip></address>
</person>
)";
  xmlight::Parser parser{xml_src};
  Person person;
  // Deserialization of primitives returns false if parse_numeric fails.
  // Because pull() relies on read_element returning true for handled fields,
  // failing to parse 'age' causes it to return false, propagating up.
  EXPECT_FALSE(xmlight::deserialize(parser, "person", person));
  EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::InvalidNumericValue);
}

/// @brief Tests that self-closing tags with attributes process the attributes
/// and terminate correctly.
TEST_F(LightningBasicTests, SelfClosingTagWithAttributes) {
  const std::string_view xml_src = R"(<Users><User id="77" Name="Self" Email="none"/></Users>)";
  xmlight::Parser parser{xml_src};
  Users users;

  // For this to work, User must be able to pull() attributes from a
  // self-closing tag. The current logic in pull() checks for ElementClose
  // immediately.
  ASSERT_TRUE(xmlight::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 1U);
  EXPECT_EQ(users.items[0].id, 77);
  // Name and Email won't be populated because there are no child elements,
  // but the element itself should be consumed without error.
  EXPECT_TRUE(users.items[0].name.empty());
}

/// @brief Unbound content between mapped fields - stray text, comments, and
/// CDATA - is ignored without disrupting deserialization.
TEST_F(LightningBasicTests, IgnoresUnmappedInterleavedContent) {
  struct Case {
    std::string_view src;
    std::string_view name;
    int age;
  };
  constexpr auto CASES = std::to_array<Case>({
      {"<person>\n  Some raw text that shouldn't break the parser.\n  <name>Mixer</name>\n"
       "  More random text.\n  <age>45</age>\n</person>",
       "Mixer", 45},
      {"<person>\n  <name>Frank</name>\n  <![CDATA[ Some raw data that the parser should ignore"
       " because it's not in a mapped field ]]>\n  <age>50</age>\n  </person>",
       "Frank", 50},
      {"<person><name>n</name> stray <age>3</age> tail </person>", "n", 3},
  });
  for (const auto& [src, name, age] : CASES) {
    xmlight::Parser parser{src};
    Person person;
    ASSERT_TRUE(xmlight::deserialize(parser, "person", person)) << src;
    EXPECT_EQ(person.name, name);
    EXPECT_EQ(person.age, age);
  }
}

/// @brief Zero-copy semantics: string_view fields point into the source
/// buffer, entities remain unexpanded, and whitespace (including newlines)
/// is preserved byte-for-byte.
TEST_F(LightningBasicTests, ZeroCopyStringSemantics) {
  {
    const std::string_view xml_src = R"(
<person>
  <name>AT&amp;T</name>
  <address><street>Me &lt; You</street></address>
</person>
)";
    xmlight::Parser parser{xml_src};
    Person person;
    ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
    EXPECT_EQ(person.name, "AT&amp;T");
    EXPECT_EQ(person.address.street, "Me &lt; You");
  }
  {
    const std::string xml = "<person><name>Alice</name></person>";
    xmlight::Parser parser{xml};
    Person person;
    ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
    const char* begin = xml.data();
    EXPECT_GE(person.name.data(), begin);
    EXPECT_LT(person.name.data(), begin + xml.size());
  }
  {
    xmlight::Parser parser{"<person><name>\n    Spaced Out\n  </name></person>"};
    Person person;
    ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
    EXPECT_EQ(person.name, "\n    Spaced Out\n  ");
  }
}

/// @brief Tests that both empty tags and self-closing tags yield empty string
/// views.
TEST_F(LightningBasicTests, EmptyAndSelfClosingStringPrimitives) {
  const std::string_view xml_src = R"(
<Users>
  <User id="1"><Name></Name><Email/></User>
</Users>
)";
  xmlight::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xmlight::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 1U);
  EXPECT_TRUE(users.items[0].name.empty());
  EXPECT_TRUE(users.items[0].email.empty());
}

/// @brief Non-xml processing instructions (single or repeated) before the root
/// element are skipped and the document parses correctly.
TEST_F(LightningBasicTests, ProcessingInstructionsBeforeRootSkipped) {
  for (
      const std::string_view xml_src :
      {std::string_view(
           R"(<?xml-stylesheet type="text/xsl" href="style.xsl"?><person><name>Pat</name></person>)"),
       std::string_view(
           R"(<?xml version="1.0"?><?xml-stylesheet type="text/xsl"?><person><name>Pat</name></person>)")}) {
    xmlight::Parser parser{xml_src};
    Person person;
    ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
    EXPECT_EQ(person.name, "Pat");
  }
}

/// @brief Known limitation of the zero-copy design: when a comment or an
/// unexpected child element splits a string primitive's text, value()
/// overwrites 'out' on each Text token, so only the last segment survives.
TEST_F(LightningBasicTests, InterruptedTextLastSegmentWins) {
  for (const auto& [src, want] :
       {std::pair<std::string_view, std::string_view>{
            "<person>\n  <name>Al<!--comment-->ice</name>\n</person>", "ice"},
        {"<person>\n  <name><unexpected/>hello</name>\n</person>", "hello"}}) {
    xmlight::Parser parser{src};
    Person person;
    ASSERT_TRUE(xmlight::deserialize(parser, "person", person)) << src;
    EXPECT_EQ(person.name, want);
  }
}

/// @brief XSD whitespace `collapse` applies to numeric leaves: padding around
/// a valid number is trimmed, while whitespace-only content is still not valid
/// lexical space for the type.
TEST_F(LightningBasicTests, NumericFieldWhitespaceCollapses) {
  {
    xmlight::Parser parser{"<person>\n  <name>Test</name>\n  <age> 30 </age>\n</person>"};
    Person person;
    ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
    EXPECT_EQ(person.age, 30);
  }
  {
    xmlight::Parser parser{"<person>\n  <name>Test</name>\n  <age>   </age>\n</person>"};
    Person person;
    EXPECT_FALSE(xmlight::deserialize(parser, "person", person));
    EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::InvalidNumericValue);
  }
}

/// @brief reset() allows reuse after both a successful and a failed parse,
/// clearing the error state.
TEST_F(LightningBasicTests, ParserCanBeResetAndReused) {
  const std::string_view xml_src = R"(<person><name>Alice</name><age>30</age></person>)";
  xmlight::Parser parser{xml_src};

  Person first;
  EXPECT_FALSE(xmlight::deserialize(parser, "employee", first));
  EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::RootElementNotFound);

  parser.reset();
  EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::None);  // reset clears it

  Person second;
  ASSERT_TRUE(xmlight::deserialize(parser, "person", second));
  EXPECT_EQ(second.name, "Alice");
  EXPECT_EQ(second.age, 30);

  parser.reset();

  Person third;
  ASSERT_TRUE(xmlight::deserialize(parser, "person", third));
  EXPECT_EQ(third.name, "Alice");
}

TEST_F(LightningBasicTests, IgnoresTrailingDocumentContent) {
  const std::string_view xml_src = R"(
<person>
  <name>Alice</name>
</person>
<junk>should not matter</junk>
)";

  xmlight::Parser parser{xml_src};

  Person person;
  ASSERT_TRUE(xmlight::deserialize(parser, "person", person));

  EXPECT_EQ(person.name, "Alice");
}

/// @brief Unknown subtrees - deep nesting, and content with quoted '>' and
/// "/>" in attribute values, comments and CDATA with markup-like content,
/// PIs, and self-closing tags - must be skipped without desyncing the parse.
TEST_F(LightningBasicTests, SkipsUnknownSubtrees) {
  for (const std::string_view xml_src :
       {std::string_view("<person>\n  <name>Alice</name>\n  <unknown>\n    <a>\n      <b>\n"
                         "        <c>\n          <d>ignored</d>\n        </c>\n      </b>\n"
                         "    </a>\n  </unknown>\n  <age>30</age>\n</person>"),
        std::string_view(R"(
<person>
  <name>Alice</name>
  <unknown a="x > y" b='gt > inside'>
    <!-- comment with > and "quotes" and <tags> -->
    <![CDATA[ raw <blob/> with ]] almost-terminators ]]>
    <?pi target with > inside ?>
    <child attr="/>">text</child>
    <selfclosed x="y"/>
  </unknown>
  <age>30</age>
</person>)"),
        std::string_view(R"(<person><unknown><!ENTITY e "v"><a/></unknown>)"
                         R"(<name>Alice</name><age>30</age></person>)")}) {
    xmlight::Parser parser{xml_src};
    Person person;
    ASSERT_TRUE(xmlight::deserialize(parser, "person", person)) << xml_src;
    EXPECT_EQ(person.name, "Alice");
    EXPECT_EQ(person.age, 30);
  }
}

/// @brief Attribute value forms: single quotes, namespace prefixes, and
/// duplicates (XML 1.0 forbids duplicate attributes, but detecting them is a
/// documented limitation -- the document-order first match wins).
TEST_F(LightningBasicTests, AttributeFormVariants) {
  constexpr auto CASES = std::to_array<std::pair<std::string_view, int>>({
      {R"(<Users><User id='123'></User></Users>)", 123},
      {R"(<Users><User ns:id="55"></User></Users>)", 55},
      {R"(<Users><User id="1" id="2"><Name>Bob</Name></User></Users>)", 1},
  });
  for (const auto& [src, id] : CASES) {
    xmlight::Parser parser{src};
    Users users;
    ASSERT_TRUE(xmlight::deserialize(parser, "Users", users)) << src;
    ASSERT_EQ(users.items.size(), 1U);
    EXPECT_EQ(users.items[0].id, id);
  }
}

/// @brief Unterminated markup constructs fail with their specific error codes.
TEST_F(LightningBasicTests, UnterminatedMarkupFails) {
  constexpr auto ATTR_CASES = std::to_array<std::pair<std::string_view, xmlight::ErrorCode>>({
      {R"(<Users><User id=123></User></Users>)", xmlight::ErrorCode::ExpectedQuotedValue},
      {R"(<Users><User id="123></User></Users>)", xmlight::ErrorCode::UnterminatedAttributeValue},
  });
  for (const auto& [src, code] : ATTR_CASES) {
    xmlight::Parser parser{src};
    Users users;
    EXPECT_FALSE(xmlight::deserialize(parser, "Users", users)) << src;
    EXPECT_EQ(parser.errorCode(), code) << src;
  }
  constexpr auto DECL_CASES = std::to_array<std::pair<std::string_view, xmlight::ErrorCode>>({
      {"<person>\n  <!-- broken\n  <name>Alice</name>\n</person>",
       xmlight::ErrorCode::UnterminatedComment},
      {"<person>\n  <![CDATA[broken\n</person>", xmlight::ErrorCode::UnterminatedCData},
      {R"(<?xml-stylesheet type="text/xsl"<person></person>)", xmlight::ErrorCode::UnterminatedPi},
  });
  for (const auto& [src, code] : DECL_CASES) {
    xmlight::Parser parser{src};
    Person person;
    EXPECT_FALSE(xmlight::deserialize(parser, "person", person)) << src;
    EXPECT_EQ(parser.errorCode(), code) << src;
  }
}

TEST_F(LightningBasicTests, NamespacePrefixesAreIgnoredForMatching) {
  const std::string_view xml_src = R"(
<ns:person>
  <ns:name>Alice</ns:name>
  <ns:age>30</ns:age>
</ns:person>
)";

  xmlight::Parser parser{xml_src};

  Person person;
  ASSERT_TRUE(xmlight::deserialize(parser, "person", person));

  EXPECT_EQ(person.name, "Alice");
  EXPECT_EQ(person.age, 30);
}

TEST_F(LightningBasicTests, LargeVectorOfUsers) {
  std::string xml = "<Users>";

  for (int i = 0; i < 1000; ++i) {
    xml += "<User id=\"" + std::to_string(i) + "\">";
    xml += "<Name>User</Name>";
    xml += "</User>";
  }

  xml += "</Users>";

  xmlight::Parser parser{xml};

  Users users;
  ASSERT_TRUE(xmlight::deserialize(parser, "Users", users));

  ASSERT_EQ(users.items.size(), 1000U);
  EXPECT_EQ(users.items.front().id, 0);
  EXPECT_EQ(users.items.back().id, 999);
}

TEST_F(LightningBasicTests, DuplicateFieldLastValueWins) {
  const std::string_view xml_src = R"(
<person>
  <name>First</name>
  <name>Second</name>
</person>
)";

  xmlight::Parser parser{xml_src};

  Person person;
  ASSERT_TRUE(xmlight::deserialize(parser, "person", person));

  EXPECT_EQ(person.name, "Second");
}

/// @brief XML tags are strictly case-sensitive. Verify that mismatched casing
/// is ignored.
TEST_F(LightningBasicTests, CaseSensitivity) {
  const std::string_view xml_src = R"(
<person>
  <NAME>Alice</NAME>
  <Age>30</Age>
</person>
)";
  xmlight::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xmlight::deserialize(parser, "person", person));

  // Should remain default/empty because the struct maps to "name" and "age",
  // not "NAME" and "Age"
  EXPECT_TRUE(person.name.empty());
  EXPECT_EQ(person.age, 0);
}

/// @brief Verifies that attributes located directly on the root node being
/// deserialized are parsed and populated correctly.
TEST_F(LightningBasicTests, ParsesAttributesOnRootElement) {
  const std::string_view xml_src = R"(<Organization id="999" name="Global Corp"></Organization>)";
  xmlight::Parser parser{xml_src};
  Organization org;
  ASSERT_TRUE(xmlight::deserialize(parser, "Organization", org));
  EXPECT_EQ(org.id, 999);
  EXPECT_EQ(org.name, "Global Corp");
}

/// @brief Deeply nested hierarchies parse in all tokenisation modes: with
/// whitespace between tags, compact (every open tag hits try_begin_element
/// directly), repeated back-to-back, namespace-prefixed (fast path fails and
/// falls through to normal tokenisation), and with unknown siblings
/// interleaved at various levels.
TEST_F(LightningBasicTests, DeepNestingVariants) {
  {
    const std::string_view xml_src = R"(
<DeepList>
  <L1>
    <L2>
      <L3>
        <L4>
          <L5><v>42</v></L5>
        </L4>
      </L3>
    </L2>
  </L1>
</DeepList>
)";
    xmlight::Parser parser{xml_src};
    DeepList list;
    ASSERT_TRUE(xmlight::deserialize(parser, "DeepList", list));
    ASSERT_EQ(list.items.size(), 1U);
    EXPECT_EQ(list.items[0].next.next.next.next.value, 42);
  }
  {
    std::string xml = "<DeepList>";
    for (int i = 0; i < 5; ++i) {
      xml += "<L1><L2><L3><L4><L5><v>" + std::to_string(i) + "</v></L5></L4></L3></L2></L1>";
    }
    xml += "</DeepList>";
    xmlight::Parser parser{xml};
    DeepList list;
    ASSERT_TRUE(xmlight::deserialize(parser, "DeepList", list));
    ASSERT_EQ(list.items.size(), 5U);
    for (size_t i = 0; i < 5; ++i) {
      EXPECT_EQ(list.items[i].next.next.next.next.value, i);
    }
  }
  {
    const std::string_view xml_src = R"(
<DeepList>
  <ns:L1><ns:L2><ns:L3><ns:L4><ns:L5>
    <ns:v>42</ns:v>
  </ns:L5></ns:L4></ns:L3></ns:L2></ns:L1>
</DeepList>)";
    xmlight::Parser parser{xml_src};
    DeepList list;
    ASSERT_TRUE(xmlight::deserialize(parser, "DeepList", list));
    ASSERT_EQ(list.items.size(), 1U);
    EXPECT_EQ(list.items[0].next.next.next.next.value, 42);
  }
  {
    const std::string_view xml_src = R"(
<DeepList>
  <unknown>stuff</unknown>
  <L1>
    <unknown2/>
    <L2><L3><L4><L5><v>1</v></L5></L4></L3></L2>
  </L1>
  <L1><L2><L3><L4><L5><v>2</v></L5></L4></L3></L2></L1>
</DeepList>)";
    xmlight::Parser parser{xml_src};
    DeepList list;
    ASSERT_TRUE(xmlight::deserialize(parser, "DeepList", list));
    ASSERT_EQ(list.items.size(), 2U);
    EXPECT_EQ(list.items[0].next.next.next.next.value, 1);
    EXPECT_EQ(list.items[1].next.next.next.next.value, 2);
  }
}

/// @brief Vector containers across element forms: mixed standard and
/// self-closing items (the token state machine must reset cleanly between
/// iterations), consecutive self-closing items, an empty container, and a
/// self-closing root.
TEST_F(LightningBasicTests, VectorContainerForms) {
  {
    const std::string_view xml_src = R"(
<Users>
  <User id="10"><Name>Standard</Name></User>
  <User id="20"/>
  <User id="30"><Name>Standard Again</Name></User>
</Users>
)";
    xmlight::Parser parser{xml_src};
    Users users;
    ASSERT_TRUE(xmlight::deserialize(parser, "Users", users));
    ASSERT_EQ(users.items.size(), 3U);
    EXPECT_EQ(users.items[0].id, 10);
    EXPECT_EQ(users.items[1].id, 20);
    EXPECT_TRUE(users.items[1].name.empty());
    EXPECT_EQ(users.items[2].id, 30);
  }
  {
    const std::string_view xml_src = R"(
<Users>
  <User id="1"/><User id="2"/><User id="3"/><User id="4"/><User id="5"/>
</Users>)";
    xmlight::Parser parser{xml_src};
    Users users;
    ASSERT_TRUE(xmlight::deserialize(parser, "Users", users));
    ASSERT_EQ(users.items.size(), 5U);
    for (size_t i = 0; i < 5; ++i) {
      EXPECT_EQ(users.items[i].id, i + 1);
      EXPECT_TRUE(users.items[i].name.empty());
    }
  }
  for (const std::string_view xml_src :
       {std::string_view(R"(<Users></Users>)"), std::string_view(R"(<Users/>)")}) {
    xmlight::Parser parser{xml_src};
    Users users;
    ASSERT_TRUE(xmlight::deserialize(parser, "Users", users)) << xml_src;
    EXPECT_TRUE(users.items.empty());
  }
}

/// @brief Tag names cannot start with a number according to XML specs.
TEST_F(LightningBasicTests, InvalidTagNamesFailCleanly) {
  const std::string_view xml_src = R"(<person><123name>Bob</123name></person>)";
  xmlight::Parser parser{xml_src};
  Person person;
  // Should fail cleanly inside `parse_element_open` since '1' is not
  // `is_name_start`
  EXPECT_FALSE(xmlight::deserialize(parser, "person", person));
  EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::UnexpectedCharAfterLt);
}

/// @brief Tests malformed tags holding garbage characters where attributes are
/// expected.
TEST_F(LightningBasicTests, MalformedTagGarbageFails) {
  const std::string_view xml_src = R"(<person !@#$gar> <name>Alice</name> </person>)";
  xmlight::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xmlight::deserialize(parser, "person", person));
  EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::ExpectedAttributeName);
}

auto generateNestedXml(const size_t depth) -> std::string {
  std::string xml;
  xml.reserve(depth * 13);  // "<Node></Node>" is 13 chars
  for (size_t i = 0; i < depth; ++i) {
    xml += "<Node>";
  }
  for (size_t i = 0; i < depth; ++i) {
    xml += "</Node>";
  }
  return xml;
}

/// @brief Exactly kMaxDepth parses; kMaxDepth + 1 aborts cleanly.
TEST_F(LightningBasicTests, MaxDepthBoundary) {
  {
    const std::string xml_src = generateNestedXml(xmlight::Parser::MAX_DEPTH);
    xmlight::Parser parser{xml_src};
    TreeNode root;
    ASSERT_TRUE(xmlight::deserialize(parser, "Node", root));
    ASSERT_EQ(root.children.size(), 1U);
    EXPECT_EQ(root.children[0].children.size(), 1U);
  }
  {
    const std::string xml_src = generateNestedXml(xmlight::Parser::MAX_DEPTH + 1);
    xmlight::Parser parser{xml_src};
    TreeNode root;
    EXPECT_FALSE(xmlight::deserialize(parser, "Node", root));
    EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::DepthExceeded);
  }
}

/// @brief try_begin_element edge shapes on an N==1 type: attributes after the
/// name force fallthrough to normal tokenisation (TreeNode has no AttrField so
/// they are ignored), while <Node/> hits the fast path's self-closing match.
TEST_F(LightningBasicTests, TreeNodeFastPathVariants) {
  {
    const std::string_view xml_src = R"(<Node><Node id="1"><Node/></Node><Node id="2"/></Node>)";
    xmlight::Parser parser{xml_src};
    TreeNode root;
    ASSERT_TRUE(xmlight::deserialize(parser, "Node", root));
    ASSERT_EQ(root.children.size(), 2U);
    ASSERT_EQ(root.children[0].children.size(), 1U);
    EXPECT_TRUE(root.children[1].children.empty());
  }
  {
    const std::string_view xml_src = R"(<Node><Node/><Node><Node/></Node></Node>)";
    xmlight::Parser parser{xml_src};
    TreeNode root;
    ASSERT_TRUE(xmlight::deserialize(parser, "Node", root));
    ASSERT_EQ(root.children.size(), 2U);
    EXPECT_TRUE(root.children[0].children.empty());
    ASSERT_EQ(root.children[1].children.size(), 1U);
    EXPECT_TRUE(root.children[1].children[0].children.empty());
  }
}

/// @brief A child element whose name matches an AttrField hash must be
/// skipped without disrupting the parse (exercises the FieldKind::Attr dispatch
/// arm).
TEST_F(LightningBasicTests, ElementNameCollidesWithAttrField) {
  const std::string_view xml_src = R"(
<Users>
  <User id="42">
    <id>999</id>
    <Name>Collider</Name>
    <Email>c@c.com</Email>
  </User>
</Users>)";
  xmlight::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xmlight::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 1U);
  EXPECT_EQ(users.items[0].id, 42);
  EXPECT_EQ(users.items[0].name, "Collider");
  EXPECT_EQ(users.items[0].email, "c@c.com");
}

/// @brief Vectors of string primitives: empty container, multiple values, and
/// a self-closing element yielding an empty string_view.
TEST_F(LightningBasicTests, VecOfPrimitivesForms) {
  {
    xmlight::Parser parser{std::string_view(R"(<Skills></Skills>)")};
    Skills skills;
    ASSERT_TRUE(xmlight::deserialize(parser, "Skills", skills));
    EXPECT_TRUE(skills.items.empty());
  }
  {
    const std::string_view xml_src = R"(
<Skills>
  <Skill>C++</Skill>
  <Skill>Rust</Skill>
  <Skill>Go</Skill>
  <Skill>Python</Skill>
</Skills>)";
    xmlight::Parser parser{xml_src};
    Skills skills;
    ASSERT_TRUE(xmlight::deserialize(parser, "Skills", skills));
    ASSERT_EQ(skills.items.size(), 4U);
    EXPECT_EQ(skills.items[0], "C++");
    EXPECT_EQ(skills.items[3], "Python");
  }
  {
    xmlight::Parser parser{
        std::string_view(R"(<Skills><Skill>A</Skill><Skill/><Skill>B</Skill></Skills>)")};
    Skills skills;
    ASSERT_TRUE(xmlight::deserialize(parser, "Skills", skills));
    ASSERT_EQ(skills.items.size(), 3U);
    EXPECT_EQ(skills.items[0], "A");
    EXPECT_TRUE(skills.items[1].empty());
    EXPECT_EQ(skills.items[2], "B");
  }
}

/// @brief All 10 attributes bind in metadata order and out of it (the fallback
/// scan / document-order cursor miss path).
TEST_F(LightningBasicTests, AttrItemAllAttributesAnyOrder) {
  for (const std::string_view xml_src : {std::string_view(R"(
<AttrList>
  <Item a1="10" a2="20" a3="30" a4="40" a5="50"
        s1="one" s2="two" s3="three" s4="four" s5="five"/>
</AttrList>)"),
                                         std::string_view(R"(
<AttrList>
  <Item s5="five" a1="10" s1="one" a5="50" a2="20"
        s2="two" a3="30" s4="four" a4="40" s3="three"/>
</AttrList>)")}) {
    xmlight::Parser parser{xml_src};
    AttrList list;
    ASSERT_TRUE(xmlight::deserialize(parser, "AttrList", list)) << xml_src;
    ASSERT_EQ(list.items.size(), 1U);
    const auto& item = list.items[0];
    EXPECT_EQ(item.a1, 10);
    EXPECT_EQ(item.a2, 20);
    EXPECT_EQ(item.a3, 30);
    EXPECT_EQ(item.a4, 40);
    EXPECT_EQ(item.a5, 50);
    EXPECT_EQ(item.s1, "one");
    EXPECT_EQ(item.s2, "two");
    EXPECT_EQ(item.s3, "three");
    EXPECT_EQ(item.s4, "four");
    EXPECT_EQ(item.s5, "five");
  }
}

/// @brief FlatItem: mixed attrs + child elements.
TEST_F(LightningBasicTests, FlatListParsing) {
  const std::string_view xml_src = R"(
<FlatList>
  <Item id="1">
    <title>First</title>
    <desc>Description one</desc>
    <status>0</status>
  </Item>
  <Item id="2">
    <title>Second</title>
    <desc>Description two</desc>
    <status>1</status>
  </Item>
</FlatList>)";
  xmlight::Parser parser{xml_src};
  FlatList list;
  ASSERT_TRUE(xmlight::deserialize(parser, "FlatList", list));
  ASSERT_EQ(list.items.size(), 2U);
  EXPECT_EQ(list.items[0].id, 1);
  EXPECT_EQ(list.items[0].title, "First");
  EXPECT_EQ(list.items[0].description, "Description one");
  EXPECT_EQ(list.items[0].status, 0);
  EXPECT_EQ(list.items[1].id, 2);
  EXPECT_EQ(list.items[1].title, "Second");
  EXPECT_EQ(list.items[1].description, "Description two");
  EXPECT_EQ(list.items[1].status, 1);
}

/// @brief Very deeply nested unknown subtree must be fully skipped.
TEST_F(LightningBasicTests, SkipsVeryDeeplyNestedUnknownSubtree) {
  std::string xml = "<person><name>Alice</name><unknown>";
  for (int i = 0; i < 50; ++i) {
    xml += "<level" + std::to_string(i) + ">";
  }
  xml += "deep";
  for (int i = 49; i >= 0; --i) {
    xml += "</level" + std::to_string(i) + ">";
  }
  xml += "</unknown><age>25</age></person>";

  xmlight::Parser parser{xml};
  Person person;
  ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Alice");
  EXPECT_EQ(person.age, 25);
}

/// @brief Comment delimiter edges: isolated dashes and "->" near-misses must
/// not terminate the scan early, while interior "--" is a well-formedness
/// error (MalformedComment) caught in the same single pass.
TEST_F(LightningBasicTests, CommentDelimiterEdges) {
  {
    const std::string_view xml_src = R"(
<person>
  <!-- tricky - dashes -> not yet > still going - almost -->
  <name>Survived</name>
  <age>1</age>
</person>)";
    xmlight::Parser parser{xml_src};
    Person person;
    ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
    EXPECT_EQ(person.name, "Survived");
    EXPECT_EQ(person.age, 1);
  }
  {
    xmlight::Parser parser{
        std::string_view(R"(<person><!-- bad -- comment --><name>X</name></person>)")};
    Person person;
    EXPECT_FALSE(xmlight::deserialize(parser, "person", person));
    EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::MalformedComment);
  }
}

/// @brief Verifies that long comments interleaved with elements are
/// correctly skipped and all element data is parsed. Mirrors the
/// comment-heavy benchmark payload structure.
TEST_F(LightningBasicTests, CommentHeavyPayloadParsesCorrectly) {
  const std::string filler(480, '=');
  std::string xml = "<Users>\n";
  for (int i = 0; i < 50; ++i) {
    xml += "  <!-- comment " + std::to_string(i) + " " + filler + " -->\n";
    xml += "  <User id=\"" + std::to_string(i) + "\">\n";
    xml += "    <Name>User " + std::to_string(i) + "</Name>\n";
    xml += "    <Email>u" + std::to_string(i) + "@e.com</Email>\n";
    xml += "  </User>\n";
  }
  // Trailing comment after the last element.
  xml += "  <!-- final trailing comment " + filler + " -->\n";
  xml += "</Users>";

  xmlight::Parser parser{xml};
  Users users;
  ASSERT_TRUE(xmlight::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 50U);
  for (size_t i = 0; i < 50; ++i) {
    EXPECT_EQ(users.items[i].id, i);
    EXPECT_EQ(users.items[i].name, "User " + std::to_string(i));
    EXPECT_EQ(users.items[i].email, "u" + std::to_string(i) + "@e.com");
  }
}

/// @brief std::string fields and attributes materialize owned copies that
/// survive the source buffer; self-closing elements yield empty strings.
TEST_F(LightningBasicTests, OwnedStringFieldsMaterializeData) {
  {
    OwnedPerson person;
    {
      // Source buffer scoped - destroyed before assertions.
      const std::string xml =
          "<person><name>Alice</name><age>30</age>"
          "<email>alice@example.com</email></person>";
      xmlight::Parser parser{xml};
      ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
    }
    EXPECT_EQ(person.name, "Alice");
    EXPECT_EQ(person.age, 30);
    EXPECT_EQ(person.email, "alice@example.com");
  }
  {
    OwnedUser user;
    {
      const std::string xml = R"(<OwnedUser id="42" role="admin"><Name>Bob</Name></OwnedUser>)";
      xmlight::Parser parser{xml};
      ASSERT_TRUE(xmlight::deserialize(parser, "OwnedUser", user));
    }
    EXPECT_EQ(user.id, 42);
    EXPECT_EQ(user.role, "admin");
    EXPECT_EQ(user.name, "Bob");
  }
  {
    OwnedList list;
    {
      const std::string xml =
          "<OwnedList><Tag>Alpha</Tag><Tag>Beta</Tag><Tag>Gamma</Tag></"
          "OwnedList>";
      xmlight::Parser parser{xml};
      ASSERT_TRUE(xmlight::deserialize(parser, "OwnedList", list));
    }
    ASSERT_EQ(list.tags.size(), 3U);
    EXPECT_EQ(list.tags[0], "Alpha");
    EXPECT_EQ(list.tags[1], "Beta");
    EXPECT_EQ(list.tags[2], "Gamma");
  }
  {
    OwnedPerson person;
    xmlight::Parser parser{std::string_view("<person><name/><age>25</age><email/></person>")};
    ASSERT_TRUE(xmlight::deserialize(parser, "person", person));
    EXPECT_TRUE(person.name.empty());
    EXPECT_EQ(person.age, 25);
    EXPECT_TRUE(person.email.empty());
  }
}

/// @brief Fixed-array fill levels: exact fill, underfill (remaining slots keep
/// defaults), overfill (extras silently skipped), empty container, and a
/// self-closing root.
TEST_F(LightningBasicTests, ArrFieldFillLevels) {
  {
    const std::string_view xml_src = R"(
<FixedSkills>
  <Skill>C++</Skill>
  <Skill>Rust</Skill>
  <Skill>Go</Skill>
</FixedSkills>)";
    xmlight::Parser parser{xml_src};
    FixedSkills skills;
    ASSERT_TRUE(xmlight::deserialize(parser, "FixedSkills", skills));
    EXPECT_EQ(skills.items[0], "C++");
    EXPECT_EQ(skills.items[1], "Rust");
    EXPECT_EQ(skills.items[2], "Go");
  }
  {
    xmlight::Parser parser{
        std::string_view("<FixedSkills>\n  <Skill>Python</Skill>\n</FixedSkills>")};
    FixedSkills skills;
    ASSERT_TRUE(xmlight::deserialize(parser, "FixedSkills", skills));
    EXPECT_EQ(skills.items[0], "Python");
    EXPECT_TRUE(skills.items[1].empty());
    EXPECT_TRUE(skills.items[2].empty());
  }
  {
    const std::string_view xml_src = R"(
<FixedSkills>
  <Skill>A</Skill>
  <Skill>B</Skill>
  <Skill>C</Skill>
  <Skill>D</Skill>
  <Skill>E</Skill>
</FixedSkills>)";
    xmlight::Parser parser{xml_src};
    FixedSkills skills;
    ASSERT_TRUE(xmlight::deserialize(parser, "FixedSkills", skills));
    EXPECT_EQ(skills.items[0], "A");
    EXPECT_EQ(skills.items[1], "B");
    EXPECT_EQ(skills.items[2], "C");
  }
  for (const std::string_view xml_src : {std::string_view(R"(<FixedSkills></FixedSkills>)"),
                                         std::string_view(R"(<FixedSkills/>)")}) {
    xmlight::Parser parser{xml_src};
    FixedSkills skills;
    ASSERT_TRUE(xmlight::deserialize(parser, "FixedSkills", skills)) << xml_src;
    EXPECT_TRUE(skills.items[0].empty());
    EXPECT_TRUE(skills.items[1].empty());
    EXPECT_TRUE(skills.items[2].empty());
  }
}

/// @brief Arr field mixed with attr and element fields (N>1 dispatch table
/// path): normal fill, overflow skipped cleanly, and a bad item value failing
/// the parse.
TEST_F(LightningBasicTests, ArrFieldMixedWithOtherFields) {
  {
    const std::string_view xml_src = R"(
<MixedRecord id="7">
  <Name>Alice</Name>
  <Score>95</Score>
  <Score>87</Score>
  <Score>92</Score>
</MixedRecord>)";
    xmlight::Parser parser{xml_src};
    MixedRecord rec;
    ASSERT_TRUE(xmlight::deserialize(parser, "MixedRecord", rec));
    EXPECT_EQ(rec.id, 7);
    EXPECT_EQ(rec.name, "Alice");
    EXPECT_EQ(rec.scores[0], 95);
    EXPECT_EQ(rec.scores[1], 87);
    EXPECT_EQ(rec.scores[2], 92);
    EXPECT_EQ(rec.scores[3], 0);
  }
  {
    const std::string_view xml_src = R"(
<MixedRecord id="1">
  <Score>1</Score><Score>2</Score><Score>3</Score><Score>4</Score>
  <Score>5</Score><Score>6</Score>
  <Name>Bob</Name>
</MixedRecord>)";
    xmlight::Parser parser{xml_src};
    MixedRecord rec;
    ASSERT_TRUE(xmlight::deserialize(parser, "MixedRecord", rec));
    EXPECT_EQ(rec.id, 1);
    EXPECT_EQ(rec.name, "Bob");
    EXPECT_EQ(rec.scores[0], 1);
    EXPECT_EQ(rec.scores[1], 2);
    EXPECT_EQ(rec.scores[2], 3);
    EXPECT_EQ(rec.scores[3], 4);
  }
  {
    xmlight::Parser parser{
        std::string_view(R"(<MixedRecord id="1"><Name>n</Name><Score>oops</Score></MixedRecord>)")};
    MixedRecord rec;
    EXPECT_FALSE(xmlight::deserialize(parser, "MixedRecord", rec));
    EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::InvalidNumericValue);
  }
}

/// @brief An unknown element whose name extends a mapped field's name
/// ("titles" vs "title") must not be matched by the document-order fast
/// path; it is skipped and all mapped siblings parse correctly.
TEST_F(LightningBasicTests, UnknownPrefixNamedSiblingIsSkipped) {
  const std::string_view xml_src = R"(
<FlatList>
  <Item id="1">
    <titles>fake</titles>
    <title>Real</title>
    <desc>D</desc>
    <status>2</status>
  </Item>
</FlatList>)";
  xmlight::Parser parser{xml_src};
  FlatList list;
  ASSERT_TRUE(xmlight::deserialize(parser, "FlatList", list));
  ASSERT_EQ(list.items.size(), 1U);
  EXPECT_EQ(list.items[0].id, 1);
  EXPECT_EQ(list.items[0].title, "Real");
  EXPECT_EQ(list.items[0].description, "D");
  EXPECT_EQ(list.items[0].status, 2);
}

/// @brief Fields arriving out of metadata order across consecutive items
/// must parse correctly, including when document order is later restored
/// (exercises hint misses and re-synchronisation).
TEST_F(LightningBasicTests, OutOfOrderThenInOrderItems) {
  const std::string_view xml_src = R"(
<FlatList>
  <Item id="1">
    <status>5</status>
    <title>A</title>
    <desc>da</desc>
  </Item>
  <Item id="2">
    <desc>db</desc>
    <status>6</status>
    <title>B</title>
  </Item>
  <Item id="3">
    <title>C</title>
    <desc>dc</desc>
    <status>7</status>
  </Item>
</FlatList>)";
  xmlight::Parser parser{xml_src};
  FlatList list;
  ASSERT_TRUE(xmlight::deserialize(parser, "FlatList", list));
  ASSERT_EQ(list.items.size(), 3U);
  EXPECT_EQ(list.items[0].title, "A");
  EXPECT_EQ(list.items[0].description, "da");
  EXPECT_EQ(list.items[0].status, 5);
  EXPECT_EQ(list.items[1].title, "B");
  EXPECT_EQ(list.items[1].description, "db");
  EXPECT_EQ(list.items[1].status, 6);
  EXPECT_EQ(list.items[2].title, "C");
  EXPECT_EQ(list.items[2].description, "dc");
  EXPECT_EQ(list.items[2].status, 7);
}

/// @brief Bool fields accept the XML Schema boolean lexical space
/// ("true", "false", "1", "0") as both attributes and elements.
TEST_F(LightningBasicTests, BoolFieldsAllLexicalForms) {
  {
    const std::string_view xml_src =
        R"(<Toggle enabled="true"><active>1</active><verbose>false</verbose></Toggle>)";
    xmlight::Parser parser{xml_src};
    Toggle t;
    ASSERT_TRUE(xmlight::deserialize(parser, "Toggle", t));
    EXPECT_TRUE(t.enabled);
    EXPECT_TRUE(t.active);
    EXPECT_FALSE(t.verbose);
  }
  {
    const std::string_view xml_src =
        R"(<Toggle enabled="0"><active>false</active><verbose>true</verbose></Toggle>)";
    xmlight::Parser parser{xml_src};
    Toggle t;
    t.enabled = true;
    ASSERT_TRUE(xmlight::deserialize(parser, "Toggle", t));
    EXPECT_FALSE(t.enabled);
    EXPECT_FALSE(t.active);
    EXPECT_TRUE(t.verbose);
  }
}

/// @brief Invalid bool text (element or attribute) and a self-closing bool
/// leaf are hard errors, mirroring numeric fields.
TEST_F(LightningBasicTests, BoolInvalidValuesFail) {
  for (const std::string_view xml_src :
       {std::string_view(R"(<Toggle enabled="1"><active>yes</active></Toggle>)"),
        std::string_view(
            R"(<Toggle enabled="maybe"><active>1</active><verbose>0</verbose></Toggle>)"),
        std::string_view(R"(<Toggle enabled="true"><active/><verbose>true</verbose></Toggle>)")}) {
    xmlight::Parser parser{xml_src};
    Toggle t;
    EXPECT_FALSE(xmlight::deserialize(parser, "Toggle", t)) << xml_src;
    EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::InvalidNumericValue) << xml_src;
  }
}

/// @brief Every field kind round-trips through the serializer: bools
/// ("true"/"false"), nested primitives, attributes, vectors (including
/// empty), fixed arrays, attr-only types (self-closing), and optional
/// attributes (omitted when absent).
TEST_F(LightningBasicTests, SerializerRoundTripsFieldKinds) {
  {
    Toggle t;
    t.enabled = true;
    t.active = false;
    t.verbose = true;
    const std::string xml = xmlight::serialize("Toggle", t);
    EXPECT_NE(xml.find("enabled=\"true\""), std::string::npos);
    EXPECT_NE(xml.find("<active>false</active>"), std::string::npos);
    xmlight::Parser parser{xml};
    Toggle out;
    ASSERT_TRUE(xmlight::deserialize(parser, "Toggle", out));
    EXPECT_TRUE(out.enabled);
    EXPECT_FALSE(out.active);
    EXPECT_TRUE(out.verbose);
  }
  {
    Person person;
    person.name = "Alice";
    person.age = 30;
    person.address.street = "Main St";
    person.address.zip = 12345;
    const std::string xml = xmlight::serialize("person", person);
    xmlight::Parser parser{xml};
    Person out;
    ASSERT_TRUE(xmlight::deserialize(parser, "person", out));
    EXPECT_EQ(out.name, "Alice");
    EXPECT_EQ(out.age, 30);
    EXPECT_EQ(out.address.street, "Main St");
    EXPECT_EQ(out.address.zip, 12345);
  }
  {
    User user;
    user.id = 99;
    user.name = "Grace Hopper";
    user.email = "grace@example.com";
    const std::string xml = xmlight::serialize("User", user);
    xmlight::Parser parser{xml};
    User out;
    ASSERT_TRUE(xmlight::deserialize(parser, "User", out));
    EXPECT_EQ(out.id, 99);
    EXPECT_EQ(out.name, "Grace Hopper");
    EXPECT_EQ(out.email, "grace@example.com");
  }
  {
    Users users;
    users.items.push_back({1, "Ada", "ada@example.com"});
    users.items.push_back({2, "Grace", "grace@example.com"});
    const std::string xml = xmlight::serialize("Users", users);
    xmlight::Parser parser{xml};
    Users out;
    ASSERT_TRUE(xmlight::deserialize(parser, "Users", out));
    ASSERT_EQ(out.items.size(), 2U);
    EXPECT_EQ(out.items[0].id, 1);
    EXPECT_EQ(out.items[0].name, "Ada");
    EXPECT_EQ(out.items[1].id, 2);
    EXPECT_EQ(out.items[1].name, "Grace");
  }
  {
    const Users users;
    const std::string xml = xmlight::serialize("Users", users);
    xmlight::Parser parser{xml};
    Users out;
    ASSERT_TRUE(xmlight::deserialize(parser, "Users", out));
    EXPECT_TRUE(out.items.empty());
  }
  {
    FixedSkills skills;
    skills.items[0] = "C++";
    skills.items[1] = "Rust";
    skills.items[2] = "Go";
    const std::string xml = xmlight::serialize("FixedSkills", skills);
    xmlight::Parser parser{xml};
    FixedSkills out;
    ASSERT_TRUE(xmlight::deserialize(parser, "FixedSkills", out));
    EXPECT_EQ(out.items[0], "C++");
    EXPECT_EQ(out.items[1], "Rust");
    EXPECT_EQ(out.items[2], "Go");
  }
  {
    AttrItem item;
    item.a1 = 1;
    item.a2 = 2;
    item.s1 = "hello";
    const std::string xml = xmlight::serialize("Item", item);
    EXPECT_NE(xml.find("/>"), std::string::npos);
    xmlight::Parser parser{xml};
    AttrItem out;
    ASSERT_TRUE(xmlight::deserialize(parser, "Item", out));
    EXPECT_EQ(out.a1, 1);
    EXPECT_EQ(out.s1, "hello");
  }
}

TEST_F(LightningBasicTests, SerializerCompactMode) {
  Person person;
  person.name = "Bob";
  person.age = 25;

  const std::string pretty = xmlight::serialize<true>("person", person);
  const std::string compact = xmlight::serialize<false>("person", person);

  EXPECT_NE(pretty.find('\n'), std::string::npos);
  EXPECT_EQ(compact.find('\n'), std::string::npos);

  xmlight::Parser parser{compact};
  Person out;
  ASSERT_TRUE(xmlight::deserialize(parser, "person", out));
  EXPECT_EQ(out.name, "Bob");
  EXPECT_EQ(out.age, 25);
}

TEST_F(LightningBasicTests, SerializerRoundTripOrganization) {
  Organization org;
  org.id = 1;
  org.name = "Acme";
  OrgDepartment dept;
  dept.id = 10;
  dept.name = "Eng";
  OrgTeam team;
  team.id = 100;
  team.name = "Platform";
  OrgMember member;
  member.id = 1001;
  member.role = "Engineer";
  member.full_name = "Ada";
  member.email = "ada@acme.com";
  member.skills.items = {"C++", "Rust"};
  team.members.push_back(member);
  dept.teams.push_back(team);
  org.departments.push_back(dept);

  const std::string xml = xmlight::serialize("Organization", org);

  xmlight::Parser parser{xml};
  Organization out;
  ASSERT_TRUE(xmlight::deserialize(parser, "Organization", out));
  ASSERT_EQ(out.departments.size(), 1U);
  ASSERT_EQ(out.departments[0].teams.size(), 1U);
  ASSERT_EQ(out.departments[0].teams[0].members.size(), 1U);
  EXPECT_EQ(out.departments[0].teams[0].members[0].full_name, "Ada");
  ASSERT_EQ(out.departments[0].teams[0].members[0].skills.items.size(), 2U);
  EXPECT_EQ(out.departments[0].teams[0].members[0].skills.items[0], "C++");
}

struct ReqRecord {
  int id{};               // required attribute
  std::string_view name;  // required element
  std::string_view note;  // optional element
};

template<>
struct xmlight::XmlMetadata<ReqRecord> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("id", &ReqRecord::id, true),
                                                 xmlight::field("name", &ReqRecord::name, true),
                                                 xmlight::field("note", &ReqRecord::note));
};

struct ReqList {
  std::vector<std::string_view> items;  // required: at least one
};

template<>
struct xmlight::XmlMetadata<ReqList> {
  static constexpr auto fields = std::make_tuple(xmlight::vecField("item", &ReqList::items, true));
};

struct ReqParent {
  ReqRecord child;  // required nested object
};

template<>
struct xmlight::XmlMetadata<ReqParent> {
  static constexpr auto fields =
      std::make_tuple(xmlight::field("ReqRecord", &ReqParent::child, true));
};

/// @brief Required fields are satisfied with the optional absent, present, or
/// out of metadata order; a required container with children exercises the
/// N==1 fast-path bit set, and a fully populated required nested object
/// succeeds.
TEST_F(LightningBasicTests, RequiredFieldsSatisfied) {
  for (const std::string_view xml_src :
       {std::string_view(R"(<ReqRecord id="7"><name>Ada</name></ReqRecord>)"),
        std::string_view(R"(<ReqRecord id="7"><name>Ada</name><note>hi</note></ReqRecord>)"),
        std::string_view(R"(<ReqRecord id="7"><note>hi</note><name>Ada</name></ReqRecord>)")}) {
    xmlight::Parser parser{xml_src};
    ReqRecord rec;
    ASSERT_TRUE(xmlight::deserialize(parser, "ReqRecord", rec)) << xml_src;
    EXPECT_EQ(rec.id, 7);
    EXPECT_EQ(rec.name, "Ada");
  }
  {
    xmlight::Parser parser{std::string_view(R"(<ReqList><item>a</item><item>b</item></ReqList>)")};
    ReqList list;
    ASSERT_TRUE(xmlight::deserialize(parser, "ReqList", list));
    ASSERT_EQ(list.items.size(), 2U);
    EXPECT_EQ(list.items[0], "a");
  }
  {
    xmlight::Parser parser{std::string_view(
        R"(<ReqParent><ReqRecord id="1"><name>Ada</name></ReqRecord></ReqParent>)")};
    ReqParent p;
    ASSERT_TRUE(xmlight::deserialize(parser, "ReqParent", p));
    EXPECT_EQ(p.child.id, 1);
    EXPECT_EQ(p.child.name, "Ada");
  }
}

/// @brief Every way a required field can be missing fails with
/// MissingRequiredField: absent child element, absent attribute, a
/// self-closing element (cannot satisfy a required child even with its
/// required attribute present), an empty required container, an absent
/// required nested object, and a present-but-incomplete nested object
/// (propagated from the inner pull()).
TEST_F(LightningBasicTests, MissingRequiredFieldVariantsFail) {
  for (const std::string_view xml_src :
       {std::string_view(R"(<ReqRecord id="7"><note>hi</note></ReqRecord>)"),
        std::string_view(R"(<ReqRecord><name>Ada</name></ReqRecord>)"),
        std::string_view(R"(<ReqRecord id="7"/>)")}) {
    xmlight::Parser parser{xml_src};
    ReqRecord rec;
    EXPECT_FALSE(xmlight::deserialize(parser, "ReqRecord", rec)) << xml_src;
    EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::MissingRequiredField) << xml_src;
  }
  {
    xmlight::Parser parser{std::string_view(R"(<ReqList></ReqList>)")};
    ReqList list;
    EXPECT_FALSE(xmlight::deserialize(parser, "ReqList", list));
    EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::MissingRequiredField);
  }
  for (const std::string_view xml_src :
       {std::string_view(R"(<ReqParent></ReqParent>)"),
        std::string_view(R"(<ReqParent><ReqRecord id="1"></ReqRecord></ReqParent>)")}) {
    xmlight::Parser parser{xml_src};
    ReqParent p;
    EXPECT_FALSE(xmlight::deserialize(parser, "ReqParent", p)) << xml_src;
    EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::MissingRequiredField) << xml_src;
  }
}

/// @brief Exactly kMaxAttributesPerElement attributes is accepted; one past
/// the cap is rejected with TooManyAttributes.
TEST_F(LightningBasicTests, MaxAttributesBoundary) {
  const auto make_xml = [](const size_t count) {
    std::string xml = "<User";
    xml.reserve(count * 5 + 16);
    for (size_t i = 0; i < count; ++i) {
      xml += R"( a="")";
    }
    xml += "/>";
    return xml;
  };
  {
    const std::string xml = make_xml(xmlight::Parser::MAX_ATTRIBUTES_PER_ELEMENT);
    xmlight::Parser parser{xml};
    User user;
    EXPECT_TRUE(xmlight::deserialize(parser, "User", user));
  }
  {
    const std::string xml = make_xml(xmlight::Parser::MAX_ATTRIBUTES_PER_ELEMENT + 1);
    xmlight::Parser parser{xml};
    User user;
    EXPECT_FALSE(xmlight::deserialize(parser, "User", user));
    EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::TooManyAttributes);
  }
}

// Normalization & reference expansion (NormalizingParser, opt-in)
//
// Normalization is gated on BasicParser<true> (xmlight::NormalizingParser) and only
// applies to owning std::string fields; std::string_view fields stay raw and
// zero-copy. The default xmlight::Parser compiles these paths away entirely.

struct NormRecord {
  std::string body;      // element text: expanded + EOL-normalized
  std::string attr;      // attribute value: expanded + whitespace-normalized
  std::string_view raw;  // string_view stays raw even under normalization
};

template<>
struct xmlight::XmlMetadata<NormRecord> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("a", &NormRecord::attr),
                                                 xmlight::field("body", &NormRecord::body),
                                                 xmlight::field("raw", &NormRecord::raw));
};

struct NormText {
  std::string v;
};

template<>
struct xmlight::XmlMetadata<NormText> {
  static constexpr auto fields = std::make_tuple(xmlight::field("v", &NormText::v));
};

/// @brief Reference expansion in element text: the five predefined entities,
/// plus decimal and hex character references including a multi-byte code
/// point encoded as UTF-8.
TEST_F(LightningBasicTests, NormalizeReferenceExpansion) {
  {
    const std::string_view src =
        R"(<NormText><v>a &amp; b &lt; c &gt; d &apos; e &quot; f</v></NormText>)";
    xmlight::NormalizingParser p{src};
    NormText t;
    ASSERT_TRUE(xmlight::deserialize(p, "NormText", t));
    EXPECT_EQ(t.v, "a & b < c > d ' e \" f");
  }
  {
    xmlight::NormalizingParser p{
        std::string_view(R"(<NormText><v>&#65;&#x42;&#x2764;</v></NormText>)")};
    NormText t;
    ASSERT_TRUE(xmlight::deserialize(p, "NormText", t));
    EXPECT_EQ(t.v,
              std::string("AB") + "\xE2\x9D\xA4");  // U+2764 HEAVY BLACK HEART
  }
}

/// @brief Text normalization forms: CDATA content is copied literally (no
/// expansion) and concatenates with surrounding runs; CR and CRLF normalize
/// to a single LF while tabs survive.
TEST_F(LightningBasicTests, NormalizeTextForms) {
  {
    xmlight::NormalizingParser p{
        std::string_view(R"(<NormText><v>x &amp; <![CDATA[y &amp; z]]> w</v></NormText>)")};
    NormText t;
    ASSERT_TRUE(xmlight::deserialize(p, "NormText", t));
    EXPECT_EQ(t.v, "x & y &amp; z w");
  }
  {
    const std::string src = "<NormText><v>a\r\nb\rc\td</v></NormText>";
    xmlight::NormalizingParser p{src};
    NormText t;
    ASSERT_TRUE(xmlight::deserialize(p, "NormText", t));
    EXPECT_EQ(t.v, "a\nb\nc\td");
  }
}

/// @brief Attribute values: literal whitespace (tab/newline) becomes a space,
/// but whitespace introduced via a character reference is preserved literally.
TEST_F(LightningBasicTests, NormalizeAttributeWhitespace) {
  const std::string src = "<NormRecord a=\"x\ty\nz&#9;w\"><body>b</body><raw>r</raw></NormRecord>";
  xmlight::NormalizingParser p{src};
  NormRecord r;
  ASSERT_TRUE(xmlight::deserialize(p, "NormRecord", r));
  EXPECT_EQ(r.attr, "x y z\tw");  // literal tab/LF -> space; &#9; stays a tab
}

/// @brief Normalization is opt-in and scoped to owning fields:
/// std::string_view fields stay raw even on the normalizing parser, and the
/// default parser performs no expansion at all (byte-for-byte fidelity).
TEST_F(LightningBasicTests, NormalizationScopeOptIn) {
  {
    const std::string_view src =
        R"(<NormRecord a="ok"><body>m &amp; n</body><raw>p &amp; q</raw></NormRecord>)";
    xmlight::NormalizingParser p{src};
    NormRecord r;
    ASSERT_TRUE(xmlight::deserialize(p, "NormRecord", r));
    EXPECT_EQ(r.body, "m & n");     // owning std::string -> expanded
    EXPECT_EQ(r.raw, "p &amp; q");  // string_view -> raw, zero-copy
  }
  {
    xmlight::Parser p{std::string_view(R"(<NormText><v>a &amp; b</v></NormText>)")};
    NormText t;
    ASSERT_TRUE(xmlight::deserialize(p, "NormText", t));
    EXPECT_EQ(t.v, "a &amp; b");
  }
}

/// @brief Bad references are hard errors on the normalizing path: undefined
/// entities (text and attribute), malformed character references, and code
/// points outside the XML Char production.
TEST_F(LightningBasicTests, NormalizeBadReferencesFail) {
  constexpr auto TEXT_CASES = std::to_array<std::pair<std::string_view, xmlight::ErrorCode>>({
      {R"(<NormText><v>a &bogus; b</v></NormText>)", xmlight::ErrorCode::UndefinedEntity},
      {R"(<NormText><v>&#xZZ;</v></NormText>)", xmlight::ErrorCode::InvalidCharRef},
      {R"(<NormText><v>&#0;</v></NormText>)", xmlight::ErrorCode::InvalidCharRef},
  });
  for (const auto& [src, code] : TEXT_CASES) {
    xmlight::NormalizingParser p{src};
    NormText t;
    EXPECT_FALSE(xmlight::deserialize(p, "NormText", t)) << src;
    EXPECT_EQ(p.errorCode(), code) << src;
  }
  {
    xmlight::NormalizingParser p{
        std::string_view(R"(<NormRecord a="x &bogus; y"><body>b</body><raw>r</raw></NormRecord>)")};
    NormRecord r;
    EXPECT_FALSE(xmlight::deserialize(p, "NormRecord", r));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UndefinedEntity);
  }
}

// Strict (fully-conforming) parser: no false positives
//
// StrictParser enforces the three WFCs (rejection cases live in the
// conformance suite). These guard against false positives and confirm it still
// normalizes (StrictParser = normalize + strict).

/// @brief No false positives on the "]]>" CharData check: a CDATA section's
/// own terminator is fine (the check runs on text, not CDATA content), and
/// "]]" without a following '>' is well-formed.
TEST_F(LightningBasicTests, StrictAcceptsCDataEdges) {
  constexpr auto CASES = std::to_array<std::pair<std::string_view, std::string_view>>({
      {R"(<NormText><v><![CDATA[x]]></v></NormText>)", "x"},
      {R"(<NormText><v>a ]] b</v></NormText>)", "a ]] b"},
  });
  for (const auto& [src, want] : CASES) {
    xmlight::StrictParser p{src};
    NormText t;
    ASSERT_TRUE(xmlight::deserialize(p, "NormText", t)) << src;
    EXPECT_EQ(t.v, want);
  }
}

/// @brief StrictParser is fully conforming: it still expands references and
/// normalizes into owning std::string fields.
TEST_F(LightningBasicTests, StrictParserNormalizes) {
  const std::string_view xml_src =
      R"(<NormRecord a="x&#9;y"><body>m &amp; n</body><raw>r</raw></NormRecord>)";
  xmlight::StrictParser p{xml_src};
  NormRecord r;
  ASSERT_TRUE(xmlight::deserialize(p, "NormRecord", r));
  EXPECT_EQ(r.body, "m & n");  // entity expanded
  EXPECT_EQ(r.attr, "x\ty");   // &#9; preserved as a literal tab
}

/// @brief Strict streamed attributes: attribute lists on a child element (the
/// hint fast path, where streamAttrs runs) parse identically to the fast
/// parser across every shape — schema-ordered full set (terminator epilogue),
/// ordered prefix (in-fold terminator), and each deviating shape that rewinds
/// through parseAttributes.
TEST_F(LightningBasicTests, StrictStreamedAttrShapesMatchFastParser) {
  constexpr auto CASES = std::to_array<std::string_view>({
      // Full set, in order, self-closing: the fold exhausts and the epilogue
      // consumes '/>'.
      R"(<AttrList><Item a1="10" a2="20" a3="30" a4="40" a5="50" s1="one" s2="two" s3="three" s4="four" s5="five"/></AttrList>)",
      // Full set, whitespace before the terminator, open + close tag.
      R"(<AttrList><Item a1="10" a2="20" a3="30" a4="40" a5="50" s1="one" s2="two" s3="three" s4="four" s5="five" ></Item></AttrList>)",
      // Ordered prefix: the terminator is recognized inside the fold.
      R"(<AttrList><Item a1="10" a2="20"/></AttrList>)",
      // Out of order: rewinds to parseAttributes.
      R"(<AttrList><Item s5="five" a1="10" s1="one" a5="50" a2="20" s2="two" a3="30" s4="four" a4="40" s3="three"/></AttrList>)",
      // Unknown attribute up front: rewinds.
      R"(<AttrList><Item zz="?" a1="10" a2="20" s1="one"/></AttrList>)",
      // Whitespace around '=': rewinds.
      R"(<AttrList><Item a1 = "10" a2="20"/></AttrList>)",
      // Unknown extra after the full set: epilogue miss rewinds.
      R"(<AttrList><Item a1="10" a2="20" a3="30" a4="40" a5="50" s1="one" s2="two" s3="three" s4="four" s5="five" zz="?"/></AttrList>)",
      // Prefixed names with the same local name: rewinds, not a duplicate.
      R"(<AttrList><Item a:s1="x" b:s1="y"/></AttrList>)",
  });
  for (const auto& xml_src : CASES) {
    xmlight::Parser fast{xml_src};
    xmlight::StrictParser strict{xml_src};
    AttrList want;
    AttrList got;
    ASSERT_TRUE(xmlight::deserialize(fast, "AttrList", want)) << xml_src;
    ASSERT_TRUE(xmlight::deserialize(strict, "AttrList", got)) << xml_src;
    ASSERT_EQ(got.items.size(), want.items.size()) << xml_src;
    for (size_t i = 0; i < want.items.size(); ++i) {
      const auto& w = want.items[i];
      const auto& g = got.items[i];
      EXPECT_EQ(g.a1, w.a1) << xml_src;
      EXPECT_EQ(g.a2, w.a2) << xml_src;
      EXPECT_EQ(g.a3, w.a3) << xml_src;
      EXPECT_EQ(g.a4, w.a4) << xml_src;
      EXPECT_EQ(g.a5, w.a5) << xml_src;
      EXPECT_EQ(g.s1, w.s1) << xml_src;
      EXPECT_EQ(g.s2, w.s2) << xml_src;
      EXPECT_EQ(g.s3, w.s3) << xml_src;
      EXPECT_EQ(g.s4, w.s4) << xml_src;
      EXPECT_EQ(g.s5, w.s5) << xml_src;
    }
  }
}

/// @brief Strict WFC violations on a child element's attribute list are still
/// caught with streaming enabled: the value checks fire inline in phase 1, and
/// everything else is caught by the rewind through parseAttributes.
TEST_F(LightningBasicTests, StrictStreamedAttrRejections) {
  constexpr auto CASES = std::to_array<std::pair<std::string_view, xmlight::ErrorCode>>({
      // '<' in a phase-1-matched value.
      {R"(<AttrList><Item a1="1<2"/></AttrList>)", xmlight::ErrorCode::LtInAttributeValue},
      // Control byte in a phase-1-matched value.
      {"<AttrList><Item a1=\"1\x01\"/></AttrList>", xmlight::ErrorCode::ForbiddenControlChar},
      // Duplicate of an earlier ordinal: pattern miss, rewind, WFC check.
      {R"(<AttrList><Item a1="1" a1="2"/></AttrList>)", xmlight::ErrorCode::DuplicateAttribute},
      // Duplicate after the full set: epilogue miss, rewind, WFC check.
      {R"(<AttrList><Item a1="10" a2="20" a3="30" a4="40" a5="50" s1="one" s2="two" s3="three" s4="four" s5="five" s5="again"/></AttrList>)",
       xmlight::ErrorCode::DuplicateAttribute},
      // Unquoted value: the rewind reproduces the legacy error.
      {R"(<AttrList><Item a1=1/></AttrList>)", xmlight::ErrorCode::ExpectedQuotedValue},
      // Truncated inside a value.
      {R"(<AttrList><Item a1="1)", xmlight::ErrorCode::UnterminatedAttributeValue},
      // Truncated in the attribute list.
      {R"(<AttrList><Item a1="1" )", xmlight::ErrorCode::UnclosedTag},
  });
  for (const auto& [xml_src, want] : CASES) {
    xmlight::StrictParser p{xml_src};
    AttrList list;
    EXPECT_FALSE(xmlight::deserialize(p, "AttrList", list)) << xml_src;
    EXPECT_EQ(p.errorCode(), want) << xml_src;
  }
}

/// @brief Fused strict CharData validation: "]]>" spanning a 16-byte chunk
/// boundary, CDataEndInContent precedence over ForbiddenControlChar in both
/// document orders, and no false positives on trailing or dense ']' content —
/// through the scalar-leaf fast path and the tokenized mixed-content path.
TEST_F(LightningBasicTests, StrictCharDataValidationFused) {
  const std::string fourteen(14, 'a');
  const std::string fifteen(15, 'a');
  const std::string sixteen(16, 'a');
  const auto REJECT = std::to_array<std::pair<std::string, xmlight::ErrorCode>>({
      // "]]" ends the first 16-byte chunk, '>' opens the next.
      {fourteen + "]]>", xmlight::ErrorCode::CDataEndInContent},
      // "]]>" split ']' / "]>" across the chunk boundary.
      {fifteen + "]]>", xmlight::ErrorCode::CDataEndInContent},
      // Control byte before a later "]]>": the delimiter still wins.
      {"a\x01" + std::string(18, 'b') + "]]>zz", xmlight::ErrorCode::CDataEndInContent},
      // "]]" without '>' ruled out, control byte in a later chunk reports.
      {"]]" + std::string(20, 'b') + "\x01" + std::string(10, 'c'),
       xmlight::ErrorCode::ForbiddenControlChar},
      // Short run: byte-loop tail only.
      {std::string("b\x01d"), xmlight::ErrorCode::ForbiddenControlChar},
  });
  for (const auto& [text, want] : REJECT) {
    const std::string xml = "<NormText><v>" + text + "</v></NormText>";
    xmlight::StrictParser p{xml};
    NormText t;
    EXPECT_FALSE(xmlight::deserialize(p, "NormText", t)) << xml;
    EXPECT_EQ(p.errorCode(), want) << xml;
    // Same run through the tokenized mixed-content path.
    const std::string mixed = "<NormText><v>ok<!--c-->" + text + "</v></NormText>";
    xmlight::StrictParser pm{mixed};
    NormText tm;
    EXPECT_FALSE(xmlight::deserialize(pm, "NormText", tm)) << mixed;
    EXPECT_EQ(pm.errorCode(), want) << mixed;
  }
  std::string dense;
  for (int i = 0; i < 20; ++i) {
    dense += "]x";
  }
  const auto ACCEPT = std::to_array<std::string>({
      sixteen + "]",   // ']' as the final byte after a clean chunk
      fifteen + "]]",  // "]]" with no '>' at the very end
      dense,           // ']'-dense long run: one containsCdataEnd resolution
      "a\tb\nc",       // legal whitespace controls
  });
  for (const auto& text : ACCEPT) {
    const std::string xml = "<NormText><v>" + text + "</v></NormText>";
    xmlight::StrictParser p{xml};
    NormText t;
    ASSERT_TRUE(xmlight::deserialize(p, "NormText", t)) << xml;
    EXPECT_EQ(t.v, text) << xml;
  }
}

//
// Library extensions: lifted field ceiling, enums, value fields, recursion.
//

// Enumerations via XmlEnumTraits (string tokens)
enum class Priority : uint8_t { Low, Medium, High };
template<>
struct xmlight::XmlEnumTraits<Priority> {
  static constexpr auto values = xmlight::enumTable<Priority>(
      {{"Low", Priority::Low}, {"Medium", Priority::Medium}, {"High", Priority::High}});
};

struct Task {
  Priority priority{};  // attribute
  Priority level{};     // child element
};
template<>
struct xmlight::XmlMetadata<Task> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("priority", &Task::priority),
                                                 xmlight::field("level", &Task::level));
};

TEST_F(LightningBasicTests, EnumFieldRoundTripAndUnknownToken) {
  {
    const std::string_view src = R"(<Task priority="High"><level>Medium</level></Task>)";
    xmlight::Parser p{src};
    Task t;
    ASSERT_TRUE(xmlight::deserialize(p, "Task", t));
    EXPECT_EQ(t.priority, Priority::High);
    EXPECT_EQ(t.level, Priority::Medium);
    const std::string out = xmlight::serialize<false>("Task", t);
    EXPECT_EQ(out, R"(<Task priority="High"><level>Medium</level></Task>)");
  }
  {
    xmlight::Parser p{std::string_view(R"(<Task priority="High"><level>Wizard</level></Task>)")};
    Task t;
    EXPECT_FALSE(xmlight::deserialize(p, "Task", t));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::InvalidEnumValue);
  }
}

// Value field (XSD simpleContent)
struct Money {
  std::string_view currency;  // attribute
  double amount{};            // element's own text
};
template<>
struct xmlight::XmlMetadata<Money> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("currency", &Money::currency),
                                                 xmlight::valueField(&Money::amount));
};
struct Invoice {
  Money total;
};
template<>
struct xmlight::XmlMetadata<Invoice> {
  static constexpr auto fields = std::make_tuple(xmlight::field("total", &Invoice::total));
};

TEST_F(LightningBasicTests, ValueFieldNumericRoundTrip) {
  const std::string_view src = R"(<Invoice><total currency="USD">9.99</total></Invoice>)";
  xmlight::Parser p{src};
  Invoice inv;
  ASSERT_TRUE(xmlight::deserialize(p, "Invoice", inv));
  EXPECT_EQ(inv.total.currency, "USD");
  EXPECT_DOUBLE_EQ(inv.total.amount, 9.99);
  const std::string out = xmlight::serialize<false>("Invoice", inv);
  EXPECT_EQ(out, R"(<Invoice><total currency="USD">9.99</total></Invoice>)");
}

struct Measure {
  std::string unit;  // attribute
  std::string text;  // required value
};
template<>
struct xmlight::XmlMetadata<Measure> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("unit", &Measure::unit),
                                                 xmlight::valueField(&Measure::text, true));
};
struct MeasureDoc {
  Measure m;
};
template<>
struct xmlight::XmlMetadata<MeasureDoc> {
  static constexpr auto fields = std::make_tuple(xmlight::field("m", &MeasureDoc::m));
};

TEST_F(LightningBasicTests, ValueFieldStringNormalized) {
  const std::string_view src = R"(<MeasureDoc><m unit="kg">a &amp; b</m></MeasureDoc>)";
  xmlight::NormalizingParser p{src};
  MeasureDoc d;
  ASSERT_TRUE(xmlight::deserialize(p, "MeasureDoc", d));
  EXPECT_EQ(d.m.unit, "kg");
  EXPECT_EQ(d.m.text, "a & b");  // entity expanded into the value
}

TEST_F(LightningBasicTests, ValueFieldRequiredEmptyFails) {
  // Self-closing and empty both lack text -> required value field is missing.
  for (const std::string_view src :
       {std::string_view(R"(<MeasureDoc><m unit="kg"/></MeasureDoc>)"),
        std::string_view(R"(<MeasureDoc><m unit="kg"></m></MeasureDoc>)")}) {
    xmlight::Parser p{src};
    MeasureDoc d;
    EXPECT_FALSE(xmlight::deserialize(p, "MeasureDoc", d));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::MissingRequiredField);
  }
}

// Recursion via std::unique_ptr
struct Section {
  std::string_view title;
  std::unique_ptr<Section> sub;  // optional, recursive
};
template<>
struct xmlight::XmlMetadata<Section> {
  static constexpr auto fields = std::make_tuple(xmlight::field("title", &Section::title),
                                                 xmlight::field("sub", &Section::sub));
};

TEST_F(LightningBasicTests, UniquePtrRecursionChainAndAbsentChild) {
  {
    const std::string_view src = R"(<Section><title>A</title><sub><title>B</title>)"
                                 R"(<sub><title>C</title></sub></sub></Section>)";
    xmlight::Parser p{src};
    Section s;
    ASSERT_TRUE(xmlight::deserialize(p, "Section", s));
    EXPECT_EQ(s.title, "A");
    ASSERT_TRUE(s.sub);
    EXPECT_EQ(s.sub->title, "B");
    ASSERT_TRUE(s.sub->sub);
    EXPECT_EQ(s.sub->sub->title, "C");
    EXPECT_FALSE(s.sub->sub->sub);
    EXPECT_EQ(xmlight::serialize<false>("Section", s), src);
  }
  {
    const std::string_view src = R"(<Section><title>solo</title></Section>)";
    xmlight::Parser p{src};
    Section s;
    ASSERT_TRUE(xmlight::deserialize(p, "Section", s));
    EXPECT_EQ(s.title, "solo");
    EXPECT_FALSE(s.sub);
    EXPECT_EQ(xmlight::serialize<false>("Section", s), src);  // no <sub>
  }
}

// More than 64 fields (multiword required mask)
struct Wide72 {
  int a0{};
  int a1{};
  int a2{};
  int a3{};
  int a4{};
  int a5{};
  int a6{};
  int a7{};
  int a8{};
  int a9{};
  int a10{};
  int a11{};
  int a12{};
  int a13{};
  int a14{};
  int a15{};
  int a16{};
  int a17{};
  int a18{};
  int a19{};
  int a20{};
  int a21{};
  int a22{};
  int a23{};
  int a24{};
  int a25{};
  int a26{};
  int a27{};
  int a28{};
  int a29{};
  int a30{};
  int a31{};
  int a32{};
  int a33{};
  int a34{};
  int a35{};
  int a36{};
  int a37{};
  int a38{};
  int a39{};
  int a40{};
  int a41{};
  int a42{};
  int a43{};
  int a44{};
  int a45{};
  int a46{};
  int a47{};
  int a48{};
  int a49{};
  int a50{};
  int a51{};
  int a52{};
  int a53{};
  int a54{};
  int a55{};
  int a56{};
  int a57{};
  int a58{};
  int a59{};
  int a60{};
  int a61{};
  int a62{};
  int a63{};
  int a64{};
  int a65{};
  int a66{};
  int a67{};
  int a68{};
  int a69{};
  int a70{};
  int a71{};
};
template<>
struct xmlight::XmlMetadata<Wide72> {
  static constexpr auto fields = std::make_tuple(
      xmlight::attrField("a0", &Wide72::a0, true), xmlight::attrField("a1", &Wide72::a1),
      xmlight::attrField("a2", &Wide72::a2), xmlight::attrField("a3", &Wide72::a3),
      xmlight::attrField("a4", &Wide72::a4), xmlight::attrField("a5", &Wide72::a5),
      xmlight::attrField("a6", &Wide72::a6), xmlight::attrField("a7", &Wide72::a7),
      xmlight::attrField("a8", &Wide72::a8), xmlight::attrField("a9", &Wide72::a9),
      xmlight::attrField("a10", &Wide72::a10), xmlight::attrField("a11", &Wide72::a11),
      xmlight::attrField("a12", &Wide72::a12), xmlight::attrField("a13", &Wide72::a13),
      xmlight::attrField("a14", &Wide72::a14), xmlight::attrField("a15", &Wide72::a15),
      xmlight::attrField("a16", &Wide72::a16), xmlight::attrField("a17", &Wide72::a17),
      xmlight::attrField("a18", &Wide72::a18), xmlight::attrField("a19", &Wide72::a19),
      xmlight::attrField("a20", &Wide72::a20), xmlight::attrField("a21", &Wide72::a21),
      xmlight::attrField("a22", &Wide72::a22), xmlight::attrField("a23", &Wide72::a23),
      xmlight::attrField("a24", &Wide72::a24), xmlight::attrField("a25", &Wide72::a25),
      xmlight::attrField("a26", &Wide72::a26), xmlight::attrField("a27", &Wide72::a27),
      xmlight::attrField("a28", &Wide72::a28), xmlight::attrField("a29", &Wide72::a29),
      xmlight::attrField("a30", &Wide72::a30), xmlight::attrField("a31", &Wide72::a31),
      xmlight::attrField("a32", &Wide72::a32), xmlight::attrField("a33", &Wide72::a33),
      xmlight::attrField("a34", &Wide72::a34), xmlight::attrField("a35", &Wide72::a35),
      xmlight::attrField("a36", &Wide72::a36), xmlight::attrField("a37", &Wide72::a37),
      xmlight::attrField("a38", &Wide72::a38), xmlight::attrField("a39", &Wide72::a39),
      xmlight::attrField("a40", &Wide72::a40), xmlight::attrField("a41", &Wide72::a41),
      xmlight::attrField("a42", &Wide72::a42), xmlight::attrField("a43", &Wide72::a43),
      xmlight::attrField("a44", &Wide72::a44), xmlight::attrField("a45", &Wide72::a45),
      xmlight::attrField("a46", &Wide72::a46), xmlight::attrField("a47", &Wide72::a47),
      xmlight::attrField("a48", &Wide72::a48), xmlight::attrField("a49", &Wide72::a49),
      xmlight::attrField("a50", &Wide72::a50), xmlight::attrField("a51", &Wide72::a51),
      xmlight::attrField("a52", &Wide72::a52), xmlight::attrField("a53", &Wide72::a53),
      xmlight::attrField("a54", &Wide72::a54), xmlight::attrField("a55", &Wide72::a55),
      xmlight::attrField("a56", &Wide72::a56), xmlight::attrField("a57", &Wide72::a57),
      xmlight::attrField("a58", &Wide72::a58), xmlight::attrField("a59", &Wide72::a59),
      xmlight::attrField("a60", &Wide72::a60), xmlight::attrField("a61", &Wide72::a61),
      xmlight::attrField("a62", &Wide72::a62), xmlight::attrField("a63", &Wide72::a63),
      xmlight::attrField("a64", &Wide72::a64, true), xmlight::attrField("a65", &Wide72::a65),
      xmlight::attrField("a66", &Wide72::a66), xmlight::attrField("a67", &Wide72::a67),
      xmlight::attrField("a68", &Wide72::a68), xmlight::attrField("a69", &Wide72::a69),
      xmlight::attrField("a70", &Wide72::a70), xmlight::attrField("a71", &Wide72::a71, true));
};

static auto wideXml(int omit) -> std::string {
  std::string s = "<Wide72";
  for (int i = 0; i < 72; ++i) {
    if (i == omit) {
      continue;
    }
    s += " a" + std::to_string(i) + "=\"" + std::to_string(i) + "\"";
  }
  s += "/>";
  return s;
}

TEST_F(LightningBasicTests, MoreThan64FieldsMultiwordMask) {
  {
    const std::string src = wideXml(-1);
    xmlight::Parser p{src};
    Wide72 w;
    ASSERT_TRUE(xmlight::deserialize(p, "Wide72", w));
    EXPECT_EQ(w.a0, 0);
    EXPECT_EQ(w.a64, 64);
    EXPECT_EQ(w.a71, 71);
  }
  {
    // a64 is required and lives in the second mask word; dropping it fails.
    const std::string src = wideXml(64);
    xmlight::Parser p{src};
    Wide72 w;
    EXPECT_FALSE(xmlight::deserialize(p, "Wide72", w));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::MissingRequiredField);
  }
}

// Date/time value types and variant (xs:choice) fields.
struct Event {
  xmlight::Date day;        // attribute
  xmlight::DateTime stamp;  // element
  xmlight::Time at;         // element
};
template<>
struct xmlight::XmlMetadata<Event> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("day", &Event::day),
                      xmlight::field("stamp", &Event::stamp), xmlight::field("at", &Event::at));
};

TEST_F(LightningBasicTests, DateTimeRoundTripAndChrono) {
  const std::string_view src = R"(<Event day="2026-06-18"><stamp>2026-06-18T09:30:00Z</stamp>)"
                               R"(<at>23:59:59.5+02:00</at></Event>)";
  xmlight::Parser p{src};
  Event e;
  ASSERT_TRUE(xmlight::deserialize(p, "Event", e));
  EXPECT_EQ(e.day, (xmlight::Date{2026, 6, 18}));
  EXPECT_EQ(e.stamp.time.hour, 9U);
  EXPECT_TRUE(e.stamp.time.tz.has_value());
  EXPECT_EQ(e.at.nanosecond, 500000000U);
  EXPECT_EQ(e.at.tz, std::chrono::minutes{120});
  // chrono accessors
  EXPECT_EQ(e.day.toSysDays(), std::chrono::sys_days{std::chrono::year{2026} / 6 / 18});
  EXPECT_EQ(e.stamp.toSysTime(),  // UTC instant
            std::chrono::sys_days{std::chrono::year{2026} / 6 / 18} + std::chrono::hours{9} +
                std::chrono::minutes{30});
  EXPECT_EQ(xmlight::serialize<false>("Event", e), src);
}

TEST_F(LightningBasicTests, DateTimeInvalidValue) {
  // Month 13 in an element is a hard parse failure.
  const std::string_view src = R"(<Event day="2026-06-18"><stamp>2026-13-01T00:00:00</stamp>)"
                               R"(<at>00:00:00</at></Event>)";
  xmlight::Parser p{src};
  Event e;
  EXPECT_FALSE(xmlight::deserialize(p, "Event", e));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::InvalidValue);
}

// Variant (xs:choice)
struct VCircle {
  int r{};
};
template<>
struct xmlight::XmlMetadata<VCircle> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("r", &VCircle::r));
};
struct VSquare {
  int s{};
};
template<>
struct xmlight::XmlMetadata<VSquare> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("s", &VSquare::s));
};

struct Shape {
  std::variant<VCircle, VSquare> kind;
};
template<>
struct xmlight::XmlMetadata<Shape> {
  static constexpr auto fields = std::make_tuple(xmlight::requiredVariantField(
      &Shape::kind, xmlight::alt<VCircle>("circle"), xmlight::alt<VSquare>("square")));
};

TEST_F(LightningBasicTests, VariantSingleBothBranches) {
  {
    const std::string_view src = R"(<Shape><square s="7"/></Shape>)";
    xmlight::Parser p{src};
    Shape sh;
    ASSERT_TRUE(xmlight::deserialize(p, "Shape", sh));
    ASSERT_EQ(sh.kind.index(), 1U);
    EXPECT_EQ(std::get<VSquare>(sh.kind).s, 7);
    EXPECT_EQ(xmlight::serialize<false>("Shape", sh), src);
  }
  {
    const std::string_view src = R"(<Shape><circle r="3"/></Shape>)";
    xmlight::Parser p{src};
    Shape sh;
    ASSERT_TRUE(xmlight::deserialize(p, "Shape", sh));
    ASSERT_EQ(sh.kind.index(), 0U);
    EXPECT_EQ(std::get<VCircle>(sh.kind).r, 3);
    EXPECT_EQ(xmlight::serialize<false>("Shape", sh), src);
  }
}

/// @brief A required variant with no matching alternative, and an alternative
/// whose value fails to parse, both reject.
TEST_F(LightningBasicTests, VariantRejects) {
  {
    xmlight::Parser p{std::string_view(R"(<Shape><triangle/></Shape>)")};
    Shape sh;
    EXPECT_FALSE(xmlight::deserialize(p, "Shape", sh));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::MissingRequiredField);
  }
  {
    xmlight::Parser p{std::string_view(R"(<Shape><circle r="x"/></Shape>)")};
    Shape sh;
    EXPECT_FALSE(xmlight::deserialize(p, "Shape", sh));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::InvalidNumericValue);
  }
}

struct VDoc {
  std::vector<std::variant<VCircle, VSquare>> body;
};
template<>
struct xmlight::XmlMetadata<VDoc> {
  static constexpr auto fields = std::make_tuple(xmlight::variantField(
      &VDoc::body, xmlight::alt<VCircle>("circle"), xmlight::alt<VSquare>("square")));
};

TEST_F(LightningBasicTests, VariantRepeatedInterleaved) {
  const std::string_view src = R"(<VDoc><circle r="1"/><square s="2"/><circle r="3"/></VDoc>)";
  xmlight::Parser p{src};
  VDoc d;
  ASSERT_TRUE(xmlight::deserialize(p, "VDoc", d));
  ASSERT_EQ(d.body.size(), 3U);
  EXPECT_EQ(d.body[0].index(), 0U);
  EXPECT_EQ(std::get<VCircle>(d.body[0]).r, 1);
  EXPECT_EQ(d.body[1].index(), 1U);
  EXPECT_EQ(std::get<VSquare>(d.body[1]).s, 2);
  EXPECT_EQ(std::get<VCircle>(d.body[2]).r, 3);
  EXPECT_EQ(xmlight::serialize<false>("VDoc", d), src);
}

// std::optional fields (present => engaged, absent => nullopt).
struct OptAddr {
  std::string_view city;
};
template<>
struct xmlight::XmlMetadata<OptAddr> {
  static constexpr auto fields = std::make_tuple(xmlight::field("city", &OptAddr::city));
};

struct OptPerson {
  std::optional<int> age;                // attribute (scalar inner)
  std::optional<std::string_view> nick;  // element (scalar inner)
  std::optional<OptAddr> addr;           // element (object inner)
};
template<>
struct xmlight::XmlMetadata<OptPerson> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("age", &OptPerson::age),
                                                 xmlight::field("nick", &OptPerson::nick),
                                                 xmlight::field("addr", &OptPerson::addr));
};

/// @brief Optionals engage when present and stay nullopt when absent; the
/// serializer emits engaged values and omits absent ones.
TEST_F(LightningBasicTests, OptionalPresenceRoundTrip) {
  {
    const std::string_view src =
        R"(<OptPerson age="30"><nick>Al</nick><addr><city>NYC</city></addr></OptPerson>)";
    xmlight::Parser p{src};
    OptPerson pe;
    ASSERT_TRUE(xmlight::deserialize(p, "OptPerson", pe));
    ASSERT_TRUE(pe.age);
    EXPECT_EQ(*pe.age, 30);
    ASSERT_TRUE(pe.nick);
    EXPECT_EQ(*pe.nick, "Al");
    ASSERT_TRUE(pe.addr);
    EXPECT_EQ(pe.addr->city, "NYC");
    EXPECT_EQ(xmlight::serialize<false>("OptPerson", pe), src);
  }
  {
    const std::string_view src = R"(<OptPerson age="5"></OptPerson>)";
    xmlight::Parser p{src};
    OptPerson pe;
    ASSERT_TRUE(xmlight::deserialize(p, "OptPerson", pe));
    ASSERT_TRUE(pe.age);
    EXPECT_EQ(*pe.age, 5);
    EXPECT_FALSE(pe.nick);
    EXPECT_FALSE(pe.addr);
    EXPECT_EQ(xmlight::serialize<false>("OptPerson", pe), src);  // no <nick>/<addr>
  }
}

// xs:list fields (whitespace-separated values in one element / attribute).
enum class Grade : uint8_t { A, B, C };
template<>
struct xmlight::XmlEnumTraits<Grade> {
  static constexpr auto values =
      xmlight::enumTable<Grade>({{"A", Grade::A}, {"B", Grade::B}, {"C", Grade::C}});
};

struct ListRec {
  std::vector<std::string> tags;  // attribute list
  std::vector<int> dims;          // element list
  std::array<int, 3> fixed{};     // fixed element list (overflow skipped)
  std::vector<Grade> grades;      // enum element list
};
template<>
struct xmlight::XmlMetadata<ListRec> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("tags", &ListRec::tags),
                                                 xmlight::listField("dims", &ListRec::dims),
                                                 xmlight::listField("fixed", &ListRec::fixed),
                                                 xmlight::listField("grades", &ListRec::grades));
};

TEST_F(LightningBasicTests, ListElementsAndAttributesRoundTrip) {
  const std::string_view src =
      R"(<ListRec tags="a b c"><dims>1 2 3 4</dims><fixed>10 20 30 40</fixed>)"
      R"(<grades>A C B</grades></ListRec>)";
  xmlight::Parser p{src};
  ListRec r;
  ASSERT_TRUE(xmlight::deserialize(p, "ListRec", r));
  EXPECT_EQ(r.tags, (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_EQ(r.dims, (std::vector<int>{1, 2, 3, 4}));
  EXPECT_EQ(r.fixed[0], 10);
  EXPECT_EQ(r.fixed[2], 30);  // 40 overflow skipped
  EXPECT_EQ(r.grades, (std::vector<Grade>{Grade::A, Grade::C, Grade::B}));
  // Round-trips to canonical (single-space) list form; fixed array drops
  // overflow.
  EXPECT_EQ(xmlight::serialize<false>("ListRec", r),
            R"(<ListRec tags="a b c"><dims>1 2 3 4</dims><fixed>10 20 30</fixed>)"
            R"(<grades>A C B</grades></ListRec>)");
}

/// @brief List whitespace and emptiness forms: empty/self-closing elements,
/// extra separator whitespace, and reference expansion in list values under
/// the normalizing parser.
TEST_F(LightningBasicTests, ListWhitespaceAndEmptyForms) {
  {
    xmlight::Parser p{std::string_view(R"(<ListRec tags=""><dims/><fixed></fixed></ListRec>)")};
    ListRec r;
    ASSERT_TRUE(xmlight::deserialize(p, "ListRec", r));
    EXPECT_TRUE(r.tags.empty());
    EXPECT_TRUE(r.dims.empty());
    EXPECT_TRUE(r.grades.empty());
  }
  {
    const std::string_view src =
        R"(<ListRec><dims>  1   2	3
      4 </dims></ListRec>)";
    xmlight::Parser p{src};
    ListRec r;
    ASSERT_TRUE(xmlight::deserialize(p, "ListRec", r));
    EXPECT_EQ(r.dims, (std::vector<int>{1, 2, 3, 4}));
  }
  {
    xmlight::NormalizingParser p{R"(<ListRec tags="a&amp;b c"><dims>1 2</dims></ListRec>)"};
    ListRec r;
    ASSERT_TRUE(xmlight::deserialize(p, "ListRec", r));
    EXPECT_EQ(r.tags, (std::vector<std::string>{"a&b", "c"}));
    EXPECT_EQ(r.dims, (std::vector<int>{1, 2}));
  }
}

/// @brief Invalid list content fails: a bad numeric token, truncated input
/// inside a list element, and bad references in attribute and element lists
/// under the normalizing parser.
TEST_F(LightningBasicTests, ListInvalidContentFails) {
  {
    xmlight::Parser p{std::string_view(R"(<ListRec><dims>1 x 3</dims></ListRec>)")};
    ListRec r;
    EXPECT_FALSE(xmlight::deserialize(p, "ListRec", r));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::InvalidNumericValue);
  }
  {
    xmlight::Parser p{R"(<ListRec><dims>1)"};
    ListRec r;
    EXPECT_FALSE(xmlight::deserialize(p, "ListRec", r));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnexpectedEof);
  }
  {
    xmlight::NormalizingParser p{std::string_view(R"(<ListRec tags="a &bogus; b"/>)")};
    ListRec r;
    EXPECT_FALSE(xmlight::deserialize(p, "ListRec", r));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UndefinedEntity);
  }
  {
    xmlight::NormalizingParser p{R"(<ListRec><dims>1&bogus;2</dims></ListRec>)"};
    ListRec r;
    EXPECT_FALSE(xmlight::deserialize(p, "ListRec", r));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UndefinedEntity);
  }
}

// Robustness / XSD-conformance for typed leaf & attribute values

namespace {
struct RobScalars {
  int count{};
  double ratio{};
  bool flag{};
};
struct RobDate {
  xmlight::Date date{};
};
struct RobAttr {
  int n{};
};
struct RobOptAttr {
  std::optional<int> n;
};
}  // namespace

template<>
struct xmlight::XmlMetadata<RobScalars> {
  static constexpr auto fields = std::make_tuple(xmlight::field("count", &RobScalars::count),
                                                 xmlight::field("ratio", &RobScalars::ratio),
                                                 xmlight::field("flag", &RobScalars::flag));
};
template<>
struct xmlight::XmlMetadata<RobDate> {
  static constexpr auto fields = std::make_tuple(xmlight::field("date", &RobDate::date));
};
template<>
struct xmlight::XmlMetadata<RobAttr> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("n", &RobAttr::n));
};
template<>
struct xmlight::XmlMetadata<RobOptAttr> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("n", &RobOptAttr::n));
};

template<typename P>
static void expectCount(std::string_view xml, int want) {
  P p{xml};
  RobScalars o{};
  ASSERT_TRUE(xmlight::deserialize(p, "RobScalars", o)) << "ec=" << static_cast<int>(p.errorCode());
  EXPECT_EQ(o.count, want);
}
template<typename P>
static void expectCountError(std::string_view xml, xmlight::ErrorCode want) {
  P p{xml};
  RobScalars o{};
  EXPECT_FALSE(xmlight::deserialize(p, "RobScalars", o));
  EXPECT_EQ(p.errorCode(), want);
}

TEST_F(LightningBasicTests, RobustNumericWhitespace) {
  for (const std::string_view xml :
       {std::string_view("<RobScalars><count> 42 </count></RobScalars>"),
        std::string_view("<RobScalars><count>\n    42\n  </count></RobScalars>")}) {
    expectCount<xmlight::Parser>(xml, 42);
    expectCount<xmlight::NormalizingParser>(xml, 42);
    expectCount<xmlight::StrictParser>(xml, 42);
  }
}

TEST_F(LightningBasicTests, RobustLeadingPlus) {
  expectCount<xmlight::Parser>("<RobScalars><count>+42</count></RobScalars>", 42);
  xmlight::Parser p{"<RobScalars><ratio>+3.5</ratio></RobScalars>"};
  RobScalars o{};
  ASSERT_TRUE(xmlight::deserialize(p, "RobScalars", o));
  EXPECT_DOUBLE_EQ(o.ratio, 3.5);
}

/// @brief Typed leaves read whole values across comment/PI splits and out of
/// CDATA sections.
TEST_F(LightningBasicTests, RobustSegmentedNumericText) {
  expectCount<xmlight::Parser>("<RobScalars><count>4<!--x-->2</count></RobScalars>", 42);
  expectCount<xmlight::NormalizingParser>("<RobScalars><count>4<!--x-->2</count></RobScalars>", 42);
  expectCount<xmlight::StrictParser>("<RobScalars><count>4<!--x-->2</count></RobScalars>", 42);
  expectCount<xmlight::Parser>("<RobScalars><count>1<!--a-->2<!--b-->3</count></RobScalars>", 123);
  expectCount<xmlight::Parser>("<RobScalars><count>1<?pi ?>2</count></RobScalars>", 12);
  expectCount<xmlight::Parser>("<RobScalars><count><![CDATA[42]]></count></RobScalars>", 42);
  expectCount<xmlight::NormalizingParser>("<RobScalars><count><![CDATA[42]]></count></RobScalars>",
                                          42);
}

TEST_F(LightningBasicTests, RobustCharRefInTypedLeaf) {
  const std::string_view xml = "<RobScalars><count>4&#50;</count></RobScalars>";
  expectCount<xmlight::NormalizingParser>(xml, 42);
  expectCount<xmlight::StrictParser>(xml, 42);
  expectCountError<xmlight::Parser>(xml, xmlight::ErrorCode::InvalidNumericValue);
}

TEST_F(LightningBasicTests, RobustBoolAndDateWhitespace) {
  {
    xmlight::Parser p{"<RobScalars><flag> true </flag></RobScalars>"};
    RobScalars o{};
    ASSERT_TRUE(xmlight::deserialize(p, "RobScalars", o));
    EXPECT_TRUE(o.flag);
  }
  {
    xmlight::Parser p{"<RobDate><date> 2026-06-28 </date></RobDate>"};
    RobDate o{};
    ASSERT_TRUE(xmlight::deserialize(p, "RobDate", o));
    EXPECT_EQ(o.date, (xmlight::Date{2026, 6, 28}));
  }
}

/// @brief Typed attributes collapse whitespace; malformed or empty values are
/// hard errors on required attributes but leave optional ones disengaged.
TEST_F(LightningBasicTests, RobustTypedAttributes) {
  {
    xmlight::Parser p{"<RobAttr n=' 42 '/>"};
    RobAttr o{};
    ASSERT_TRUE(xmlight::deserialize(p, "RobAttr", o));
    EXPECT_EQ(o.n, 42);
  }
  for (const std::string_view xml :
       {std::string_view("<RobAttr n='abc'/>"), std::string_view("<RobAttr n=''/>")}) {
    xmlight::Parser p{xml};
    RobAttr o{};
    EXPECT_FALSE(xmlight::deserialize(p, "RobAttr", o)) << xml;
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::InvalidNumericValue) << xml;
  }
  {
    xmlight::Parser p{"<RobOptAttr n=' 42 '/>"};
    RobOptAttr o{};
    ASSERT_TRUE(xmlight::deserialize(p, "RobOptAttr", o));
    ASSERT_TRUE(o.n.has_value());
    EXPECT_EQ(*o.n, 42);
  }
  for (const std::string_view xml :
       {std::string_view("<RobOptAttr n='abc'/>"), std::string_view("<RobOptAttr n=''/>")}) {
    xmlight::Parser p{xml};
    RobOptAttr o{};
    ASSERT_TRUE(xmlight::deserialize(p, "RobOptAttr", o)) << xml;
    EXPECT_FALSE(o.n.has_value());
  }
}

struct ValItem {
  std::string sku;
};
template<>
struct xmlight::XmlMetadata<ValItem> {
  static constexpr auto fields = std::make_tuple(xmlight::field("sku", &ValItem::sku));
};
template<>
struct xmlight::XmlConstraints<ValItem> {
  static auto check(const ValItem& v) -> std::optional<std::string> {
    if (v.sku.size() > 4) {
      return "sku: maxLength violation";
    }
    return {};
  }
};

struct ValOrder {
  int qty{};
  std::vector<ValItem> items;
  std::optional<ValItem> gift;
  std::unique_ptr<ValOrder> parent;
  std::variant<ValItem, int> pick{0};
};
template<>
struct xmlight::XmlMetadata<ValOrder> {
  static constexpr auto fields = std::make_tuple(
      xmlight::attrField("qty", &ValOrder::qty), xmlight::vecField("item", &ValOrder::items),
      xmlight::field("gift", &ValOrder::gift), xmlight::field("parent", &ValOrder::parent),
      xmlight::variantField(&ValOrder::pick, xmlight::alt<ValItem>("pickItem"),
                            xmlight::alt<int>("pickInt")));
};
template<>
struct xmlight::XmlConstraints<ValOrder> {
  static auto check(const ValOrder& v) -> std::optional<std::string> {
    if (v.qty < 0) {
      return "qty: minInclusive violation";
    }
    return {};
  }
};

/// @brief validate() checks a type's own constraints first, then recurses
/// through containers, optionals, unique_ptrs, and variants; a fully valid
/// nested tree passes.
TEST_F(LightningBasicTests, ValidateRecursesNestedConstraints) {
  {
    ValOrder o;
    o.qty = -1;
    o.items.push_back({"TOOLONG"});
    const auto err = xmlight::validate(o);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->message, "qty: minInclusive violation");
  }
  {
    ValOrder o;
    o.items.push_back({"OK"});
    o.items.push_back({"TOOLONG"});
    const auto err = xmlight::validate(o);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->message, "sku: maxLength violation");
  }
  {
    ValOrder o;
    o.gift = ValItem{"TOOLONG"};
    EXPECT_TRUE(xmlight::validate(o).has_value());

    ValOrder p;
    p.parent = std::make_unique<ValOrder>();
    p.parent->qty = -5;
    EXPECT_TRUE(xmlight::validate(p).has_value());
  }
  {
    ValOrder o;
    o.pick = ValItem{"TOOLONG"};
    EXPECT_TRUE(xmlight::validate(o).has_value());
    o.pick = 7;
    EXPECT_FALSE(xmlight::validate(o).has_value());
  }
  {
    ValOrder o;
    o.qty = 3;
    o.items.push_back({"ABCD"});
    o.gift = ValItem{"OK"};
    o.parent = std::make_unique<ValOrder>();
    EXPECT_FALSE(xmlight::validate(o).has_value());
  }
}

// Date/time lexical forms (XSD 3.2.9-3.2.11 reject cases)

TEST_F(LightningBasicTests, DateTimeLexicalRejects) {
  xmlight::Date d{};
  for (const std::string_view bad :
       {"2026-06-1", "2026-0a-01", "202-01-01", "111111111111111111111-01-01", "2026-06",
        "2026-00-01", "2026-06-00", "2026-06-32", "2026-06x18", "99999", "2026-06-18x",
        "2026-06-18Zx", "2026-06-18+5:00", "2026-06-18+15:00", "2026-06-18-"}) {
    EXPECT_FALSE(xmlight::XmlValueTraits<xmlight::Date>::parse(bad, d)) << bad;
  }
  xmlight::Time t{};
  for (const std::string_view bad : {"25:00:00", "24:00:01", "24:01:00", "24:00:00.5", "12:60:00",
                                     "12:00:61", "12:00", "12x00:00", "12:0a:00", "12:00x00",
                                     "12:00:0a", "12:00:00.", "12:00:00+05:60", "12:00:00+0500"}) {
    EXPECT_FALSE(xmlight::XmlValueTraits<xmlight::Time>::parse(bad, t)) << bad;
  }
  // 24:00:00 is the one legal hour-24 form.
  EXPECT_TRUE(xmlight::XmlValueTraits<xmlight::Time>::parse("24:00:00", t));
  xmlight::DateTime dt{};
  for (const std::string_view bad : {"2026-06-18 09:30:00", "2026-06-18T09:30:00+15:00"}) {
    EXPECT_FALSE(xmlight::XmlValueTraits<xmlight::DateTime>::parse(bad, dt)) << bad;
  }
}

/// @brief Lexical edge forms: negative years round-trip with the sign and
/// zero padding, fractions truncate past nanoseconds, negative timezones
/// round-trip, and a dateTime without a timezone converts as UTC.
TEST_F(LightningBasicTests, DateTimeEdgeFormsRoundTrip) {
  {
    xmlight::Date d{};
    ASSERT_TRUE(xmlight::XmlValueTraits<xmlight::Date>::parse("-0044-03-15", d));
    EXPECT_EQ(d, (xmlight::Date{-44, 3, 15}));
    std::string out;
    xmlight::XmlValueTraits<xmlight::Date>::format(out, d);
    EXPECT_EQ(out, "-0044-03-15");
  }
  {
    xmlight::Time t{};
    ASSERT_TRUE(xmlight::XmlValueTraits<xmlight::Time>::parse("12:00:00.1234567890123", t));
    EXPECT_EQ(t.nanosecond, 123456789U);
  }
  {
    xmlight::Time t{};
    ASSERT_TRUE(xmlight::XmlValueTraits<xmlight::Time>::parse("23:30:00-05:30", t));
    EXPECT_EQ(t.tz, std::chrono::minutes{-330});
    std::string out;
    xmlight::XmlValueTraits<xmlight::Time>::format(out, t);
    EXPECT_EQ(out, "23:30:00-05:30");
  }
  {
    xmlight::DateTime dt{};
    ASSERT_TRUE(xmlight::XmlValueTraits<xmlight::DateTime>::parse("2026-06-18T09:30:00", dt));
    EXPECT_FALSE(dt.time.tz.has_value());
    EXPECT_EQ(dt.toSysTime(), std::chrono::sys_days{std::chrono::year{2026} / 6 / 18} +
                                  std::chrono::hours{9} + std::chrono::minutes{30});
  }
}

// Serializer escaping

/// @brief Escaping in attribute values and element text, both short runs
/// (round-trips) and runs long enough to hand off to the memchr scan.
TEST_F(LightningBasicTests, SerializerEscaping) {
  {
    Person person;
    person.name = "AT&T";
    person.age = 1;
    const std::string xml = xmlight::serialize("person", person);
    EXPECT_NE(xml.find("&amp;"), std::string::npos);
    xmlight::Parser p{xml};
    Person out;
    ASSERT_TRUE(xmlight::deserialize(p, "person", out));
    EXPECT_EQ(out.name, "AT&amp;T");
  }
  {
    OwnedUser u;
    u.id = 1;
    u.role = std::string(40, 'a') + '"' + std::string(10, 'b');
    u.name = std::string(40, 'A') + "<mid>" + std::string(40, 'B');
    const std::string out = xmlight::serialize<false>("U", u);
    EXPECT_EQ(out, R"(<U id="1" role=")" + std::string(40, 'a') + "&quot;" + std::string(10, 'b') +
                       R"("><Name>)" + std::string(40, 'A') + "&lt;mid&gt;" + std::string(40, 'B') +
                       "</Name></U>");
  }
}

TEST_F(LightningBasicTests, SerializerLongTagNameRoundTrip) {
  const std::string tag(70, 'T');
  Person person;
  person.name = "n";
  person.age = 1;
  const std::string out = xmlight::serialize<false>(tag, person);
  EXPECT_TRUE(out.starts_with("<" + tag + ">"));
  EXPECT_TRUE(out.ends_with("</" + tag + ">"));
  xmlight::Parser p{out};
  Person back;
  ASSERT_TRUE(xmlight::deserialize(p, tag, back));
  EXPECT_EQ(back.name, "n");
}

/// @brief A value field with no usable text fails for numeric targets:
/// self-closing and a lone sign are both invalid lexical space.
TEST_F(LightningBasicTests, ValueFieldInvalidNumericFails) {
  for (const std::string_view src :
       {std::string_view(R"(<Invoice><total currency="USD"/></Invoice>)"),
        std::string_view(R"(<Invoice><total currency="USD">+</total></Invoice>)")}) {
    xmlight::Parser p{src};
    Invoice inv;
    EXPECT_FALSE(xmlight::deserialize(p, "Invoice", inv)) << src;
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::InvalidNumericValue) << src;
  }
}

// skipElement: unknown subtrees with declarations, truncation, or bad markup

/// @brief skipElement failure modes: truncation at every scan state, bad
/// markup, and unknown nesting beyond kMaxDepth (depth parity with the
/// recursive descent path).
TEST_F(LightningBasicTests, UnknownSubtreeFailureModes) {
  for (const std::string_view src :
       {std::string_view(R"(<person><unknown><a></a)"),   // cut inside an end-tag
        std::string_view(R"(<person><unknown><a b="c)"),  // cut inside a quoted value
        std::string_view(R"(<person><unknown><a b)"),     // cut inside a start-tag
        std::string_view(R"(<person><unknown>x<)")}) {    // cut right after '<'
    xmlight::Parser p{src};
    Person person;
    EXPECT_FALSE(xmlight::deserialize(p, "person", person)) << src;
  }
  constexpr auto EOF_CASES = std::to_array<std::pair<std::string_view, xmlight::ErrorCode>>({
      {R"(<person><unknown><a><b>deep)", xmlight::ErrorCode::UnexpectedEof},
      // An unterminated quote must not scan past the document end.
      {R"(<person><unknown><a attr="broken></a></unknown><name>X</name></person>)",
       xmlight::ErrorCode::UnexpectedEof},
  });
  for (const auto& [src, code] : EOF_CASES) {
    xmlight::Parser p{src};
    Person person;
    EXPECT_FALSE(xmlight::deserialize(p, "person", person)) << src;
    EXPECT_EQ(p.errorCode(), code) << src;
  }
  {
    xmlight::Parser p{
        std::string_view(R"(<person><unknown><$oops/></unknown><name>n</name></person>)")};
    Person person;
    EXPECT_FALSE(xmlight::deserialize(p, "person", person));
  }
  {
    const int over = xmlight::Parser::MAX_DEPTH + 5;
    std::string xml = "<person><name>A</name><unknown>";
    for (int i = 0; i < over; ++i) {
      xml += "<n>";
    }
    for (int i = 0; i < over; ++i) {
      xml += "</n>";
    }
    xml += "</unknown><age>1</age></person>";
    xmlight::Parser p{xml};
    Person person;
    EXPECT_FALSE(xmlight::deserialize(p, "person", person));
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::DepthExceeded);
  }
}

TEST_F(LightningBasicTests, TruncatedOrMalformedAtHintedElementFails) {
  for (const std::string_view src :
       {std::string_view(R"(<person><name)"),      // too short for the fast-path match
        std::string_view(R"(<person><name/x)"),    // '/' not followed by '>'
        std::string_view(R"(<person><name/)")}) {  // '/' at end of input
    xmlight::Parser p{src};
    Person person;
    EXPECT_FALSE(xmlight::deserialize(p, "person", person)) << src;
  }
}

TEST_F(LightningBasicTests, SerializerOptionalAttributePresentAndAbsent) {
  RobOptAttr o{};
  EXPECT_EQ(xmlight::serialize<false>("RobOptAttr", o), "<RobOptAttr/>");
  o.n = 42;
  EXPECT_EQ(xmlight::serialize<false>("RobOptAttr", o), R"(<RobOptAttr n="42"/>)");
}

// Non-required value field: empty text leaves the member empty and the
// element still parses.
struct NoteVal {
  std::string text;
};
template<>
struct xmlight::XmlMetadata<NoteVal> {
  static constexpr auto fields = std::make_tuple(xmlight::valueField(&NoteVal::text));
};

TEST_F(LightningBasicTests, ValueFieldOptionalEmptyText) {
  xmlight::Parser p{"<note></note>"};
  NoteVal note;
  ASSERT_TRUE(xmlight::deserialize(p, "note", note));
  EXPECT_TRUE(note.text.empty());
}

TEST_F(LightningBasicTests, SerializerUnmappedEnumEmitsEmptyText) {
  // Documented XmlEnumTraits contract: an enumerator with no table entry
  // serializes as empty text.
  ListRec r;
  // The cast is intentionally out of range: an enumerator with no
  // XmlEnumTraits entry is exactly the contract this test exercises.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  r.grades = {static_cast<Grade>(9)};
  EXPECT_EQ(xmlight::serialize<false>("ListRec", r),
            R"(<ListRec tags=""><dims></dims><fixed>0 0 0</fixed><grades></grades></ListRec>)");
}

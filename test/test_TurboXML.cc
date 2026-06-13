#include <gtest/gtest.h>

#include <string_view>
#include <vector>

#include "Helpers.hh"
#include "TurboXML.hh"

class XmlParserTest : public ::testing::Test {};

TEST_F(XmlParserTest, ParsingNested) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Alice</name>
  <age>30</age>
  <address>
    <street>Main St</street>
    <zip>12345</zip>
  </address>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "employee", person));

  xml::Parser correct_parser{xml_src};
  ASSERT_TRUE(xml::deserialize(correct_parser, "person", person));
  EXPECT_EQ(person.name, "Alice");
  EXPECT_EQ(person.age, 30);
  EXPECT_EQ(person.address.street, "Main St");
  EXPECT_EQ(person.address.zip, 12345);
}

TEST_F(XmlParserTest, SkipsUnknownFields) {
  constexpr std::string_view xml_src = R"(
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
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Bob");
  EXPECT_EQ(person.age, 25);
  EXPECT_EQ(person.address.street, "Elm Ave");
  EXPECT_EQ(person.address.zip, 99999);
}

TEST_F(XmlParserTest, MissingFieldsRetainDefaults) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Carol</name>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  std::ignore = xml::deserialize(parser, "person", person);
  EXPECT_EQ(person.name, "Carol");
  EXPECT_EQ(person.age, 0);
}

TEST_F(XmlParserTest, MalformedXmlReturnsFalse) {
  constexpr std::string_view xml_src = R"(<person><name>Dave</name>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

TEST_F(XmlParserTest, EmptyInputReturnsFalse) {
  xml::Parser parser{""};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

TEST_F(XmlParserTest, ParsesFieldsOutOfOrder) {
  constexpr std::string_view xml_src = R"(
<person>
  <age>42</age>
  <address>
    <zip>11111</zip>
    <street>Oak Rd</street>
  </address>
  <name>Eve</name>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Eve");
  EXPECT_EQ(person.age, 42);
  EXPECT_EQ(person.address.street, "Oak Rd");
  EXPECT_EQ(person.address.zip, 11111);
}

TEST_F(XmlParserTest, PullsUserDataAndIgnoresUnknownTags) {
  constexpr std::string_view xml_src = R"(
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
  xml::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 2U);
  EXPECT_EQ(users.items[0].id, 42);
  EXPECT_EQ(users.items[0].name, "Ada Lovelace");
  EXPECT_EQ(users.items[0].email, "ada@example.com");
  EXPECT_EQ(users.items[1].id, 99);
  EXPECT_EQ(users.items[1].name, "Grace Hopper");
  EXPECT_EQ(users.items[1].email, "grace@example.com");
}

TEST_F(XmlParserTest, DeserializesFullOrganizationHierarchy) {
  constexpr std::string_view xml_src = R"(<?xml version="1.0"?>
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

  xml::Parser parser{xml_src};
  Organization org;
  ASSERT_TRUE(xml::deserialize(parser, "Organization", org));
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
TEST_F(XmlParserTest, EmptyStringAttribute) {
  constexpr std::string_view xml_src =
      R"(<Organization id="1" name=""></Organization>)";
  xml::Parser parser{xml_src};
  Organization org;

  ASSERT_TRUE(xml::deserialize(parser, "Organization", org));
  EXPECT_EQ(org.id, 1);
  EXPECT_TRUE(org.name.empty());
}

/// @brief Tests that empty numeric attributes and empty child elements
/// fall back to default values safely without failing the parse.
TEST_F(XmlParserTest, EmptyNumericAttributeAndEmptyElement) {
  // 'id' is an empty attribute. 'Email' is an empty child element.
  constexpr std::string_view xml_src = R"(
<Users>
  <User id="">
    <Name>Ghost</Name>
    <Email></Email>
  </User>
</Users>
)";

  xml::Parser parser{xml_src};
  Users users;

  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 1U);

  // std::from_chars fails on "", so it leaves the default initialized value (0)
  EXPECT_EQ(users.items[0].id, 0);
  EXPECT_EQ(users.items[0].name, "Ghost");
  EXPECT_TRUE(users.items[0].email.empty());
}

/// @brief Tests that malformed numeric data fails gracefully rather than
/// throwing or crashing.
TEST_F(XmlParserTest, MalformedNumericDataFailsGracefully) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>John</name>
  <age>not-a-number</age>
  <address><street>123 Ave</street><zip>NaN</zip></address>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  // Deserialization of primitives returns false if parse_numeric fails.
  // Because pull() relies on read_element returning true for handled fields,
  // failing to parse 'age' causes it to return false, propagating up.
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

/// @brief Tests that self-closing tags with attributes process the attributes
/// and terminate correctly.
TEST_F(XmlParserTest, SelfClosingTagWithAttributes) {
  constexpr std::string_view xml_src =
      R"(<Users><User id="77" Name="Self" Email="none"/></Users>)";
  xml::Parser parser{xml_src};
  Users users;

  // For this to work, User must be able to pull() attributes from a
  // self-closing tag. The current logic in pull() checks for ElementClose
  // immediately.
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 1U);
  EXPECT_EQ(users.items[0].id, 77);
  // Name and Email won't be populated because there are no child elements,
  // but the element itself should be consumed without error.
  EXPECT_TRUE(users.items[0].name.empty());
}

/// @brief Tests mixed content (text nodes alongside child elements).
/// The parser should ignore text nodes that aren't bound to a specific field.
TEST_F(XmlParserTest, MixedContentIsIgnored) {
  constexpr std::string_view xml_src = R"(
<person>
  Some raw text that shouldn't break the parser.
  <name>Mixer</name>
  More random text.
  <age>45</age>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Mixer");
  EXPECT_EQ(person.age, 45);
}

/// @brief Tests that comments and CDATA between fields do not disrupt
/// deserialization.
TEST_F(XmlParserTest, IgnoresInterleavedCommentsAndCData) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Frank</name>
  <![CDATA[ Some raw data that the parser should ignore because it's not in a mapped field ]]>
  <age>50</age>
  </person>
)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Frank");
  EXPECT_EQ(person.age, 50);
}

/// @brief Enforces the zero-allocation rule that entities are NOT expanded.
TEST_F(XmlParserTest, EntitiesRemainUnexpanded) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>AT&amp;T</name>
  <address><street>Me &lt; You</street></address>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));

  // The views should contain the raw entity strings
  EXPECT_EQ(person.name, "AT&amp;T");
  EXPECT_EQ(person.address.street, "Me &lt; You");
}

/// @brief Tests that deeply mismatched closing tags cause an immediate parse
/// failure.
TEST_F(XmlParserTest, MismatchedClosingTagsFailCleanly) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Alice</age> </person>
)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

/// @brief Tests that both empty tags and self-closing tags yield empty string
/// views.
TEST_F(XmlParserTest, EmptyAndSelfClosingStringPrimitives) {
  constexpr std::string_view xml_src = R"(
<Users>
  <User id="1"><Name></Name><Email/></User>
</Users>
)";
  xml::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 1U);
  EXPECT_TRUE(users.items[0].name.empty());
  EXPECT_TRUE(users.items[0].email.empty());
}

/// @brief Tests that a mismatched close tag deep in the hierarchy propagates
/// failure all the way out, not just at the leaf level.
TEST_F(XmlParserTest, DeepMismatchedClosingTagFails) {
  constexpr std::string_view xml_src = R"(
<person>
  <address>
    <street>123 Ave</zip>
  </address>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

/// @brief Tests that a close tag with no name (</>) is rejected as malformed.
TEST_F(XmlParserTest, EmptyClosingTagNameFails) {
  constexpr std::string_view xml_src = R"(<person></>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

/// @brief Tests that a stray close tag appearing before the root open tag
/// causes begin_element to fail immediately.
TEST_F(XmlParserTest, StrayCloseTagBeforeRootFails) {
  constexpr std::string_view xml_src = R"(</person><person></person>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

/// @brief Tests that a non-xml processing instruction before the root element
/// is skipped and the document parses correctly.
TEST_F(XmlParserTest, NonXmlProcessingInstructionIsSkipped) {
  constexpr std::string_view xml_src =
      R"(<?xml-stylesheet type="text/xsl" href="style.xsl"?><person><name>Pat</name></person>)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Pat");
}

/// @brief Tests that when a comment splits a text node inside a primitive
/// element, only the last text segment is captured (last-write-wins).
/// This is a known limitation of the zero-copy design: value() overwrites
/// 'out' on each Text token, so a comment between two text runs discards
/// the first run.
TEST_F(XmlParserTest, CommentSplitsTextNodeLastSegmentWins) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Al<!--comment-->ice</name>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  // "Al" is overwritten when "ice" arrives as a second Text token.
  EXPECT_EQ(person.name, "ice");
}

/// @brief Tests that an unexpected open element inside a string primitive
/// field is silently consumed and only the trailing text is captured.
/// This is a known limitation: value() has no mechanism to reject child
/// elements and will resume reading after the inner element is consumed.
TEST_F(XmlParserTest, OpenElementInsideStringPrimitiveIsConsumed) {
  constexpr std::string_view xml_src = R"(
<person>
  <name><unexpected/>hello</name>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  // Text before the inner element ("") is overwritten by text after it.
  EXPECT_EQ(person.name, "hello");
}

/// @brief Tests that a numeric field containing only whitespace fails to
/// parse, because std::from_chars does not accept leading whitespace.
TEST_F(XmlParserTest, WhitespaceOnlyNumericFieldFails) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Test</name>
  <age>   </age>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

/// @brief Tests that a numeric field with whitespace padding around a valid
/// number fails to parse, because std::from_chars does not accept leading
/// whitespace or trailing characters after the number.
TEST_F(XmlParserTest, WhitespacePaddedNumericFieldFails) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Test</name>
  <age> 30 </age>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

TEST_F(XmlParserTest, ParserCanBeResetAndReused) {
  constexpr std::string_view xml_src =
      R"(<person><name>Alice</name><age>30</age></person>)";

  xml::Parser parser{xml_src};

  Person first;
  ASSERT_TRUE(xml::deserialize(parser, "person", first));
  EXPECT_EQ(first.name, "Alice");

  parser.reset();

  Person second;
  ASSERT_TRUE(xml::deserialize(parser, "person", second));
  EXPECT_EQ(second.name, "Alice");
  EXPECT_EQ(second.age, 30);
}

TEST_F(XmlParserTest, PrimitiveFastPathTruncatedCloseTag) {
  // Truncated close tag: `</name` with no `>`.
  std::string xml = "<person><name>Alice</name";
  xml::Parser parser{xml};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

TEST_F(XmlParserTest, IgnoresTrailingDocumentContent) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Alice</name>
</person>
<junk>should not matter</junk>
)";

  xml::Parser parser{xml_src};

  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));

  EXPECT_EQ(person.name, "Alice");
}

TEST_F(XmlParserTest, SkipsDeepUnknownSubtree) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Alice</name>
  <unknown>
    <a>
      <b>
        <c>
          <d>ignored</d>
        </c>
      </b>
    </a>
  </unknown>
  <age>30</age>
</person>
)";

  xml::Parser parser{xml_src};

  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));

  EXPECT_EQ(person.name, "Alice");
  EXPECT_EQ(person.age, 30);
}

TEST_F(XmlParserTest, SingleQuotedAttributes) {
  constexpr std::string_view xml_src =
      R"(<Users><User id='123'></User></Users>)";

  xml::Parser parser{xml_src};

  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));

  ASSERT_EQ(users.items.size(), 1U);
  EXPECT_EQ(users.items[0].id, 123);
}

TEST_F(XmlParserTest, MissingAttributeQuoteFails) {
  constexpr std::string_view xml_src = R"(<Users><User id=123></User></Users>)";

  xml::Parser parser{xml_src};

  Users users;
  EXPECT_FALSE(xml::deserialize(parser, "Users", users));
}

TEST_F(XmlParserTest, UnterminatedAttributeFails) {
  constexpr std::string_view xml_src =
      R"(<Users><User id="123></User></Users>)";

  xml::Parser parser{xml_src};

  Users users;
  EXPECT_FALSE(xml::deserialize(parser, "Users", users));
}

TEST_F(XmlParserTest, UnterminatedCommentFails) {
  constexpr std::string_view xml_src = R"(
<person>
  <!-- broken
  <name>Alice</name>
</person>
)";

  xml::Parser parser{xml_src};

  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

TEST_F(XmlParserTest, UnterminatedCDataFails) {
  constexpr std::string_view xml_src = R"(
<person>
  <![CDATA[broken
</person>
)";

  xml::Parser parser{xml_src};

  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

TEST_F(XmlParserTest, UnterminatedProcessingInstructionFails) {
  constexpr std::string_view xml_src =
      R"(<?xml-stylesheet type="text/xsl"<person></person>)";

  xml::Parser parser{xml_src};

  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

TEST_F(XmlParserTest, NamespacePrefixesAreIgnoredForMatching) {
  constexpr std::string_view xml_src = R"(
<ns:person>
  <ns:name>Alice</ns:name>
  <ns:age>30</ns:age>
</ns:person>
)";

  xml::Parser parser{xml_src};

  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));

  EXPECT_EQ(person.name, "Alice");
  EXPECT_EQ(person.age, 30);
}

TEST_F(XmlParserTest, NamespacedAttributes) {
  constexpr std::string_view xml_src =
      R"(<Users><User ns:id="55"></User></Users>)";

  xml::Parser parser{xml_src};

  Users users;

  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 1U);

  EXPECT_EQ(users.items[0].id, 55);
}

TEST_F(XmlParserTest, LargeVectorOfUsers) {
  std::string xml = "<Users>";

  for (int i = 0; i < 1000; ++i) {
    xml += "<User id=\"" + std::to_string(i) + "\">";
    xml += "<Name>User</Name>";
    xml += "</User>";
  }

  xml += "</Users>";

  xml::Parser parser{xml};

  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));

  ASSERT_EQ(users.items.size(), 1000U);
  EXPECT_EQ(users.items.front().id, 0);
  EXPECT_EQ(users.items.back().id, 999);
}

TEST_F(XmlParserTest, StringViewsReferenceOriginalBuffer) {
  std::string xml = "<person><name>Alice</name></person>";

  xml::Parser parser{xml};

  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));

  const char* begin = xml.data();
  const char* end = begin + xml.size();

  EXPECT_GE(person.name.data(), begin);
  EXPECT_LT(person.name.data(), end);
}

TEST_F(XmlParserTest, DuplicateFieldLastValueWins) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>First</name>
  <name>Second</name>
</person>
)";

  xml::Parser parser{xml_src};

  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));

  EXPECT_EQ(person.name, "Second");
}

/// @brief XML tags are strictly case-sensitive. Verify that mismatched casing
/// is ignored.
TEST_F(XmlParserTest, CaseSensitivity) {
  constexpr std::string_view xml_src = R"(
<person>
  <NAME>Alice</NAME>
  <Age>30</Age>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));

  // Should remain default/empty because the struct maps to "name" and "age",
  // not "NAME" and "Age"
  EXPECT_TRUE(person.name.empty());
  EXPECT_EQ(person.age, 0);
}

/// @brief Because it's a zero-copy parser leveraging string_views, standard
/// string text nodes should preserve exact whitespace (including newlines).
TEST_F(XmlParserTest, PreservesWhitespaceInStrings) {
  constexpr std::string_view xml_src = R"(<person><name>
    Spaced Out
  </name></person>)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "\n    Spaced Out\n  ");
}

/// @brief XML standard states duplicate attributes are illegal.
/// This tests the parser's deterministic fallback (taking the first match).
TEST_F(XmlParserTest, DuplicateAttributesTakesFirst) {
  constexpr std::string_view xml_src =
      R"(<Users><User id="1" id="2"><Name>Bob</Name></User></Users>)";
  xml::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 1U);
  EXPECT_EQ(users.items[0].id, 1);
}

/// @brief Tests that if the root element does not match the requested object
/// name, the parser fails gracefully right away.
TEST_F(XmlParserTest, RootTagMismatchFails) {
  constexpr std::string_view xml_src = R"(<alien><name>Zorg</name></alien>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

/// @brief Verifies that attributes located directly on the root node being
/// deserialized are parsed and populated correctly.
TEST_F(XmlParserTest, ParsesAttributesOnRootElement) {
  constexpr std::string_view xml_src =
      R"(<Organization id="999" name="Global Corp"></Organization>)";
  xml::Parser parser{xml_src};
  Organization org;
  ASSERT_TRUE(xml::deserialize(parser, "Organization", org));
  EXPECT_EQ(org.id, 999);
  EXPECT_EQ(org.name, "Global Corp");
}

/// @brief Verifies deeply nested hierarchical deserialization. Tests parser's
/// recursion limit / stack handling implicitly using the Helpers.hh DeepList.
TEST_F(XmlParserTest, DeepNestingDeserialization) {
  constexpr std::string_view xml_src = R"(
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
  xml::Parser parser{xml_src};
  DeepList list;
  ASSERT_TRUE(xml::deserialize(parser, "DeepList", list));
  ASSERT_EQ(list.items.size(), 1U);
  EXPECT_EQ(list.items[0].next.next.next.next.value, 42);
}

/// @brief Mixes standard tags and self-closing tags within the same vector
/// to ensure the token state machine resets cleanly between iterations.
TEST_F(XmlParserTest, MixedSelfClosingAndStandardTagsInVector) {
  constexpr std::string_view xml_src = R"(
<Users>
  <User id="10"><Name>Standard</Name></User>
  <User id="20"/>
  <User id="30"><Name>Standard Again</Name></User>
</Users>
)";
  xml::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 3U);
  EXPECT_EQ(users.items[0].id, 10);
  EXPECT_EQ(users.items[1].id, 20);
  EXPECT_TRUE(users.items[1].name.empty());
  EXPECT_EQ(users.items[2].id, 30);
}

/// @brief Tag names cannot start with a number according to XML specs.
TEST_F(XmlParserTest, InvalidTagNamesFailCleanly) {
  constexpr std::string_view xml_src =
      R"(<person><123name>Bob</123name></person>)";
  xml::Parser parser{xml_src};
  Person person;
  // Should fail cleanly inside `parse_element_open` since '1' is not
  // `is_name_start`
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

/// @brief Tests malformed tags holding garbage characters where attributes are
/// expected.
TEST_F(XmlParserTest, MalformedTagGarbageFails) {
  constexpr std::string_view xml_src =
      R"(<person !@#$gar> <name>Alice</name> </person>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
}

// Helper to generate perfectly nested XML
auto generate_nested_xml(const size_t depth) -> std::string {
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

/// @brief Tests that the parser successfully evaluates exactly kMaxDepth
TEST_F(XmlParserTest, MaxDepthBoundarySucceeds) {
  std::string xml_src = generate_nested_xml(xml::Parser::kMaxDepth);

  xml::Parser parser{xml_src};
  TreeNode root;

  // Should successfully parse exactly up to the limit
  ASSERT_TRUE(xml::deserialize(parser, "Node", root));
  ASSERT_EQ(root.children.size(), 1U);
  EXPECT_EQ(root.children[0].children.size(), 1U);
}

/// @brief Tests that the parser cleanly aborts when depth hits kMaxDepth + 1
TEST_F(XmlParserTest, MaxDepthExceededFailsCleanly) {
  std::string xml_src = generate_nested_xml(xml::Parser::kMaxDepth + 1);

  xml::Parser parser{xml_src};
  TreeNode root;
  EXPECT_FALSE(xml::deserialize(parser, "Node", root));
}

// ---- try_begin_element fast-path coverage ----

/// @brief Compact deep XML with zero whitespace between tags.
/// Every open tag hits try_begin_element directly.
TEST_F(XmlParserTest, DeepNestingNoWhitespace) {
  constexpr std::string_view xml_src =
      R"(<DeepList><L1><L2><L3><L4><L5><v>99</v></L5></L4></L3></L2></L1></DeepList>)";
  xml::Parser parser{xml_src};
  DeepList list;
  ASSERT_TRUE(xml::deserialize(parser, "DeepList", list));
  ASSERT_EQ(list.items.size(), 1U);
  EXPECT_EQ(list.items[0].next.next.next.next.value, 99);
}

/// @brief Multiple deep elements back-to-back without whitespace.
TEST_F(XmlParserTest, DeepNestingMultipleNoWhitespace) {
  std::string xml = "<DeepList>";
  for (int i = 0; i < 5; ++i) {
    xml += "<L1><L2><L3><L4><L5><v>" + std::to_string(i) +
           "</v></L5></L4></L3></L2></L1>";
  }
  xml += "</DeepList>";

  xml::Parser parser{xml};
  DeepList list;
  ASSERT_TRUE(xml::deserialize(parser, "DeepList", list));
  ASSERT_EQ(list.items.size(), 5U);
  for (size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(list.items[i].next.next.next.next.value, i);
  }
}

/// @brief Namespace-prefixed deep nesting causes try_begin_element to fail
/// and fall through to normal tokenisation. Structure must still parse.
TEST_F(XmlParserTest, DeepNestingWithNamespaceFallthrough) {
  constexpr std::string_view xml_src = R"(
<DeepList>
  <ns:L1><ns:L2><ns:L3><ns:L4><ns:L5>
    <ns:v>42</ns:v>
  </ns:L5></ns:L4></ns:L3></ns:L2></ns:L1>
</DeepList>)";
  xml::Parser parser{xml_src};
  DeepList list;
  ASSERT_TRUE(xml::deserialize(parser, "DeepList", list));
  ASSERT_EQ(list.items.size(), 1U);
  EXPECT_EQ(list.items[0].next.next.next.next.value, 42);
}

/// @brief Tags with attributes on an N==1 type cause try_begin_element
/// to fail (char after name is ' ', not '>'), falling through to normal
/// tokenisation. TreeNode has no AttrField so attributes are silently ignored.
TEST_F(XmlParserTest, TreeNodeWithAttributedChildrenFallthrough) {
  constexpr std::string_view xml_src =
      R"(<Node><Node id="1"><Node/></Node><Node id="2"/></Node>)";
  xml::Parser parser{xml_src};
  TreeNode root;
  ASSERT_TRUE(xml::deserialize(parser, "Node", root));
  ASSERT_EQ(root.children.size(), 2U);
  ASSERT_EQ(root.children[0].children.size(), 1U);
  EXPECT_TRUE(root.children[1].children.empty());
}

/// @brief Self-closing tags inside a tree hit the try_begin_element
/// self-closing path (<Node/> matched as name + "/>").
TEST_F(XmlParserTest, SelfClosingViaFastPath) {
  constexpr std::string_view xml_src =
      R"(<Node><Node/><Node><Node/></Node></Node>)";
  xml::Parser parser{xml_src};
  TreeNode root;
  ASSERT_TRUE(xml::deserialize(parser, "Node", root));
  ASSERT_EQ(root.children.size(), 2U);
  EXPECT_TRUE(root.children[0].children.empty());
  ASSERT_EQ(root.children[1].children.size(), 1U);
  EXPECT_TRUE(root.children[1].children[0].children.empty());
}

/// @brief Unknown sibling elements at various nesting levels must be skipped
/// even when the fast path fires for known elements.
TEST_F(XmlParserTest, DeepNestingWithUnknownSiblings) {
  constexpr std::string_view xml_src = R"(
<DeepList>
  <unknown>stuff</unknown>
  <L1>
    <unknown2/>
    <L2><L3><L4><L5><v>1</v></L5></L4></L3></L2>
  </L1>
  <L1><L2><L3><L4><L5><v>2</v></L5></L4></L3></L2></L1>
</DeepList>)";
  xml::Parser parser{xml_src};
  DeepList list;
  ASSERT_TRUE(xml::deserialize(parser, "DeepList", list));
  ASSERT_EQ(list.items.size(), 2U);
  EXPECT_EQ(list.items[0].next.next.next.next.value, 1);
  EXPECT_EQ(list.items[1].next.next.next.next.value, 2);
}

// ---- AttrField / element-name collision ----

/// @brief A child element whose name matches an AttrField hash must be
/// skipped without disrupting the parse. This exercises the FieldKind::Attr
/// branch in handle_element / inline dispatch.
TEST_F(XmlParserTest, ElementNameCollidesWithAttrField) {
  constexpr std::string_view xml_src = R"(
<Users>
  <User id="42">
    <id>999</id>
    <Name>Collider</Name>
    <Email>c@c.com</Email>
  </User>
</Users>)";
  xml::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 1U);
  EXPECT_EQ(users.items[0].id, 42);
  EXPECT_EQ(users.items[0].name, "Collider");
  EXPECT_EQ(users.items[0].email, "c@c.com");
}

// ---- Container edge cases ----

/// @brief Empty vector container: root element with no matching children.
TEST_F(XmlParserTest, EmptyVectorContainer) {
  constexpr std::string_view xml_src = R"(<Users></Users>)";
  xml::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  EXPECT_TRUE(users.items.empty());
}

/// @brief Self-closing root that holds a vector field.
TEST_F(XmlParserTest, SelfClosingVectorRoot) {
  constexpr std::string_view xml_src = R"(<Users/>)";
  xml::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  EXPECT_TRUE(users.items.empty());
}

/// @brief All self-closing elements inside a vector.
TEST_F(XmlParserTest, ConsecutiveSelfClosingInVector) {
  constexpr std::string_view xml_src = R"(
<Users>
  <User id="1"/><User id="2"/><User id="3"/><User id="4"/><User id="5"/>
</Users>)";
  xml::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 5U);
  for (size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(users.items[i].id, i + 1);
    EXPECT_TRUE(users.items[i].name.empty());
  }
}

// ---- Vec of primitives ----

/// @brief Empty Skills (vec of string_view primitives).
TEST_F(XmlParserTest, VecOfPrimitivesEmpty) {
  constexpr std::string_view xml_src = R"(<Skills></Skills>)";
  xml::Parser parser{xml_src};
  Skills skills;
  ASSERT_TRUE(xml::deserialize(parser, "Skills", skills));
  EXPECT_TRUE(skills.items.empty());
}

/// @brief Multiple primitives in a vector.
TEST_F(XmlParserTest, VecOfPrimitivesMultiple) {
  constexpr std::string_view xml_src = R"(
<Skills>
  <Skill>C++</Skill>
  <Skill>Rust</Skill>
  <Skill>Go</Skill>
  <Skill>Python</Skill>
</Skills>)";
  xml::Parser parser{xml_src};
  Skills skills;
  ASSERT_TRUE(xml::deserialize(parser, "Skills", skills));
  ASSERT_EQ(skills.items.size(), 4U);
  EXPECT_EQ(skills.items[0], "C++");
  EXPECT_EQ(skills.items[1], "Rust");
  EXPECT_EQ(skills.items[2], "Go");
  EXPECT_EQ(skills.items[3], "Python");
}

/// @brief Self-closing primitive element yields empty string_view.
TEST_F(XmlParserTest, VecOfPrimitivesSelfClosing) {
  constexpr std::string_view xml_src =
      R"(<Skills><Skill>A</Skill><Skill/><Skill>B</Skill></Skills>)";
  xml::Parser parser{xml_src};
  Skills skills;
  ASSERT_TRUE(xml::deserialize(parser, "Skills", skills));
  ASSERT_EQ(skills.items.size(), 3U);
  EXPECT_EQ(skills.items[0], "A");
  EXPECT_TRUE(skills.items[1].empty());
  EXPECT_EQ(skills.items[2], "B");
}

// ---- AttrItem / FlatItem coverage ----

/// @brief All 10 attributes populated on AttrItem.
TEST_F(XmlParserTest, FullAttrItemAllAttributesParsed) {
  constexpr std::string_view xml_src = R"(
<AttrList>
  <Item a1="10" a2="20" a3="30" a4="40" a5="50"
        s1="one" s2="two" s3="three" s4="four" s5="five"/>
</AttrList>)";
  xml::Parser parser{xml_src};
  AttrList list;
  ASSERT_TRUE(xml::deserialize(parser, "AttrList", list));
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

/// @brief FlatItem: mixed attrs + child elements.
TEST_F(XmlParserTest, FlatListParsing) {
  constexpr std::string_view xml_src = R"(
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
  xml::Parser parser{xml_src};
  FlatList list;
  ASSERT_TRUE(xml::deserialize(parser, "FlatList", list));
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

// ---- Parser resilience ----

/// @brief Reset after a failed parse must allow a clean retry.
TEST_F(XmlParserTest, ResetAfterFailedParse) {
  std::string xml = R"(<person><name>Alice</name><age>30</age></person>)";
  xml::Parser parser{xml};

  Person p1;
  EXPECT_FALSE(xml::deserialize(parser, "employee", p1));

  parser.reset();

  Person p2;
  ASSERT_TRUE(xml::deserialize(parser, "person", p2));
  EXPECT_EQ(p2.name, "Alice");
  EXPECT_EQ(p2.age, 30);
}

/// @brief Very deeply nested unknown subtree must be fully skipped.
TEST_F(XmlParserTest, SkipsVeryDeeplyNestedUnknownSubtree) {
  std::string xml = "<person><name>Alice</name><unknown>";
  for (int i = 0; i < 50; ++i) {
    xml += "<level" + std::to_string(i) + ">";
  }
  xml += "deep";
  for (int i = 49; i >= 0; --i) {
    xml += "</level" + std::to_string(i) + ">";
  }
  xml += "</unknown><age>25</age></person>";

  xml::Parser parser{xml};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Alice");
  EXPECT_EQ(person.age, 25);
}

/// @brief Multiple processing instructions before root.
TEST_F(XmlParserTest, MultipleProcessingInstructions) {
  constexpr std::string_view xml_src =
      R"(<?xml version="1.0"?><?xml-stylesheet type="text/xsl"?><person><name>Bob</name></person>)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Bob");
}

/// @brief Comments containing dashes and near-miss delimiters must not
/// cause premature termination of the comment scan.
TEST_F(XmlParserTest, CommentWithNearMissDelimiters) {
  constexpr std::string_view xml_src = R"(
<person>
  <!-- tricky - dashes -- but not --> yet - still going -- almost -->
  <name>Survived</name>
  <age>1</age>
</person>)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Survived");
  EXPECT_EQ(person.age, 1);
}

/// @brief Verifies that long comments interleaved with elements are
/// correctly skipped and all element data is parsed. Mirrors the
/// comment-heavy benchmark payload structure.
TEST_F(XmlParserTest, CommentHeavyPayloadParsesCorrectly) {
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

  xml::Parser parser{xml};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 50U);
  for (size_t i = 0; i < 50; ++i) {
    EXPECT_EQ(users.items[i].id, i);
    EXPECT_EQ(users.items[i].name, "User " + std::to_string(i));
    EXPECT_EQ(users.items[i].email, "u" + std::to_string(i) + "@e.com");
  }
}

/// @brief Parse into std::string fields - data must survive after the
/// source buffer is destroyed.
TEST_F(XmlParserTest, StringFieldsMaterializeData) {
  OwnedPerson person;
  {
    // Source buffer scoped - will be destroyed before assertions.
    std::string xml =
        "<person><name>Alice</name><age>30</age>"
        "<email>alice@example.com</email></person>";
    xml::Parser parser{xml};
    ASSERT_TRUE(xml::deserialize(parser, "person", person));
    // xml is destroyed here
  }
  EXPECT_EQ(person.name, "Alice");
  EXPECT_EQ(person.age, 30);
  EXPECT_EQ(person.email, "alice@example.com");
}

/// @brief std::string attributes are materialized, not views.
TEST_F(XmlParserTest, StringAttrsMaterializeData) {
  OwnedUser user;
  {
    std::string xml =
        R"(<OwnedUser id="42" role="admin"><Name>Bob</Name></OwnedUser>)";
    xml::Parser parser{xml};
    ASSERT_TRUE(xml::deserialize(parser, "OwnedUser", user));
  }
  EXPECT_EQ(user.id, 42);
  EXPECT_EQ(user.role, "admin");
  EXPECT_EQ(user.name, "Bob");
}

/// @brief Vector of std::string - each element is an owned copy.
TEST_F(XmlParserTest, VecOfStringMaterializesData) {
  OwnedList list;
  {
    std::string xml =
        "<OwnedList><Tag>Alpha</Tag><Tag>Beta</Tag><Tag>Gamma</Tag></"
        "OwnedList>";
    xml::Parser parser{xml};
    ASSERT_TRUE(xml::deserialize(parser, "OwnedList", list));
  }
  ASSERT_EQ(list.tags.size(), 3U);
  EXPECT_EQ(list.tags[0], "Alpha");
  EXPECT_EQ(list.tags[1], "Beta");
  EXPECT_EQ(list.tags[2], "Gamma");
}

/// @brief Self-closing element yields empty std::string.
TEST_F(XmlParserTest, SelfClosingStringFieldIsEmpty) {
  OwnedPerson person;
  std::string xml = "<person><name/><age>25</age><email/></person>";
  xml::Parser parser{xml};
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_TRUE(person.name.empty());
  EXPECT_EQ(person.age, 25);
  EXPECT_TRUE(person.email.empty());
}

/// @brief Exact fill: element count == array capacity.
TEST_F(XmlParserTest, ArrFieldExactFill) {
  constexpr std::string_view xml_src = R"(
<FixedSkills>
  <Skill>C++</Skill>
  <Skill>Rust</Skill>
  <Skill>Go</Skill>
</FixedSkills>)";
  xml::Parser parser{xml_src};
  FixedSkills skills;
  ASSERT_TRUE(xml::deserialize(parser, "FixedSkills", skills));
  EXPECT_EQ(skills.items[0], "C++");
  EXPECT_EQ(skills.items[1], "Rust");
  EXPECT_EQ(skills.items[2], "Go");
}

/// @brief Underfill: fewer elements than capacity - remaining slots keep
/// defaults.
TEST_F(XmlParserTest, ArrFieldUnderfill) {
  constexpr std::string_view xml_src = R"(
<FixedSkills>
  <Skill>Python</Skill>
</FixedSkills>)";
  xml::Parser parser{xml_src};
  FixedSkills skills;
  ASSERT_TRUE(xml::deserialize(parser, "FixedSkills", skills));
  EXPECT_EQ(skills.items[0], "Python");
  EXPECT_TRUE(skills.items[1].empty());
  EXPECT_TRUE(skills.items[2].empty());
}

/// @brief Overfill: more elements than capacity - extras silently skipped.
TEST_F(XmlParserTest, ArrFieldOverfill) {
  constexpr std::string_view xml_src = R"(
<FixedSkills>
  <Skill>A</Skill>
  <Skill>B</Skill>
  <Skill>C</Skill>
  <Skill>D</Skill>
  <Skill>E</Skill>
</FixedSkills>)";
  xml::Parser parser{xml_src};
  FixedSkills skills;
  ASSERT_TRUE(xml::deserialize(parser, "FixedSkills", skills));
  EXPECT_EQ(skills.items[0], "A");
  EXPECT_EQ(skills.items[1], "B");
  EXPECT_EQ(skills.items[2], "C");
}

/// @brief Empty container.
TEST_F(XmlParserTest, ArrFieldEmpty) {
  constexpr std::string_view xml_src = R"(<FixedSkills></FixedSkills>)";
  xml::Parser parser{xml_src};
  FixedSkills skills;
  ASSERT_TRUE(xml::deserialize(parser, "FixedSkills", skills));
  EXPECT_TRUE(skills.items[0].empty());
  EXPECT_TRUE(skills.items[1].empty());
  EXPECT_TRUE(skills.items[2].empty());
}

/// @brief Self-closing root with arr_field.
TEST_F(XmlParserTest, ArrFieldSelfClosingRoot) {
  constexpr std::string_view xml_src = R"(<FixedSkills/>)";
  xml::Parser parser{xml_src};
  FixedSkills skills;
  ASSERT_TRUE(xml::deserialize(parser, "FixedSkills", skills));
  EXPECT_TRUE(skills.items[0].empty());
}

/// @brief Arr field mixed with attr and element fields (N>1 dispatch table
/// path).
TEST_F(XmlParserTest, ArrFieldMixedWithOtherFields) {
  constexpr std::string_view xml_src = R"(
<MixedRecord id="7">
  <Name>Alice</Name>
  <Score>95</Score>
  <Score>87</Score>
  <Score>92</Score>
</MixedRecord>)";
  xml::Parser parser{xml_src};
  MixedRecord rec;
  ASSERT_TRUE(xml::deserialize(parser, "MixedRecord", rec));
  EXPECT_EQ(rec.id, 7);
  EXPECT_EQ(rec.name, "Alice");
  EXPECT_EQ(rec.scores[0], 95);
  EXPECT_EQ(rec.scores[1], 87);
  EXPECT_EQ(rec.scores[2], 92);
  EXPECT_EQ(rec.scores[3], 0);
}

/// @brief Arr field with overflow in N>1 type - extras skipped cleanly.
TEST_F(XmlParserTest, ArrFieldMixedOverflow) {
  constexpr std::string_view xml_src = R"(
<MixedRecord id="1">
  <Score>1</Score><Score>2</Score><Score>3</Score><Score>4</Score>
  <Score>5</Score><Score>6</Score>
  <Name>Bob</Name>
</MixedRecord>)";
  xml::Parser parser{xml_src};
  MixedRecord rec;
  ASSERT_TRUE(xml::deserialize(parser, "MixedRecord", rec));
  EXPECT_EQ(rec.id, 1);
  EXPECT_EQ(rec.name, "Bob");
  EXPECT_EQ(rec.scores[0], 1);
  EXPECT_EQ(rec.scores[1], 2);
  EXPECT_EQ(rec.scores[2], 3);
  EXPECT_EQ(rec.scores[3], 4);
}

// ---- document-order hint fast path ----

/// @brief An unknown element whose name extends a mapped field's name
/// ("titles" vs "title") must not be matched by the document-order fast
/// path; it is skipped and all mapped siblings parse correctly.
TEST_F(XmlParserTest, UnknownPrefixNamedSiblingIsSkipped) {
  constexpr std::string_view xml_src = R"(
<FlatList>
  <Item id="1">
    <titles>fake</titles>
    <title>Real</title>
    <desc>D</desc>
    <status>2</status>
  </Item>
</FlatList>)";
  xml::Parser parser{xml_src};
  FlatList list;
  ASSERT_TRUE(xml::deserialize(parser, "FlatList", list));
  ASSERT_EQ(list.items.size(), 1U);
  EXPECT_EQ(list.items[0].id, 1);
  EXPECT_EQ(list.items[0].title, "Real");
  EXPECT_EQ(list.items[0].description, "D");
  EXPECT_EQ(list.items[0].status, 2);
}

/// @brief Fields arriving out of metadata order across consecutive items
/// must parse correctly, including when document order is later restored
/// (exercises hint misses and re-synchronisation).
TEST_F(XmlParserTest, OutOfOrderThenInOrderItems) {
  constexpr std::string_view xml_src = R"(
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
  xml::Parser parser{xml_src};
  FlatList list;
  ASSERT_TRUE(xml::deserialize(parser, "FlatList", list));
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

// ---- bool fields ----

/// @brief Bool fields accept the XML Schema boolean lexical space
/// ("true", "false", "1", "0") as both attributes and elements.
TEST_F(XmlParserTest, BoolFieldsAllLexicalForms) {
  {
    constexpr std::string_view xml_src =
        R"(<Toggle enabled="true"><active>1</active><verbose>false</verbose></Toggle>)";
    xml::Parser parser{xml_src};
    Toggle t;
    ASSERT_TRUE(xml::deserialize(parser, "Toggle", t));
    EXPECT_TRUE(t.enabled);
    EXPECT_TRUE(t.active);
    EXPECT_FALSE(t.verbose);
  }
  {
    constexpr std::string_view xml_src =
        R"(<Toggle enabled="0"><active>false</active><verbose>true</verbose></Toggle>)";
    xml::Parser parser{xml_src};
    Toggle t;
    t.enabled = true;
    ASSERT_TRUE(xml::deserialize(parser, "Toggle", t));
    EXPECT_FALSE(t.enabled);
    EXPECT_FALSE(t.active);
    EXPECT_TRUE(t.verbose);
  }
}

/// @brief Invalid bool element text fails the parse, mirroring numeric fields.
TEST_F(XmlParserTest, BoolFieldRejectsInvalidText) {
  constexpr std::string_view xml_src =
      R"(<Toggle enabled="1"><active>yes</active></Toggle>)";
  xml::Parser parser{xml_src};
  Toggle t;
  EXPECT_FALSE(xml::deserialize(parser, "Toggle", t));
}

/// @brief An unparseable bool attribute leaves the default value, consistent
/// with how numeric attributes fail silently.
TEST_F(XmlParserTest, BoolAttrInvalidLeavesDefault) {
  constexpr std::string_view xml_src =
      R"(<Toggle enabled="maybe"><active>1</active><verbose>0</verbose></Toggle>)";
  xml::Parser parser{xml_src};
  Toggle t;
  ASSERT_TRUE(xml::deserialize(parser, "Toggle", t));
  EXPECT_FALSE(t.enabled);
  EXPECT_TRUE(t.active);
  EXPECT_FALSE(t.verbose);
}

/// @brief Bools serialize as "true"/"false" and round-trip.
TEST_F(XmlParserTest, SerializerBoolRoundTrip) {
  Toggle t;
  t.enabled = true;
  t.active = false;
  t.verbose = true;

  const std::string xml = xml::serialize("Toggle", t);
  EXPECT_NE(xml.find("enabled=\"true\""), std::string::npos);
  EXPECT_NE(xml.find("<active>false</active>"), std::string::npos);

  xml::Parser parser{xml};
  Toggle out;
  ASSERT_TRUE(xml::deserialize(parser, "Toggle", out));
  EXPECT_TRUE(out.enabled);
  EXPECT_FALSE(out.active);
  EXPECT_TRUE(out.verbose);
}

// ---- Serializer ----

TEST_F(XmlParserTest, SerializerPrimitiveFields) {
  Person person;
  person.name = "Alice";
  person.age = 30;
  person.address.street = "Main St";
  person.address.zip = 12345;

  const std::string xml = xml::serialize("person", person);

  xml::Parser parser{xml};
  Person out;
  ASSERT_TRUE(xml::deserialize(parser, "person", out));
  EXPECT_EQ(out.name, "Alice");
  EXPECT_EQ(out.age, 30);
  EXPECT_EQ(out.address.street, "Main St");
  EXPECT_EQ(out.address.zip, 12345);
}

TEST_F(XmlParserTest, SerializerAttributeFields) {
  User user;
  user.id = 99;
  user.name = "Grace Hopper";
  user.email = "grace@example.com";

  const std::string xml = xml::serialize("User", user);

  xml::Parser parser{xml};
  User out;
  ASSERT_TRUE(xml::deserialize(parser, "User", out));
  EXPECT_EQ(out.id, 99);
  EXPECT_EQ(out.name, "Grace Hopper");
  EXPECT_EQ(out.email, "grace@example.com");
}

TEST_F(XmlParserTest, SerializerVecField) {
  Users users;
  users.items.push_back({1, "Ada", "ada@example.com"});
  users.items.push_back({2, "Grace", "grace@example.com"});

  const std::string xml = xml::serialize("Users", users);

  xml::Parser parser{xml};
  Users out;
  ASSERT_TRUE(xml::deserialize(parser, "Users", out));
  ASSERT_EQ(out.items.size(), 2U);
  EXPECT_EQ(out.items[0].id, 1);
  EXPECT_EQ(out.items[0].name, "Ada");
  EXPECT_EQ(out.items[1].id, 2);
  EXPECT_EQ(out.items[1].name, "Grace");
}

TEST_F(XmlParserTest, SerializerArrField) {
  FixedSkills skills;
  skills.items[0] = "C++";
  skills.items[1] = "Rust";
  skills.items[2] = "Go";

  const std::string xml = xml::serialize("FixedSkills", skills);

  xml::Parser parser{xml};
  FixedSkills out;
  ASSERT_TRUE(xml::deserialize(parser, "FixedSkills", out));
  EXPECT_EQ(out.items[0], "C++");
  EXPECT_EQ(out.items[1], "Rust");
  EXPECT_EQ(out.items[2], "Go");
}

TEST_F(XmlParserTest, SerializerEmptyVec) {
  Users users;
  const std::string xml = xml::serialize("Users", users);

  xml::Parser parser{xml};
  Users out;
  ASSERT_TRUE(xml::deserialize(parser, "Users", out));
  EXPECT_TRUE(out.items.empty());
}

TEST_F(XmlParserTest, SerializerAttrOnlyTypeIsSelfClosing) {
  AttrItem item;
  item.a1 = 1;
  item.a2 = 2;
  item.s1 = "hello";

  const std::string xml = xml::serialize("Item", item);
  EXPECT_NE(xml.find("/>"), std::string::npos);

  xml::Parser parser{xml};
  AttrItem out;
  ASSERT_TRUE(xml::deserialize(parser, "Item", out));
  EXPECT_EQ(out.a1, 1);
  EXPECT_EQ(out.s1, "hello");
}

TEST_F(XmlParserTest, SerializerEscapesSpecialChars) {
  Person person;
  person.name = "AT&T";
  person.age = 1;

  const std::string xml = xml::serialize("person", person);
  EXPECT_NE(xml.find("&amp;"), std::string::npos);

  xml::Parser parser{xml};
  Person out;
  ASSERT_TRUE(xml::deserialize(parser, "person", out));
  EXPECT_EQ(out.name, "AT&amp;T");
}

TEST_F(XmlParserTest, SerializerCompactMode) {
  Person person;
  person.name = "Bob";
  person.age = 25;

  const std::string pretty = xml::serialize<true>("person", person);
  const std::string compact = xml::serialize<false>("person", person);

  EXPECT_NE(pretty.find('\n'), std::string::npos);
  EXPECT_EQ(compact.find('\n'), std::string::npos);

  xml::Parser parser{compact};
  Person out;
  ASSERT_TRUE(xml::deserialize(parser, "person", out));
  EXPECT_EQ(out.name, "Bob");
  EXPECT_EQ(out.age, 25);
}

TEST_F(XmlParserTest, SerializerRoundTripOrganization) {
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

  const std::string xml = xml::serialize("Organization", org);

  xml::Parser parser{xml};
  Organization out;
  ASSERT_TRUE(xml::deserialize(parser, "Organization", out));
  ASSERT_EQ(out.departments.size(), 1U);
  ASSERT_EQ(out.departments[0].teams.size(), 1U);
  ASSERT_EQ(out.departments[0].teams[0].members.size(), 1U);
  EXPECT_EQ(out.departments[0].teams[0].members[0].full_name, "Ada");
  ASSERT_EQ(out.departments[0].teams[0].members[0].skills.items.size(), 2U);
  EXPECT_EQ(out.departments[0].teams[0].members[0].skills.items[0], "C++");
}

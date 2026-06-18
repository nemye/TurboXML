#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Helpers.hh"
#include "TurboXML.hh"

class TurboBasicTests : public ::testing::Test {};

TEST_F(TurboBasicTests, ParsingNested) {
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
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::RootElementNotFound);

  xml::Parser correct_parser{xml_src};
  ASSERT_TRUE(xml::deserialize(correct_parser, "person", person));
  EXPECT_EQ(person.name, "Alice");
  EXPECT_EQ(person.age, 30);
  EXPECT_EQ(person.address.street, "Main St");
  EXPECT_EQ(person.address.zip, 12345);
}

TEST_F(TurboBasicTests, SkipsUnknownFields) {
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

TEST_F(TurboBasicTests, MissingFieldsRetainDefaults) {
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

TEST_F(TurboBasicTests, MalformedXmlReturnsFalse) {
  constexpr std::string_view xml_src = R"(<person><name>Dave</name>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::UnexpectedEof);
}

TEST_F(TurboBasicTests, EmptyInputReturnsFalse) {
  xml::Parser parser{""};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::RootElementNotFound);
}

TEST_F(TurboBasicTests, ParsesFieldsOutOfOrder) {
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

TEST_F(TurboBasicTests, PullsUserDataAndIgnoresUnknownTags) {
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

TEST_F(TurboBasicTests, DeserializesFullOrganizationHierarchy) {
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
TEST_F(TurboBasicTests, EmptyStringAttribute) {
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
TEST_F(TurboBasicTests, EmptyNumericAttributeAndEmptyElement) {
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
TEST_F(TurboBasicTests, MalformedNumericDataFailsGracefully) {
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
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::InvalidNumericValue);
}

/// @brief Tests that self-closing tags with attributes process the attributes
/// and terminate correctly.
TEST_F(TurboBasicTests, SelfClosingTagWithAttributes) {
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
TEST_F(TurboBasicTests, MixedContentIsIgnored) {
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
TEST_F(TurboBasicTests, IgnoresInterleavedCommentsAndCData) {
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
TEST_F(TurboBasicTests, EntitiesRemainUnexpanded) {
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
TEST_F(TurboBasicTests, MismatchedClosingTagsFailCleanly) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Alice</age> </person>
)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::ElementMismatch);
}

/// @brief Tests that both empty tags and self-closing tags yield empty string
/// views.
TEST_F(TurboBasicTests, EmptyAndSelfClosingStringPrimitives) {
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
TEST_F(TurboBasicTests, DeepMismatchedClosingTagFails) {
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
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::ElementMismatch);
}

/// @brief Tests that a close tag with no name (</>) is rejected as malformed.
TEST_F(TurboBasicTests, EmptyClosingTagNameFails) {
  constexpr std::string_view xml_src = R"(<person></>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::ExpectedNameInCloseTag);
}

/// @brief Tests that a stray close tag appearing before the root open tag
/// causes begin_element to fail immediately.
TEST_F(TurboBasicTests, StrayCloseTagBeforeRootFails) {
  constexpr std::string_view xml_src = R"(</person><person></person>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::RootElementNotFound);
}

/// @brief Tests that a non-xml processing instruction before the root element
/// is skipped and the document parses correctly.
TEST_F(TurboBasicTests, NonXmlProcessingInstructionIsSkipped) {
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
TEST_F(TurboBasicTests, CommentSplitsTextNodeLastSegmentWins) {
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
TEST_F(TurboBasicTests, OpenElementInsideStringPrimitiveIsConsumed) {
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
TEST_F(TurboBasicTests, WhitespaceOnlyNumericFieldFails) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Test</name>
  <age>   </age>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::InvalidNumericValue);
}

/// @brief Tests that a numeric field with whitespace padding around a valid
/// number fails to parse, because std::from_chars does not accept leading
/// whitespace or trailing characters after the number.
TEST_F(TurboBasicTests, WhitespacePaddedNumericFieldFails) {
  constexpr std::string_view xml_src = R"(
<person>
  <name>Test</name>
  <age> 30 </age>
</person>
)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::InvalidNumericValue);
}

TEST_F(TurboBasicTests, ParserCanBeResetAndReused) {
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

TEST_F(TurboBasicTests, PrimitiveFastPathTruncatedCloseTag) {
  // Truncated close tag: `</name` with no `>`.
  std::string xml = "<person><name>Alice</name";
  xml::Parser parser{xml};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::ExpectedCloseTagEnd);
}

TEST_F(TurboBasicTests, IgnoresTrailingDocumentContent) {
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

TEST_F(TurboBasicTests, SkipsDeepUnknownSubtree) {
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

/// @brief Unknown subtrees containing quoted '>' and "/>" in attribute
/// values, comments and CDATA with markup-like content, PIs, and
/// self-closing tags must be skipped without desyncing the parse.
TEST_F(TurboBasicTests, SkipsUnknownSubtreeWithTrickyContent) {
  constexpr std::string_view xml_src = R"(
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
</person>)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Alice");
  EXPECT_EQ(person.age, 30);
}

/// @brief Input truncated inside an unknown subtree must fail the parse.
TEST_F(TurboBasicTests, TruncatedUnknownSubtreeFails) {
  constexpr std::string_view xml_src = R"(<person><unknown><a><b>deep)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::UnexpectedEof);
}

/// @brief An unterminated attribute quote inside an unknown subtree must
/// fail the parse rather than scan past the document end.
TEST_F(TurboBasicTests, UnterminatedQuoteInUnknownSubtreeFails) {
  constexpr std::string_view xml_src =
      R"(<person><unknown><a attr="broken></a></unknown><name>X</name></person>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::UnexpectedEof);
}

TEST_F(TurboBasicTests, SingleQuotedAttributes) {
  constexpr std::string_view xml_src =
      R"(<Users><User id='123'></User></Users>)";

  xml::Parser parser{xml_src};

  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));

  ASSERT_EQ(users.items.size(), 1U);
  EXPECT_EQ(users.items[0].id, 123);
}

TEST_F(TurboBasicTests, MissingAttributeQuoteFails) {
  constexpr std::string_view xml_src = R"(<Users><User id=123></User></Users>)";

  xml::Parser parser{xml_src};

  Users users;
  EXPECT_FALSE(xml::deserialize(parser, "Users", users));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::ExpectedQuotedValue);
}

TEST_F(TurboBasicTests, UnterminatedAttributeFails) {
  constexpr std::string_view xml_src =
      R"(<Users><User id="123></User></Users>)";

  xml::Parser parser{xml_src};

  Users users;
  EXPECT_FALSE(xml::deserialize(parser, "Users", users));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::UnterminatedAttributeValue);
}

TEST_F(TurboBasicTests, UnterminatedCommentFails) {
  constexpr std::string_view xml_src = R"(
<person>
  <!-- broken
  <name>Alice</name>
</person>
)";

  xml::Parser parser{xml_src};

  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::UnterminatedComment);
}

TEST_F(TurboBasicTests, UnterminatedCDataFails) {
  constexpr std::string_view xml_src = R"(
<person>
  <![CDATA[broken
</person>
)";

  xml::Parser parser{xml_src};

  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::UnterminatedCData);
}

TEST_F(TurboBasicTests, UnterminatedProcessingInstructionFails) {
  constexpr std::string_view xml_src =
      R"(<?xml-stylesheet type="text/xsl"<person></person>)";

  xml::Parser parser{xml_src};

  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::UnterminatedPi);
}

TEST_F(TurboBasicTests, NamespacePrefixesAreIgnoredForMatching) {
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

TEST_F(TurboBasicTests, NamespacedAttributes) {
  constexpr std::string_view xml_src =
      R"(<Users><User ns:id="55"></User></Users>)";

  xml::Parser parser{xml_src};

  Users users;

  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  ASSERT_EQ(users.items.size(), 1U);

  EXPECT_EQ(users.items[0].id, 55);
}

TEST_F(TurboBasicTests, LargeVectorOfUsers) {
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

TEST_F(TurboBasicTests, StringViewsReferenceOriginalBuffer) {
  std::string xml = "<person><name>Alice</name></person>";

  xml::Parser parser{xml};

  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));

  const char* begin = xml.data();
  const char* end = begin + xml.size();

  EXPECT_GE(person.name.data(), begin);
  EXPECT_LT(person.name.data(), end);
}

TEST_F(TurboBasicTests, DuplicateFieldLastValueWins) {
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
TEST_F(TurboBasicTests, CaseSensitivity) {
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
TEST_F(TurboBasicTests, PreservesWhitespaceInStrings) {
  constexpr std::string_view xml_src = R"(<person><name>
    Spaced Out
  </name></person>)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "\n    Spaced Out\n  ");
}

/// @brief XML 1.0 forbids duplicate attributes, but TurboXML does not detect
/// them (documented limitation -- the O(n^2) check is too costly for the
/// performance target). The document-order (first) match deterministically
/// wins.
TEST_F(TurboBasicTests, DuplicateAttributesTakesFirst) {
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
TEST_F(TurboBasicTests, RootTagMismatchFails) {
  constexpr std::string_view xml_src = R"(<alien><name>Zorg</name></alien>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::RootElementNotFound);
}

/// @brief Verifies that attributes located directly on the root node being
/// deserialized are parsed and populated correctly.
TEST_F(TurboBasicTests, ParsesAttributesOnRootElement) {
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
TEST_F(TurboBasicTests, DeepNestingDeserialization) {
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
TEST_F(TurboBasicTests, MixedSelfClosingAndStandardTagsInVector) {
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
TEST_F(TurboBasicTests, InvalidTagNamesFailCleanly) {
  constexpr std::string_view xml_src =
      R"(<person><123name>Bob</123name></person>)";
  xml::Parser parser{xml_src};
  Person person;
  // Should fail cleanly inside `parse_element_open` since '1' is not
  // `is_name_start`
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::UnexpectedCharAfterLt);
}

/// @brief Tests malformed tags holding garbage characters where attributes are
/// expected.
TEST_F(TurboBasicTests, MalformedTagGarbageFails) {
  constexpr std::string_view xml_src =
      R"(<person !@#$gar> <name>Alice</name> </person>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::ExpectedAttributeName);
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
TEST_F(TurboBasicTests, MaxDepthBoundarySucceeds) {
  std::string xml_src = generate_nested_xml(xml::Parser::kMaxDepth);

  xml::Parser parser{xml_src};
  TreeNode root;

  // Should successfully parse exactly up to the limit
  ASSERT_TRUE(xml::deserialize(parser, "Node", root));
  ASSERT_EQ(root.children.size(), 1U);
  EXPECT_EQ(root.children[0].children.size(), 1U);
}

/// @brief Tests that the parser cleanly aborts when depth hits kMaxDepth + 1
TEST_F(TurboBasicTests, MaxDepthExceededFailsCleanly) {
  std::string xml_src = generate_nested_xml(xml::Parser::kMaxDepth + 1);

  xml::Parser parser{xml_src};
  TreeNode root;
  EXPECT_FALSE(xml::deserialize(parser, "Node", root));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::DepthExceeded);
}

// ---- try_begin_element fast-path coverage ----

/// @brief Compact deep XML with zero whitespace between tags.
/// Every open tag hits try_begin_element directly.
TEST_F(TurboBasicTests, DeepNestingNoWhitespace) {
  constexpr std::string_view xml_src =
      R"(<DeepList><L1><L2><L3><L4><L5><v>99</v></L5></L4></L3></L2></L1></DeepList>)";
  xml::Parser parser{xml_src};
  DeepList list;
  ASSERT_TRUE(xml::deserialize(parser, "DeepList", list));
  ASSERT_EQ(list.items.size(), 1U);
  EXPECT_EQ(list.items[0].next.next.next.next.value, 99);
}

/// @brief Multiple deep elements back-to-back without whitespace.
TEST_F(TurboBasicTests, DeepNestingMultipleNoWhitespace) {
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
TEST_F(TurboBasicTests, DeepNestingWithNamespaceFallthrough) {
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
TEST_F(TurboBasicTests, TreeNodeWithAttributedChildrenFallthrough) {
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
TEST_F(TurboBasicTests, SelfClosingViaFastPath) {
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
TEST_F(TurboBasicTests, DeepNestingWithUnknownSiblings) {
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
TEST_F(TurboBasicTests, ElementNameCollidesWithAttrField) {
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
TEST_F(TurboBasicTests, EmptyVectorContainer) {
  constexpr std::string_view xml_src = R"(<Users></Users>)";
  xml::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  EXPECT_TRUE(users.items.empty());
}

/// @brief Self-closing root that holds a vector field.
TEST_F(TurboBasicTests, SelfClosingVectorRoot) {
  constexpr std::string_view xml_src = R"(<Users/>)";
  xml::Parser parser{xml_src};
  Users users;
  ASSERT_TRUE(xml::deserialize(parser, "Users", users));
  EXPECT_TRUE(users.items.empty());
}

/// @brief All self-closing elements inside a vector.
TEST_F(TurboBasicTests, ConsecutiveSelfClosingInVector) {
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
TEST_F(TurboBasicTests, VecOfPrimitivesEmpty) {
  constexpr std::string_view xml_src = R"(<Skills></Skills>)";
  xml::Parser parser{xml_src};
  Skills skills;
  ASSERT_TRUE(xml::deserialize(parser, "Skills", skills));
  EXPECT_TRUE(skills.items.empty());
}

/// @brief Multiple primitives in a vector.
TEST_F(TurboBasicTests, VecOfPrimitivesMultiple) {
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
TEST_F(TurboBasicTests, VecOfPrimitivesSelfClosing) {
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
TEST_F(TurboBasicTests, FullAttrItemAllAttributesParsed) {
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

/// @brief Attributes arriving out of metadata order must still bind via the
/// fallback scan (exercises the attribute document-order cursor miss path).
TEST_F(TurboBasicTests, OutOfOrderAttributesParsed) {
  constexpr std::string_view xml_src = R"(
<AttrList>
  <Item s5="five" a1="10" s1="one" a5="50" a2="20"
        s2="two" a3="30" s4="four" a4="40" s3="three"/>
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
TEST_F(TurboBasicTests, FlatListParsing) {
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
TEST_F(TurboBasicTests, ResetAfterFailedParse) {
  std::string xml = R"(<person><name>Alice</name><age>30</age></person>)";
  xml::Parser parser{xml};

  Person p1;
  EXPECT_FALSE(xml::deserialize(parser, "employee", p1));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::RootElementNotFound);

  parser.reset();
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::None);  // reset clears it

  Person p2;
  ASSERT_TRUE(xml::deserialize(parser, "person", p2));
  EXPECT_EQ(p2.name, "Alice");
  EXPECT_EQ(p2.age, 30);
}

/// @brief Very deeply nested unknown subtree must be fully skipped.
TEST_F(TurboBasicTests, SkipsVeryDeeplyNestedUnknownSubtree) {
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
TEST_F(TurboBasicTests, MultipleProcessingInstructions) {
  constexpr std::string_view xml_src =
      R"(<?xml version="1.0"?><?xml-stylesheet type="text/xsl"?><person><name>Bob</name></person>)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Bob");
}

/// @brief Comments containing isolated dashes and a "->" near-miss delimiter
/// must not cause premature termination of the comment scan. (Interior "--"
/// is a separate well-formedness error; see DoubleHyphenInsideCommentRejected.)
TEST_F(TurboBasicTests, CommentWithNearMissDelimiters) {
  constexpr std::string_view xml_src = R"(
<person>
  <!-- tricky - dashes -> not yet > still going - almost -->
  <name>Survived</name>
  <age>1</age>
</person>)";
  xml::Parser parser{xml_src};
  Person person;
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(person.name, "Survived");
  EXPECT_EQ(person.age, 1);
}

/// @brief XML forbids "--" within a comment's content (WFC). TurboXML enforces
/// this in a single scanning pass and reports MalformedComment.
TEST_F(TurboBasicTests, DoubleHyphenInsideCommentRejected) {
  constexpr std::string_view xml_src =
      R"(<person><!-- bad -- comment --><name>X</name></person>)";
  xml::Parser parser{xml_src};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::MalformedComment);
}

/// @brief Verifies that long comments interleaved with elements are
/// correctly skipped and all element data is parsed. Mirrors the
/// comment-heavy benchmark payload structure.
TEST_F(TurboBasicTests, CommentHeavyPayloadParsesCorrectly) {
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
TEST_F(TurboBasicTests, StringFieldsMaterializeData) {
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
TEST_F(TurboBasicTests, StringAttrsMaterializeData) {
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
TEST_F(TurboBasicTests, VecOfStringMaterializesData) {
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
TEST_F(TurboBasicTests, SelfClosingStringFieldIsEmpty) {
  OwnedPerson person;
  std::string xml = "<person><name/><age>25</age><email/></person>";
  xml::Parser parser{xml};
  ASSERT_TRUE(xml::deserialize(parser, "person", person));
  EXPECT_TRUE(person.name.empty());
  EXPECT_EQ(person.age, 25);
  EXPECT_TRUE(person.email.empty());
}

/// @brief Exact fill: element count == array capacity.
TEST_F(TurboBasicTests, ArrFieldExactFill) {
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
TEST_F(TurboBasicTests, ArrFieldUnderfill) {
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
TEST_F(TurboBasicTests, ArrFieldOverfill) {
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
TEST_F(TurboBasicTests, ArrFieldEmpty) {
  constexpr std::string_view xml_src = R"(<FixedSkills></FixedSkills>)";
  xml::Parser parser{xml_src};
  FixedSkills skills;
  ASSERT_TRUE(xml::deserialize(parser, "FixedSkills", skills));
  EXPECT_TRUE(skills.items[0].empty());
  EXPECT_TRUE(skills.items[1].empty());
  EXPECT_TRUE(skills.items[2].empty());
}

/// @brief Self-closing root with arr_field.
TEST_F(TurboBasicTests, ArrFieldSelfClosingRoot) {
  constexpr std::string_view xml_src = R"(<FixedSkills/>)";
  xml::Parser parser{xml_src};
  FixedSkills skills;
  ASSERT_TRUE(xml::deserialize(parser, "FixedSkills", skills));
  EXPECT_TRUE(skills.items[0].empty());
}

/// @brief Arr field mixed with attr and element fields (N>1 dispatch table
/// path).
TEST_F(TurboBasicTests, ArrFieldMixedWithOtherFields) {
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
TEST_F(TurboBasicTests, ArrFieldMixedOverflow) {
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
TEST_F(TurboBasicTests, UnknownPrefixNamedSiblingIsSkipped) {
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
TEST_F(TurboBasicTests, OutOfOrderThenInOrderItems) {
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
TEST_F(TurboBasicTests, BoolFieldsAllLexicalForms) {
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
TEST_F(TurboBasicTests, BoolFieldRejectsInvalidText) {
  constexpr std::string_view xml_src =
      R"(<Toggle enabled="1"><active>yes</active></Toggle>)";
  xml::Parser parser{xml_src};
  Toggle t;
  EXPECT_FALSE(xml::deserialize(parser, "Toggle", t));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::InvalidNumericValue);
}

/// @brief An unparseable bool attribute leaves the default value, consistent
/// with how numeric attributes fail silently.
TEST_F(TurboBasicTests, BoolAttrInvalidLeavesDefault) {
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
TEST_F(TurboBasicTests, SerializerBoolRoundTrip) {
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

TEST_F(TurboBasicTests, SerializerPrimitiveFields) {
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

TEST_F(TurboBasicTests, SerializerAttributeFields) {
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

TEST_F(TurboBasicTests, SerializerVecField) {
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

TEST_F(TurboBasicTests, SerializerArrField) {
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

TEST_F(TurboBasicTests, SerializerEmptyVec) {
  Users users;
  const std::string xml = xml::serialize("Users", users);

  xml::Parser parser{xml};
  Users out;
  ASSERT_TRUE(xml::deserialize(parser, "Users", out));
  EXPECT_TRUE(out.items.empty());
}

TEST_F(TurboBasicTests, SerializerAttrOnlyTypeIsSelfClosing) {
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

TEST_F(TurboBasicTests, SerializerEscapesSpecialChars) {
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

TEST_F(TurboBasicTests, SerializerCompactMode) {
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

TEST_F(TurboBasicTests, SerializerRoundTripOrganization) {
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

// ---- Required fields (optional by default) ----

struct ReqRecord {
  int id{};               // required attribute
  std::string_view name;  // required element
  std::string_view note;  // optional element
};

template <>
struct xml::XmlMetadata<ReqRecord> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("id", &ReqRecord::id, true),
                      xml::field("name", &ReqRecord::name, true),
                      xml::field("note", &ReqRecord::note));
};

struct ReqList {
  std::vector<std::string_view> items;  // required: at least one
};

template <>
struct xml::XmlMetadata<ReqList> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("item", &ReqList::items, true));
};

struct ReqParent {
  ReqRecord child;  // required nested object
};

template <>
struct xml::XmlMetadata<ReqParent> {
  static constexpr auto fields =
      std::make_tuple(xml::field("ReqRecord", &ReqParent::child, true));
};

/// @brief All required fields present, optional one absent -> success.
TEST_F(TurboBasicTests, RequiredFieldsAllPresentOptionalAbsent) {
  constexpr std::string_view xml_src =
      R"(<ReqRecord id="7"><name>Ada</name></ReqRecord>)";
  xml::Parser parser{xml_src};
  ReqRecord rec;
  ASSERT_TRUE(xml::deserialize(parser, "ReqRecord", rec));
  EXPECT_EQ(rec.id, 7);
  EXPECT_EQ(rec.name, "Ada");
  EXPECT_TRUE(rec.note.empty());
}

/// @brief Optional field may also be present.
TEST_F(TurboBasicTests, RequiredFieldsWithOptionalPresent) {
  constexpr std::string_view xml_src =
      R"(<ReqRecord id="7"><name>Ada</name><note>hi</note></ReqRecord>)";
  xml::Parser parser{xml_src};
  ReqRecord rec;
  ASSERT_TRUE(xml::deserialize(parser, "ReqRecord", rec));
  EXPECT_EQ(rec.note, "hi");
}

/// @brief Out-of-order required fields still satisfy the mask.
TEST_F(TurboBasicTests, RequiredFieldsOutOfOrder) {
  constexpr std::string_view xml_src =
      R"(<ReqRecord id="7"><note>hi</note><name>Ada</name></ReqRecord>)";
  xml::Parser parser{xml_src};
  ReqRecord rec;
  ASSERT_TRUE(xml::deserialize(parser, "ReqRecord", rec));
  EXPECT_EQ(rec.name, "Ada");
  EXPECT_EQ(rec.note, "hi");
}

/// @brief Missing a required child element -> MissingRequiredField.
TEST_F(TurboBasicTests, MissingRequiredElementFails) {
  constexpr std::string_view xml_src =
      R"(<ReqRecord id="7"><note>hi</note></ReqRecord>)";
  xml::Parser parser{xml_src};
  ReqRecord rec;
  EXPECT_FALSE(xml::deserialize(parser, "ReqRecord", rec));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::MissingRequiredField);
}

/// @brief Missing a required attribute -> MissingRequiredField.
TEST_F(TurboBasicTests, MissingRequiredAttributeFails) {
  constexpr std::string_view xml_src =
      R"(<ReqRecord><name>Ada</name></ReqRecord>)";
  xml::Parser parser{xml_src};
  ReqRecord rec;
  EXPECT_FALSE(xml::deserialize(parser, "ReqRecord", rec));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::MissingRequiredField);
}

/// @brief A self-closing element cannot satisfy a required child element,
/// even though its required attribute is present.
TEST_F(TurboBasicTests, SelfClosingMissingRequiredElementFails) {
  constexpr std::string_view xml_src = R"(<ReqRecord id="7"/>)";
  xml::Parser parser{xml_src};
  ReqRecord rec;
  EXPECT_FALSE(xml::deserialize(parser, "ReqRecord", rec));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::MissingRequiredField);
}

/// @brief A required container with zero matching children fails.
TEST_F(TurboBasicTests, RequiredContainerEmptyFails) {
  constexpr std::string_view xml_src = R"(<ReqList></ReqList>)";
  xml::Parser parser{xml_src};
  ReqList list;
  EXPECT_FALSE(xml::deserialize(parser, "ReqList", list));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::MissingRequiredField);
}

/// @brief A required container with at least one child succeeds (exercises the
/// N==1 fast-path bit set).
TEST_F(TurboBasicTests, RequiredContainerNonEmptySucceeds) {
  constexpr std::string_view xml_src =
      R"(<ReqList><item>a</item><item>b</item></ReqList>)";
  xml::Parser parser{xml_src};
  ReqList list;
  ASSERT_TRUE(xml::deserialize(parser, "ReqList", list));
  ASSERT_EQ(list.items.size(), 2U);
  EXPECT_EQ(list.items[0], "a");
}

/// @brief A required nested object that is absent fails at the parent level.
TEST_F(TurboBasicTests, RequiredNestedObjectMissingFails) {
  constexpr std::string_view xml_src = R"(<ReqParent></ReqParent>)";
  xml::Parser parser{xml_src};
  ReqParent p;
  EXPECT_FALSE(xml::deserialize(parser, "ReqParent", p));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::MissingRequiredField);
}

/// @brief A required nested object that is present but itself incomplete
/// propagates MissingRequiredField from the inner pull().
TEST_F(TurboBasicTests, RequiredNestedObjectIncompleteFails) {
  constexpr std::string_view xml_src =
      R"(<ReqParent><ReqRecord id="1"></ReqRecord></ReqParent>)";
  xml::Parser parser{xml_src};
  ReqParent p;
  EXPECT_FALSE(xml::deserialize(parser, "ReqParent", p));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::MissingRequiredField);
}

/// @brief A required nested object, fully populated, succeeds.
TEST_F(TurboBasicTests, RequiredNestedObjectCompleteSucceeds) {
  constexpr std::string_view xml_src =
      R"(<ReqParent><ReqRecord id="1"><name>Ada</name></ReqRecord></ReqParent>)";
  xml::Parser parser{xml_src};
  ReqParent p;
  ASSERT_TRUE(xml::deserialize(parser, "ReqParent", p));
  EXPECT_EQ(p.child.id, 1);
  EXPECT_EQ(p.child.name, "Ada");
}

// ---- Resource bounds (untrusted input) ----

/// @brief Exactly kMaxAttributesPerElement attributes is accepted (boundary).
TEST_F(TurboBasicTests, MaxAttributesBoundaryAccepted) {
  std::string xml = "<User";
  xml.reserve(xml::Parser::kMaxAttributesPerElement * 5 + 16);
  for (size_t i = 0; i < xml::Parser::kMaxAttributesPerElement; ++i) {
    xml += R"( a="")";
  }
  xml += "/>";
  xml::Parser parser{xml};
  User user;
  EXPECT_TRUE(xml::deserialize(parser, "User", user));
}

/// @brief One attribute past the cap is rejected with TooManyAttributes.
TEST_F(TurboBasicTests, TooManyAttributesRejected) {
  std::string xml = "<User";
  xml.reserve(xml::Parser::kMaxAttributesPerElement * 5 + 16);
  for (size_t i = 0; i <= xml::Parser::kMaxAttributesPerElement; ++i) {
    xml += R"( a="")";
  }
  xml += "/>";
  xml::Parser parser{xml};
  User user;
  EXPECT_FALSE(xml::deserialize(parser, "User", user));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::TooManyAttributes);
}

/// @brief A deeply nested *unknown* subtree beyond kMaxDepth is rejected by
/// skip_element (depth parity with the recursive descent path) rather than
/// silently skipped.
TEST_F(TurboBasicTests, UnknownSubtreeDepthLimited) {
  const int over = xml::Parser::kMaxDepth + 5;
  std::string xml = "<person><name>A</name><unknown>";
  for (int i = 0; i < over; ++i) {
    xml += "<n>";
  }
  for (int i = 0; i < over; ++i) {
    xml += "</n>";
  }
  xml += "</unknown><age>1</age></person>";
  xml::Parser parser{xml};
  Person person;
  EXPECT_FALSE(xml::deserialize(parser, "person", person));
  EXPECT_EQ(parser.error_code(), xml::ErrorCode::DepthExceeded);
}

// ---- Normalization & reference expansion (NormalizingParser, opt-in) ----
//
// Normalization is gated on BasicParser<true> (xml::NormalizingParser) and only
// applies to owning std::string fields; std::string_view fields stay raw and
// zero-copy. The default xml::Parser compiles these paths away entirely.

struct NormRecord {
  std::string body;      // element text: expanded + EOL-normalized
  std::string attr;      // attribute value: expanded + whitespace-normalized
  std::string_view raw;  // string_view stays raw even under normalization
};

template <>
struct xml::XmlMetadata<NormRecord> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("a", &NormRecord::attr),
                      xml::field("body", &NormRecord::body),
                      xml::field("raw", &NormRecord::raw));
};

struct NormText {
  std::string v;
};

template <>
struct xml::XmlMetadata<NormText> {
  static constexpr auto fields = std::make_tuple(xml::field("v", &NormText::v));
};

/// @brief The five predefined entities expand in element text.
TEST_F(TurboBasicTests, NormalizePredefinedEntities) {
  constexpr std::string_view src =
      R"(<NormText><v>a &amp; b &lt; c &gt; d &apos; e &quot; f</v></NormText>)";
  xml::NormalizingParser p{src};
  NormText t;
  ASSERT_TRUE(xml::deserialize(p, "NormText", t));
  EXPECT_EQ(t.v, "a & b < c > d ' e \" f");
}

/// @brief Decimal and hex character references expand, including a multi-byte
/// code point encoded as UTF-8.
TEST_F(TurboBasicTests, NormalizeCharacterReferences) {
  constexpr std::string_view src =
      R"(<NormText><v>&#65;&#x42;&#x2764;</v></NormText>)";
  xml::NormalizingParser p{src};
  NormText t;
  ASSERT_TRUE(xml::deserialize(p, "NormText", t));
  EXPECT_EQ(t.v,
            std::string("AB") + "\xE2\x9D\xA4");  // U+2764 HEAVY BLACK HEART
}

/// @brief CDATA content is copied literally: references inside it are NOT
/// expanded, and it concatenates with surrounding text runs.
TEST_F(TurboBasicTests, NormalizeCDataLiteralAndConcatenated) {
  constexpr std::string_view src =
      R"(<NormText><v>x &amp; <![CDATA[y &amp; z]]> w</v></NormText>)";
  xml::NormalizingParser p{src};
  NormText t;
  ASSERT_TRUE(xml::deserialize(p, "NormText", t));
  EXPECT_EQ(t.v, "x & y &amp; z w");
}

/// @brief CR and CRLF in element text normalize to a single LF; tabs survive.
TEST_F(TurboBasicTests, NormalizeLineEndings) {
  const std::string src = "<NormText><v>a\r\nb\rc\td</v></NormText>";
  xml::NormalizingParser p{src};
  NormText t;
  ASSERT_TRUE(xml::deserialize(p, "NormText", t));
  EXPECT_EQ(t.v, "a\nb\nc\td");
}

/// @brief Attribute values: literal whitespace (tab/newline) becomes a space,
/// but whitespace introduced via a character reference is preserved literally.
TEST_F(TurboBasicTests, NormalizeAttributeWhitespace) {
  const std::string src =
      "<NormRecord a=\"x\ty\nz&#9;w\"><body>b</body><raw>r</raw></NormRecord>";
  xml::NormalizingParser p{src};
  NormRecord r;
  ASSERT_TRUE(xml::deserialize(p, "NormRecord", r));
  EXPECT_EQ(r.attr, "x y z\tw");  // literal tab/LF -> space; &#9; stays a tab
}

/// @brief std::string_view fields are unaffected by normalization (raw bytes),
/// even on the normalizing parser, while sibling std::string fields expand.
TEST_F(TurboBasicTests, NormalizeLeavesStringViewRaw) {
  constexpr std::string_view src =
      R"(<NormRecord a="ok"><body>m &amp; n</body><raw>p &amp; q</raw></NormRecord>)";
  xml::NormalizingParser p{src};
  NormRecord r;
  ASSERT_TRUE(xml::deserialize(p, "NormRecord", r));
  EXPECT_EQ(r.body, "m & n");     // owning std::string -> expanded
  EXPECT_EQ(r.raw, "p &amp; q");  // string_view -> raw, zero-copy
}

/// @brief The default parser performs no expansion: owning std::string fields
/// receive raw bytes, preserving byte-for-byte fidelity (opt-in by design).
TEST_F(TurboBasicTests, DefaultParserDoesNotNormalize) {
  constexpr std::string_view src = R"(<NormText><v>a &amp; b</v></NormText>)";
  xml::Parser p{src};
  NormText t;
  ASSERT_TRUE(xml::deserialize(p, "NormText", t));
  EXPECT_EQ(t.v, "a &amp; b");
}

/// @brief An undefined entity (no DTD, not one of the five predefined) is a
/// hard error on the normalizing path, in element text and in attributes.
TEST_F(TurboBasicTests, NormalizeUndefinedEntityFailsInText) {
  constexpr std::string_view src = R"(<NormText><v>a &bogus; b</v></NormText>)";
  xml::NormalizingParser p{src};
  NormText t;
  EXPECT_FALSE(xml::deserialize(p, "NormText", t));
  EXPECT_EQ(p.error_code(), xml::ErrorCode::UndefinedEntity);
}

TEST_F(TurboBasicTests, NormalizeUndefinedEntityFailsInAttribute) {
  constexpr std::string_view src =
      R"(<NormRecord a="x &bogus; y"><body>b</body><raw>r</raw></NormRecord>)";
  xml::NormalizingParser p{src};
  NormRecord r;
  EXPECT_FALSE(xml::deserialize(p, "NormRecord", r));
  EXPECT_EQ(p.error_code(), xml::ErrorCode::UndefinedEntity);
}

/// @brief A malformed or out-of-range character reference fails with
/// InvalidCharRef.
TEST_F(TurboBasicTests, NormalizeInvalidCharRefFails) {
  constexpr std::string_view src = R"(<NormText><v>&#xZZ;</v></NormText>)";
  xml::NormalizingParser p{src};
  NormText t;
  EXPECT_FALSE(xml::deserialize(p, "NormText", t));
  EXPECT_EQ(p.error_code(), xml::ErrorCode::InvalidCharRef);
}

/// @brief A character reference to a code point outside the XML Char production
/// (here NUL) is rejected as InvalidCharRef.
TEST_F(TurboBasicTests, NormalizeForbiddenCodePointFails) {
  constexpr std::string_view src = R"(<NormText><v>&#0;</v></NormText>)";
  xml::NormalizingParser p{src};
  NormText t;
  EXPECT_FALSE(xml::deserialize(p, "NormText", t));
  EXPECT_EQ(p.error_code(), xml::ErrorCode::InvalidCharRef);
}

// ---- Strict (fully-conforming) parser: no false positives ----
//
// StrictParser enforces the three WFCs (rejection cases live in the
// conformance suite). These guard against false positives and confirm it still
// normalizes (StrictParser = normalize + strict).

/// @brief A CDATA section's terminating "]]>" must NOT be flagged as the
/// forbidden CharData sequence (the check runs on text, not CDATA content).
TEST_F(TurboBasicTests, StrictAcceptsCDataTerminator) {
  constexpr std::string_view xml_src =
      R"(<NormText><v><![CDATA[x]]></v></NormText>)";
  xml::StrictParser p{xml_src};
  NormText t;
  ASSERT_TRUE(xml::deserialize(p, "NormText", t));
  EXPECT_EQ(t.v, "x");
}

/// @brief "]]" in text without a following '>' is well-formed and accepted.
TEST_F(TurboBasicTests, StrictAcceptsBracketsWithoutClose) {
  constexpr std::string_view xml_src = R"(<NormText><v>a ]] b</v></NormText>)";
  xml::StrictParser p{xml_src};
  NormText t;
  ASSERT_TRUE(xml::deserialize(p, "NormText", t));
  EXPECT_EQ(t.v, "a ]] b");
}

/// @brief StrictParser is fully conforming: it still expands references and
/// normalizes into owning std::string fields.
TEST_F(TurboBasicTests, StrictParserNormalizes) {
  constexpr std::string_view xml_src =
      R"(<NormRecord a="x&#9;y"><body>m &amp; n</body><raw>r</raw></NormRecord>)";
  xml::StrictParser p{xml_src};
  NormRecord r;
  ASSERT_TRUE(xml::deserialize(p, "NormRecord", r));
  EXPECT_EQ(r.body, "m & n");  // entity expanded
  EXPECT_EQ(r.attr, "x\ty");   // &#9; preserved as a literal tab
}

// ===========================================================================
// Library extensions: lifted field ceiling, enums, value fields, recursion.
// ===========================================================================

// ---- Enumerations via XmlEnumTraits (string tokens) ----
enum class Priority { Low, Medium, High };
template <>
struct xml::XmlEnumTraits<Priority> {
  static constexpr auto values = xml::enum_table<Priority>(
      {{"Low", Priority::Low},
       {"Medium", Priority::Medium},
       {"High", Priority::High}});
};

struct Task {
  Priority priority{};  // attribute
  Priority level{};     // child element
};
template <>
struct xml::XmlMetadata<Task> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("priority", &Task::priority),
                      xml::field("level", &Task::level));
};

TEST_F(TurboBasicTests, EnumFieldRoundTrip) {
  constexpr std::string_view src =
      R"(<Task priority="High"><level>Medium</level></Task>)";
  xml::Parser p{src};
  Task t;
  ASSERT_TRUE(xml::deserialize(p, "Task", t));
  EXPECT_EQ(t.priority, Priority::High);
  EXPECT_EQ(t.level, Priority::Medium);
  const std::string out = xml::serialize<false>("Task", t);
  EXPECT_EQ(out, R"(<Task priority="High"><level>Medium</level></Task>)");
}

TEST_F(TurboBasicTests, EnumUnknownTokenFails) {
  constexpr std::string_view src =
      R"(<Task priority="High"><level>Wizard</level></Task>)";
  xml::Parser p{src};
  Task t;
  EXPECT_FALSE(xml::deserialize(p, "Task", t));
  EXPECT_EQ(p.error_code(), xml::ErrorCode::InvalidEnumValue);
}

// ---- Value field (XSD simpleContent) ----
struct Money {
  std::string_view currency;  // attribute
  double amount{};            // element's own text
};
template <>
struct xml::XmlMetadata<Money> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("currency", &Money::currency),
                      xml::value_field(&Money::amount));
};
struct Invoice {
  Money total;
};
template <>
struct xml::XmlMetadata<Invoice> {
  static constexpr auto fields =
      std::make_tuple(xml::field("total", &Invoice::total));
};

TEST_F(TurboBasicTests, ValueFieldNumericRoundTrip) {
  constexpr std::string_view src =
      R"(<Invoice><total currency="USD">9.99</total></Invoice>)";
  xml::Parser p{src};
  Invoice inv;
  ASSERT_TRUE(xml::deserialize(p, "Invoice", inv));
  EXPECT_EQ(inv.total.currency, "USD");
  EXPECT_DOUBLE_EQ(inv.total.amount, 9.99);
  const std::string out = xml::serialize<false>("Invoice", inv);
  EXPECT_EQ(out, R"(<Invoice><total currency="USD">9.99</total></Invoice>)");
}

struct Measure {
  std::string unit;  // attribute
  std::string text;  // required value
};
template <>
struct xml::XmlMetadata<Measure> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("unit", &Measure::unit),
                      xml::value_field(&Measure::text, true));
};
struct MeasureDoc {
  Measure m;
};
template <>
struct xml::XmlMetadata<MeasureDoc> {
  static constexpr auto fields =
      std::make_tuple(xml::field("m", &MeasureDoc::m));
};

TEST_F(TurboBasicTests, ValueFieldStringNormalized) {
  constexpr std::string_view src =
      R"(<MeasureDoc><m unit="kg">a &amp; b</m></MeasureDoc>)";
  xml::NormalizingParser p{src};
  MeasureDoc d;
  ASSERT_TRUE(xml::deserialize(p, "MeasureDoc", d));
  EXPECT_EQ(d.m.unit, "kg");
  EXPECT_EQ(d.m.text, "a & b");  // entity expanded into the value
}

TEST_F(TurboBasicTests, ValueFieldRequiredEmptyFails) {
  // Self-closing and empty both lack text -> required value field is missing.
  for (std::string_view src :
       {std::string_view(R"(<MeasureDoc><m unit="kg"/></MeasureDoc>)"),
        std::string_view(R"(<MeasureDoc><m unit="kg"></m></MeasureDoc>)")}) {
    xml::Parser p{src};
    MeasureDoc d;
    EXPECT_FALSE(xml::deserialize(p, "MeasureDoc", d));
    EXPECT_EQ(p.error_code(), xml::ErrorCode::MissingRequiredField);
  }
}

// ---- Recursion via std::unique_ptr ----
struct Section {
  std::string_view title;
  std::unique_ptr<Section> sub;  // optional, recursive
};
template <>
struct xml::XmlMetadata<Section> {
  static constexpr auto fields = std::make_tuple(
      xml::field("title", &Section::title), xml::field("sub", &Section::sub));
};

TEST_F(TurboBasicTests, UniquePtrRecursionChain) {
  constexpr std::string_view src =
      R"(<Section><title>A</title><sub><title>B</title>)"
      R"(<sub><title>C</title></sub></sub></Section>)";
  xml::Parser p{src};
  Section s;
  ASSERT_TRUE(xml::deserialize(p, "Section", s));
  EXPECT_EQ(s.title, "A");
  ASSERT_TRUE(s.sub);
  EXPECT_EQ(s.sub->title, "B");
  ASSERT_TRUE(s.sub->sub);
  EXPECT_EQ(s.sub->sub->title, "C");
  EXPECT_FALSE(s.sub->sub->sub);
  const std::string out = xml::serialize<false>("Section", s);
  EXPECT_EQ(out, R"(<Section><title>A</title><sub><title>B</title>)"
                 R"(<sub><title>C</title></sub></sub></Section>)");
}

TEST_F(TurboBasicTests, UniquePtrAbsentChildOmitted) {
  constexpr std::string_view src = R"(<Section><title>solo</title></Section>)";
  xml::Parser p{src};
  Section s;
  ASSERT_TRUE(xml::deserialize(p, "Section", s));
  EXPECT_EQ(s.title, "solo");
  EXPECT_FALSE(s.sub);
  const std::string out = xml::serialize<false>("Section", s);
  EXPECT_EQ(out, R"(<Section><title>solo</title></Section>)");  // no <sub>
}

// ---- More than 64 fields (multiword required mask) ----
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
template <>
struct xml::XmlMetadata<Wide72> {
  static constexpr auto fields = std::make_tuple(
      xml::attr_field("a0", &Wide72::a0, true),
      xml::attr_field("a1", &Wide72::a1), xml::attr_field("a2", &Wide72::a2),
      xml::attr_field("a3", &Wide72::a3), xml::attr_field("a4", &Wide72::a4),
      xml::attr_field("a5", &Wide72::a5), xml::attr_field("a6", &Wide72::a6),
      xml::attr_field("a7", &Wide72::a7), xml::attr_field("a8", &Wide72::a8),
      xml::attr_field("a9", &Wide72::a9), xml::attr_field("a10", &Wide72::a10),
      xml::attr_field("a11", &Wide72::a11),
      xml::attr_field("a12", &Wide72::a12),
      xml::attr_field("a13", &Wide72::a13),
      xml::attr_field("a14", &Wide72::a14),
      xml::attr_field("a15", &Wide72::a15),
      xml::attr_field("a16", &Wide72::a16),
      xml::attr_field("a17", &Wide72::a17),
      xml::attr_field("a18", &Wide72::a18),
      xml::attr_field("a19", &Wide72::a19),
      xml::attr_field("a20", &Wide72::a20),
      xml::attr_field("a21", &Wide72::a21),
      xml::attr_field("a22", &Wide72::a22),
      xml::attr_field("a23", &Wide72::a23),
      xml::attr_field("a24", &Wide72::a24),
      xml::attr_field("a25", &Wide72::a25),
      xml::attr_field("a26", &Wide72::a26),
      xml::attr_field("a27", &Wide72::a27),
      xml::attr_field("a28", &Wide72::a28),
      xml::attr_field("a29", &Wide72::a29),
      xml::attr_field("a30", &Wide72::a30),
      xml::attr_field("a31", &Wide72::a31),
      xml::attr_field("a32", &Wide72::a32),
      xml::attr_field("a33", &Wide72::a33),
      xml::attr_field("a34", &Wide72::a34),
      xml::attr_field("a35", &Wide72::a35),
      xml::attr_field("a36", &Wide72::a36),
      xml::attr_field("a37", &Wide72::a37),
      xml::attr_field("a38", &Wide72::a38),
      xml::attr_field("a39", &Wide72::a39),
      xml::attr_field("a40", &Wide72::a40),
      xml::attr_field("a41", &Wide72::a41),
      xml::attr_field("a42", &Wide72::a42),
      xml::attr_field("a43", &Wide72::a43),
      xml::attr_field("a44", &Wide72::a44),
      xml::attr_field("a45", &Wide72::a45),
      xml::attr_field("a46", &Wide72::a46),
      xml::attr_field("a47", &Wide72::a47),
      xml::attr_field("a48", &Wide72::a48),
      xml::attr_field("a49", &Wide72::a49),
      xml::attr_field("a50", &Wide72::a50),
      xml::attr_field("a51", &Wide72::a51),
      xml::attr_field("a52", &Wide72::a52),
      xml::attr_field("a53", &Wide72::a53),
      xml::attr_field("a54", &Wide72::a54),
      xml::attr_field("a55", &Wide72::a55),
      xml::attr_field("a56", &Wide72::a56),
      xml::attr_field("a57", &Wide72::a57),
      xml::attr_field("a58", &Wide72::a58),
      xml::attr_field("a59", &Wide72::a59),
      xml::attr_field("a60", &Wide72::a60),
      xml::attr_field("a61", &Wide72::a61),
      xml::attr_field("a62", &Wide72::a62),
      xml::attr_field("a63", &Wide72::a63),
      xml::attr_field("a64", &Wide72::a64, true),
      xml::attr_field("a65", &Wide72::a65),
      xml::attr_field("a66", &Wide72::a66),
      xml::attr_field("a67", &Wide72::a67),
      xml::attr_field("a68", &Wide72::a68),
      xml::attr_field("a69", &Wide72::a69),
      xml::attr_field("a70", &Wide72::a70),
      xml::attr_field("a71", &Wide72::a71, true));
};

static std::string wide_xml(int omit) {
  std::string s = "<Wide72";
  for (int i = 0; i < 72; ++i) {
    if (i == omit) continue;
    s += " a" + std::to_string(i) + "=\"" + std::to_string(i) + "\"";
  }
  s += "/>";
  return s;
}

TEST_F(TurboBasicTests, MoreThan64FieldsAllPresent) {
  const std::string src = wide_xml(-1);
  xml::Parser p{src};
  Wide72 w;
  ASSERT_TRUE(xml::deserialize(p, "Wide72", w));
  EXPECT_EQ(w.a0, 0);
  EXPECT_EQ(w.a64, 64);
  EXPECT_EQ(w.a71, 71);
}

TEST_F(TurboBasicTests, MoreThan64FieldsMissingRequiredSecondWord) {
  // a64 is required and lives in the second mask word; dropping it fails.
  const std::string src = wide_xml(64);
  xml::Parser p{src};
  Wide72 w;
  EXPECT_FALSE(xml::deserialize(p, "Wide72", w));
  EXPECT_EQ(p.error_code(), xml::ErrorCode::MissingRequiredField);
}

/// @file test_Conformance.cc
/// @brief XML 1.0 (Fifth Edition) conformance test suite for LightningXML.
///
/// Tests are organized by section of W3C REC-xml-20081126.
/// Each test references the relevant spec section, production rule,
/// or well-formedness constraint (WFC).
///
/// LightningXML is a zero-copy pull-parser/deserializer. The default xmlight::Parser
/// enforces structural well-formedness. Additional conformance is opt-in:
///
///   xmlight::NormalizingParser (normalize=true):
///     - Predefined entity expansion &amp; &lt; &gt; &apos; &quot; (sec 4.6)
///     - Character reference resolution &#nnn; / &#xhh; (sec 4.1)
///     - End-of-line normalization \r\n -> \n, \r -> \n (sec 2.11)
///     - Attribute-value whitespace normalization (sec 3.3.3)
///
///   xmlight::StrictParser (normalize=true, strict=true):
///     - All of the above, plus:
///     - "]]>" rejection in character data (sec 2.4)
///     - '<' rejection in attribute values (sec 3.1 WFC)
///     - Duplicate-attribute detection (sec 3.1 WFC)
///     - Forbidden control bytes (sec 2.2, Production [2]) rejected in
///       character data, CDATA, attribute values, comments, and PIs
///
/// Deliberately not implemented (design trade-offs):
///   - DTD processing (sec 2.8, 3.2-3.4) -- DOCTYPE is skipped, not interpreted
///   - External entity resolution (sec 4.4) -- requires file I/O
///   - Encoding-level validation (sec 2.2, 4.3.3) -- multi-byte UTF-8 passes
///     through unchecked on every parser; the basic and normalizing parsers
///     also pass raw control bytes through (character references are always
///     validated). Skipped constructs (DOCTYPE, unmapped subtrees) are
///     scanned structurally, not validated.
/// @link https://www.w3.org/TR/2008/REC-xml-20081126/

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "LightningXML.hh"

// Helper structs for exercising the parser.

struct Leaf {
  std::string_view text;
};

template<>
struct xmlight::XmlMetadata<Leaf> {
  static constexpr auto fields = std::make_tuple(xmlight::field("v", &Leaf::text));
};

struct LeafInt {
  int value{};
};

template<>
struct xmlight::XmlMetadata<LeafInt> {
  static constexpr auto fields = std::make_tuple(xmlight::field("v", &LeafInt::value));
};

struct TwoFields {
  std::string_view a;
  std::string_view b;
};

template<>
struct xmlight::XmlMetadata<TwoFields> {
  static constexpr auto fields =
      std::make_tuple(xmlight::field("a", &TwoFields::a), xmlight::field("b", &TwoFields::b));
};

struct AttrOnly {
  std::string_view x;
  std::string_view y;
};

template<>
struct xmlight::XmlMetadata<AttrOnly> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("x", &AttrOnly::x), xmlight::attrField("y", &AttrOnly::y));
};

struct AttrInt {
  int id{};
  std::string_view name;
};

template<>
struct xmlight::XmlMetadata<AttrInt> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("id", &AttrInt::id),
                                                 xmlight::attrField("name", &AttrInt::name));
};

struct VecLeaf {
  std::vector<std::string_view> items;
};

template<>
struct xmlight::XmlMetadata<VecLeaf> {
  static constexpr auto fields = std::make_tuple(xmlight::vecField("item", &VecLeaf::items));
};

struct Nested {
  Leaf inner;
};

template<>
struct xmlight::XmlMetadata<Nested> {
  static constexpr auto fields = std::make_tuple(xmlight::field("inner", &Nested::inner));
};

struct OwnedLeaf {
  std::string text;
};

template<>
struct xmlight::XmlMetadata<OwnedLeaf> {
  static constexpr auto fields = std::make_tuple(xmlight::field("v", &OwnedLeaf::text));
};

struct OwnedAttr {
  std::string x;
};

template<>
struct xmlight::XmlMetadata<OwnedAttr> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("x", &OwnedAttr::x));
};

// sec 2.1 - Well-Formed XML Documents [Production 1: document]
class Sec21WellFormedDocument : public ::testing::Test {};

/// Production [1]: document ::= prolog element Misc*
/// A minimal well-formed document is a single root element.
TEST_F(Sec21WellFormedDocument, MinimalDocument) {
  const std::string_view src = R"(<r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// There must be exactly one root element.
/// Content after the root close tag should not interfere with parsing
/// the root itself.
TEST_F(Sec21WellFormedDocument, ExtraContentAfterRootIgnored) {
  const std::string_view src = R"(<r><v>ok</v></r><extra/>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// No root element at all - must fail.
TEST_F(Sec21WellFormedDocument, NoRootElement) {
  const std::string_view src = R"(<!-- just a comment -->)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::RootElementNotFound);
}

/// Empty input - must fail.
TEST_F(Sec21WellFormedDocument, EmptyInput) {
  xmlight::Parser p{""};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::RootElementNotFound);
}

/// Whitespace-only input - must fail (no element).
TEST_F(Sec21WellFormedDocument, WhitespaceOnlyInput) {
  const std::string_view src = "   \n\t  \n  ";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::RootElementNotFound);
}

// sec 2.2 - Characters [Production 2: Char]
class Sec22Characters : public ::testing::Test {};

/// Tab (#x9), LF (#xA), CR (#xD), and space (#x20) are legal in content.
TEST_F(Sec22Characters, LegalWhitespaceInContent) {
  const std::string src = "<r><v>\t\n text\r\n</v></r>";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_FALSE(leaf.text.empty());
}

/// NUL (#x0) is outside the Char production. On the basic and normalizing
/// parsers raw byte validation is a documented design trade-off (see file
/// header): text passes through unchecked, so the document is accepted and
/// the NUL preserved. StrictParser rejects it (see
/// StrictRejectsForbiddenControlBytes).
TEST_F(Sec22Characters, NulByteInContentPassedThrough) {
  std::string src = "<r><v>ab";
  src.push_back('\0');
  src += "cd</v></r>";
  {
    xmlight::Parser p{src};
    Leaf leaf;
    ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
    EXPECT_EQ(leaf.text.size(), 5U);  // "ab\0cd", NUL included
  }
  {
    xmlight::NormalizingParser p{src};
    OwnedLeaf leaf;
    ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
    EXPECT_EQ(leaf.text.size(), 5U);
  }
}

/// Production [2] Char excludes controls below #x20 other than tab, LF, and
/// CR. StrictParser reports them as fatal in every tokenized context:
/// character data (both the close-tag fast path and mixed content), CDATA,
/// attribute values, comments, and PI data.
TEST_F(Sec22Characters, StrictRejectsForbiddenControlBytes) {
  const std::vector<std::string> bad = {
      std::string("<r><v>a\x01"
                  "b</v></r>"),                         // text, fast path
      std::string("<r><v>a") + '\0' + "b</v></r>",      // NUL in text
      std::string("<r><v>a\x02y<!-- c --></v></r>"),    // text, tokenized run
      std::string("<r><v><![CDATA[a\x03z]]></v></r>"),  // CDATA content
      std::string("<r x=\"a\x04\"><v>t</v></r>"),       // attribute value
      std::string("<r><!-- bad \x05 --><v>t</v></r>"),  // comment content
      std::string("<r><?pi bad \x06 ?><v>t</v></r>"),   // PI data
      // Long run: the control byte sits inside a 16-byte word-scan chunk.
      "<r><v>" + std::string(20, 'a') + '\x07' + std::string(20, 'b') + "</v></r>",
      // 8..15-byte run with the control byte inside the single-word step.
      std::string("<r><v>ab\x0B"
                  "cdefghij</v></r>"),
  };
  for (const std::string& src : bad) {
    xmlight::StrictParser sp{src};
    OwnedLeaf leaf;
    EXPECT_FALSE(xmlight::deserialize(sp, "r", leaf)) << src;
    EXPECT_EQ(sp.errorCode(), xmlight::ErrorCode::ForbiddenControlChar) << src;
  }
}

/// Tab, LF, and CR are the legal sub-#x20 characters, and DEL (#x7F) is
/// within the Char range; none of them trip the strict scan.
TEST_F(Sec22Characters, StrictAllowsLegalControlCharacters) {
  {
    const std::string src = "<r x=\"a\tb\"><v>l1\r\nl2\x7F</v><!-- \t\r\n --><?pi \t?></r>";
    xmlight::StrictParser sp{src};
    OwnedLeaf leaf;
    ASSERT_TRUE(xmlight::deserialize(sp, "r", leaf));
    EXPECT_EQ(leaf.text, "l1\nl2\x7F");
  }
  {
    // A clean run past the 16-byte word-scan chunks, with legal tab/LF mixed
    // in past the first chunk.
    const std::string text = std::string(20, 'a') + "\t" + std::string(20, 'b') + "\n";
    const std::string src = "<r><v>" + text + "</v></r>";
    xmlight::StrictParser sp{src};
    OwnedLeaf leaf;
    ASSERT_TRUE(xmlight::deserialize(sp, "r", leaf));
    EXPECT_EQ(leaf.text, text);
  }
}

/// Valid multi-byte UTF-8 characters in element names and content.
/// NameStartChar allows [#xC0-#xD6] etc.
TEST_F(Sec22Characters, Utf8InElementNamesAndContent) {
  // "café" as element name (é = U+00E9 -> 0xC3 0xA9 in UTF-8)
  const std::string_view src = R"(<r><v>café</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "café");
}

/// Three-byte UTF-8 (CJK) in content.
TEST_F(Sec22Characters, Utf8CjkContent) {
  const std::string_view src = R"(<r><v>漢字</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "漢字");
}

// sec 2.3 - Common Syntactic Constructs [Productions 3-8: S, Name, etc.]
class Sec23Names : public ::testing::Test {};

/// Names may contain hyphens, dots, digits, underscores, colons.
/// Production [4a]: NameChar includes '-', '.', [0-9].
TEST_F(Sec23Names, NameWithHyphenDotDigit) {
  const std::string_view src = R"(<root-1.0><v>ok</v></root-1.0>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "root-1.0", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Names starting with underscore are legal.
/// Production [4]: NameStartChar includes '_'.
TEST_F(Sec23Names, NameStartsWithUnderscore) {
  const std::string_view src = R"(<_r><v>ok</v></_r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "_r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Names starting with colon are legal per XML 1.0 (though discouraged
/// by the Namespaces spec). Production [4]: NameStartChar includes ':'.
TEST_F(Sec23Names, NameStartsWithColon) {
  const std::string_view src = R"(<:r><v>ok</v></:r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  // Colon at start means the prefix is empty and local name is "r" in
  // LightningXML's namespace-aware parse_name. The root match uses full
  // name comparison, so we try both.
  // The parser's begin_element compares token.name which is the local
  // part after the colon. So root_name "r" should match.
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Names must not start with a digit.
/// Production [4]: NameStartChar excludes [0-9].
TEST_F(Sec23Names, NameStartingWithDigitFails) {
  const std::string_view src = R"(<1tag></1tag>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "1tag", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnexpectedCharAfterLt);
}

/// Names must not start with hyphen.
TEST_F(Sec23Names, NameStartingWithHyphenFails) {
  const std::string_view src = R"(<-tag></-tag>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "-tag", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnexpectedCharAfterLt);
}

/// Names must not start with a dot.
TEST_F(Sec23Names, NameStartingWithDotFails) {
  const std::string_view src = R"(<.tag></.tag>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, ".tag", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnexpectedCharAfterLt);
}

// sec 2.4 - Character Data and Markup [Production 14: CharData]
class Sec24CharData : public ::testing::Test {};

/// Production [14]: CharData must not contain "]]>" (the CDATA close
/// delimiter appearing in ordinary text is a fatal error per spec).
///
/// StrictParser enforces this; the default Parser opts out of the extra text
/// scan for speed and accepts it.
TEST_F(Sec24CharData, CDataEndDelimiterInTextStrictFails) {
  const std::string_view src = R"(<r><v>bad ]]> text</v></r>)";

  xmlight::StrictParser sp{src};
  Leaf strict_leaf;
  EXPECT_FALSE(xmlight::deserialize(sp, "r", strict_leaf));
  EXPECT_EQ(sp.errorCode(), xmlight::ErrorCode::CDataEndInContent);

  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_TRUE(xmlight::deserialize(p, "r", leaf));  // accepted by the fast path
}

/// Production [14] again, but through the tokenizer: the comment after the
/// text defeats the close-tag fast path the test above exercises.
TEST_F(Sec24CharData, StrictCDataEndInMixedContentFails) {
  const std::string_view src = R"(<r><v>bad ]]> text<!-- c --></v></r>)";
  xmlight::StrictParser sp{src};
  OwnedLeaf leaf;
  EXPECT_FALSE(xmlight::deserialize(sp, "r", leaf));
  EXPECT_EQ(sp.errorCode(), xmlight::ErrorCode::CDataEndInContent);
}

/// "]]" or "]" without a closing '>' is ordinary character data; the strict
/// scan must keep looking rather than reject the bare prefix, including a
/// trailing "]]" cut short by the end of the run.
TEST_F(Sec24CharData, StrictLoneBracketsAllowed) {
  const std::string_view src = R"(<r><v>a ] b ]]</v></r>)";
  xmlight::StrictParser sp{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(sp, "r", leaf));
  EXPECT_EQ(leaf.text, "a ] b ]]");
}

/// A child element interrupting a scalar leaf's text is rejected at its end
/// tag: the leaf expects its own close next (WFC: Element Type Match).
TEST_F(Sec24CharData, TextInterruptedByChildElementFails) {
  const std::string_view src = R"(<r><v>x<b>y</b>z</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ElementMismatch);
}

// sec 2.5 - Comments [Production 15]
class Sec25Comments : public ::testing::Test {};

/// Well-formed comment between elements should be skipped.
TEST_F(Sec25Comments, CommentBetweenElements) {
  const std::string_view src = R"(<r><!-- hello --><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Comment before the root element (in prolog) should be skipped.
TEST_F(Sec25Comments, CommentInProlog) {
  const std::string_view src = R"(<!-- prolog comment --><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Comment after the root element (in Misc) should be ignored.
TEST_F(Sec25Comments, CommentAfterRoot) {
  const std::string_view src = R"(<r><v>ok</v></r><!-- trailing -->)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Empty comment is legal: "<!---->".
TEST_F(Sec25Comments, EmptyComment) {
  const std::string_view src = R"(<!----><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Comments must not contain "--" (double hyphen).
/// Production [15]: Comment ::= '<!--' ((Char - '-') | ('-' (Char - '-')))*
/// '-->'
///
/// LightningXML enforces this WFC: the comment scan rejects any interior "--"
/// (the first "--" must begin the "-->" terminator).
TEST_F(Sec25Comments, DoubleHyphenInsideCommentFails) {
  const std::string_view src = R"(<!-- bad -- comment --><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::MalformedComment);
}

/// Comment ending with "--->" is ill-formed: the content's trailing '-' is
/// adjacent to the terminator's leading '-', forming a forbidden "--".
TEST_F(Sec25Comments, CommentEndingWithTripleHyphenFails) {
  const std::string_view src = R"(<!--- bad ---><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::MalformedComment);
}

/// Unterminated comment - must fail.
TEST_F(Sec25Comments, UnterminatedComment) {
  const std::string_view src = R"(<!-- never ends <r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnterminatedComment);
}

/// A comment whose "--" sits at the very end of input is unterminated, not
/// malformed: the "--" must begin a complete "-->".
TEST_F(Sec25Comments, CommentDoubleHyphenAtEndOfInput) {
  const std::string_view src = R"(<r><v>ok</v><!-- trailing --)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnterminatedComment);
}

/// A lone '-' at the very end of input is likewise unterminated.
TEST_F(Sec25Comments, CommentLoneHyphenAtEndOfInput) {
  const std::string_view src = R"(<r><v>ok</v><!-- trailing -)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnterminatedComment);
}

/// Input ending immediately after "<!--".
TEST_F(Sec25Comments, TruncatedCommentStart) {
  const std::string_view src = R"(<r><!--)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnterminatedComment);
}

// sec 2.6 - Processing Instructions [Production 16-17]
class Sec26Pi : public ::testing::Test {};

/// Production [17]: only case variants of exactly "xml" are reserved; shorter
/// or longer targets that merely start the same are ordinary PIs.
TEST_F(Sec26Pi, PITargetNearXmlNamesAllowed) {
  const std::string_view src = R"(<?xm d?><?xmz d?><?xql d?><?xmll d?><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// A well-formed PI before the root is skipped.
TEST_F(Sec26Pi, PIInProlog) {
  const std::string_view src = R"(<?myapp version="2.0"?><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// A PI between child elements is skipped.
TEST_F(Sec26Pi, PIBetweenElements) {
  const std::string_view src = R"(<r><?proc data?><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Production [16]: the data part is optional -- "<?target?>" is legal.
TEST_F(Sec26Pi, PIWithNoData) {
  const std::string_view src = R"(<?proc?><r><?p2?><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// PITarget must not be "xml" (case-insensitive) for processing
/// instructions (as opposed to the XML declaration).
/// Production [17]: PITarget ::= Name - (('X'|'x')('M'|'m')('L'|'l'))
///
/// NOTE: LightningXML treats "<?xml ...?>" as TokenType::XmlDeclaration and
/// any other PI target as TokenType::ProcessingInstruction. A reserved
/// case-variant of "xml" ("XML", "Xml", ...) is rejected as an illegal PI
/// target (see PITargetXmlMixedCaseRejected).
TEST_F(Sec26Pi, PITargetXmlLowercase) {
  // "<?xml ...?>" at the start is the XML declaration - legal.
  const std::string_view src = R"(<?xml version="1.0"?><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Production [17]: PITarget excludes every case variant of "xml". Only the
/// exact lowercase form names the XML declaration; "<?XML?>", "<?Xml?>", etc.
/// are reserved and ill-formed as PI targets.
TEST_F(Sec26Pi, PITargetXmlMixedCaseRejected) {
  const std::string_view src = R"(<?XML version="1.0"?><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  // Production 17 reserves every case variant of "xml"; LightningXML rejects a
  // mixed-case "xml" PI target as ill-formed.
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ReservedPiTarget);
}

/// A PI target that merely starts with "xml" (e.g. "xml-stylesheet") is a
/// legal Name, not the reserved target, and must be skipped without error.
TEST_F(Sec26Pi, PITargetXmlPrefixedNameAllowed) {
  const std::string_view src = R"(<?xml-stylesheet href="s.xsl"?><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Unterminated PI - must fail.
TEST_F(Sec26Pi, UnterminatedPI) {
  const std::string_view src = R"(<?proc never ends <r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnterminatedPi);
}

// sec 2.7 - CDATA Sections [Production 18-21]
class Sec27Cdata : public ::testing::Test {};

/// Input ending immediately after "<![CDATA[" is unterminated.
TEST_F(Sec27Cdata, TruncatedCDataStart) {
  const std::string_view src = R"(<r><![CDATA[)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnterminatedCData);
}

/// Well-formed CDATA between elements is skipped during struct
/// deserialization (it's not bound to a field in pull()).
TEST_F(Sec27Cdata, CDataBetweenElements) {
  const std::string_view src = R"(<r><![CDATA[ignored <markup> & stuff]]><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// CDATA sections cannot be nested.
/// "<![CDATA[outer <![CDATA[inner]]> ]]>" is ill-formed because the
/// first "]]>" terminates the outer section, leaving " ]]>" as stray
/// text. But the parser should not crash.
TEST_F(Sec27Cdata, NestedCData) {
  const std::string_view src = R"(<r><![CDATA[outer <![CDATA[inner]]> rest]]><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  // The exact behavior depends on how scan_to_delimiter handles the
  // first "]]>". The important thing is no crash.
  std::ignore = xmlight::deserialize(p, "r", leaf);
}

/// Unterminated CDATA - must fail.
TEST_F(Sec27Cdata, UnterminatedCData) {
  const std::string_view src = R"(<r><![CDATA[never ends</r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnterminatedCData);
}

// sec 2.8 - Prolog and Document Type Declaration [Production 22-28]
class Sec28Prolog : public ::testing::Test {};

/// XML declaration must come before the root element.
TEST_F(Sec28Prolog, XmlDeclarationBeforeRoot) {
  const std::string_view src = R"(<?xml version="1.0" encoding="UTF-8"?><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// XML declaration with standalone="yes".
TEST_F(Sec28Prolog, XmlDeclarationStandalone) {
  const std::string_view src = R"(<?xml version="1.0" standalone="yes"?><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Productions [24]-[26] and [32] allow single-quoted values in the XML
/// declaration.
TEST_F(Sec28Prolog, XmlDeclarationSingleQuotes) {
  const std::string_view src = R"(<?xml version='1.0' encoding='UTF-8'?><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// DOCTYPE declaration should be skipped (non-validating parser).
/// LightningXML handles "<!...>" by scanning for '>' and moving on.
TEST_F(Sec28Prolog, DoctypeSkipped) {
  const std::string_view src =
      R"(<?xml version="1.0"?><!DOCTYPE root SYSTEM "root.dtd"><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// DOCTYPE with internal subset (inline DTD). The parser should skip the
/// entire block, including nested declarations whose '>' must not terminate
/// the outer DOCTYPE prematurely (sec 2.8, Production [28]).
TEST_F(Sec28Prolog, DoctypeWithInternalSubset) {
  const std::string_view src =
      R"(<?xml version="1.0"?><!DOCTYPE r [<!ELEMENT r (v)><!ELEMENT v (#PCDATA)>]><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// DOCTYPE with entity declaration containing '>' in quoted literal must
/// not terminate the internal subset scan early (sec 2.8, 4.2).
TEST_F(Sec28Prolog, DoctypeEntityWithGtInLiteral) {
  const std::string_view src =
      R"(<?xml version="1.0"?><!DOCTYPE r [<!ENTITY bar "a>b">]><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Comments and PIs inside the internal subset may contain '>' without
/// terminating the DOCTYPE scan (sec 2.8, Production [28b] intSubset).
TEST_F(Sec28Prolog, DoctypeWithNestedCommentAndPi) {
  const std::string_view src =
      R"(<!DOCTYPE r [ <!-- a > inside --> <?pi with > inside ?> ]><r><v>ok</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// DOCTYPE scan edge cases: a stray ']' outside any subset is ignored, while
/// input truncated inside a literal, a nested "<!-", or right after '<' fails.
TEST_F(Sec28Prolog, DoctypeEdgeCases) {
  {
    xmlight::Parser p{R"(<!DOCTYPE r ]junk><r><v>ok</v></r>)"};
    Leaf leaf;
    ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
    EXPECT_EQ(leaf.text, "ok");
  }
  for (const std::string_view bad :
       {std::string_view(R"(<!DOCTYPE r "unterminated)"), std::string_view(R"(<!DOCTYPE r [ <!-)"),
        std::string_view(R"(<!DOCTYPE r [ <)")}) {
    xmlight::Parser p{bad};
    Leaf leaf;
    EXPECT_FALSE(xmlight::deserialize(p, "r", leaf)) << bad;
  }
}

/// A partial BOM is not stripped; the bytes are treated as (skippable, since
/// they precede the root) character data.
TEST_F(Sec28Prolog, PartialBomNotStripped) {
  for (const std::string& src :
       {std::string("\xEF\xBBx<r><v>ok</v></r>"), std::string("\xEFx<r><v>ok</v></r>")}) {
    xmlight::Parser p{src};
    Leaf leaf;
    ASSERT_TRUE(xmlight::deserialize(p, "r", leaf)) << src;
    EXPECT_EQ(leaf.text, "ok");
  }
}

/// UTF-8 BOM (\xEF\xBB\xBF) at the start of the document must be stripped
/// transparently before parsing begins (sec 4.3.3, Appendix F).
TEST_F(Sec28Prolog, Utf8BomStripped) {
  // BOM + "<?xml version=\"1.0\"?><r><v>ok</v></r>"
  const std::string src = "\xEF\xBB\xBF<?xml version=\"1.0\"?><r><v>ok</v></r>";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// UTF-8 BOM without an XML declaration (bare BOM before root element).
TEST_F(Sec28Prolog, Utf8BomNoDeclaration) {
  const std::string src = "\xEF\xBB\xBF<r><v>ok</v></r>";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

// sec 2.10 - White Space Handling
class Sec210Whitespace : public ::testing::Test {};

/// Leading and trailing whitespace in element content is preserved
/// in the string_view (XML spec says processors must pass all characters
/// that are not markup to the application).
TEST_F(Sec210Whitespace, WhitespacePreservedInContent) {
  const std::string_view src = R"(<r><v>  hello  </v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "  hello  ");
}

/// Whitespace between child elements (insignificant whitespace) should
/// not disrupt parsing.
TEST_F(Sec210Whitespace, WhitespaceBetweenElements) {
  const std::string_view src = "<r>\n  <a>x</a>\n  <b>y</b>\n</r>";
  xmlight::Parser p{src};
  TwoFields tf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", tf));
  EXPECT_EQ(tf.a, "x");
  EXPECT_EQ(tf.b, "y");
}

// sec 2.11 - End-of-Line Handling
class Sec211Eol : public ::testing::Test {};

/// Per spec, \r\n -> \n and bare \r -> \n before any other processing. Applied
/// on the normalizing parser for owning std::string fields; a zero-copy
/// std::string_view necessarily preserves the raw bytes.
TEST_F(Sec211Eol, CrLfNormalization) {
  const std::string src = "<r><v>line1\r\nline2</v></r>";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "line1\nline2");
}

/// Bare \r -> \n.
TEST_F(Sec211Eol, BareCarriageReturn) {
  const std::string src = "<r><v>a\rb</v></r>";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "a\nb");
}

// sec 3.1 - Start-Tags, End-Tags, and Empty-Element Tags
//         [Productions 40-44, WFC: Element Type Match,
//          WFC: Unique Att Spec, WFC: No < in Attribute Values]
class Sec31Tags : public ::testing::Test {};

/// WFC: Element Type Match - the end-tag name must match the start-tag.
TEST_F(Sec31Tags, WFC_ElementTypeMatch_Mismatch) {
  const std::string_view src = R"(<r><v>data</wrong></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ElementMismatch);
}

/// WFC: Element Type Match - case-sensitive matching.
TEST_F(Sec31Tags, WFC_ElementTypeMatch_CaseSensitive) {
  const std::string_view src = R"(<R><v>data</v></R>)";
  xmlight::Parser p{src};
  Leaf leaf;
  // "R" and "r" are different names.
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::RootElementNotFound);
  xmlight::Parser p2{src};
  ASSERT_TRUE(xmlight::deserialize(p2, "R", leaf));
  EXPECT_EQ(leaf.text, "data");
}

/// Empty-element tag (self-closing).
/// Production [44]: EmptyElemTag ::= '<' Name (S Attribute)* S? '/>'
TEST_F(Sec31Tags, EmptyElementTag) {
  const std::string_view src = R"(<r><v/></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_TRUE(leaf.text.empty());
}

/// Empty-element tag with attributes.
TEST_F(Sec31Tags, EmptyElementTagWithAttrs) {
  const std::string_view src = R"(<r x="hello" y="world"/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "hello");
  EXPECT_EQ(ao.y, "world");
}

/// Whitespace around '=' in attributes is legal.
/// Production [25]: Eq ::= S? '=' S?
TEST_F(Sec31Tags, WhitespaceAroundEquals) {
  const std::string_view src = R"(<r x = "hello" y= "world" />)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "hello");
  EXPECT_EQ(ao.y, "world");
}

/// Production [3]: S is any mix of #x20, #x9, #xD, #xA -- all four forms are
/// legal between attributes, around '=', and before the tag close.
TEST_F(Sec31Tags, WhitespaceVariantsInsideTags) {
  const std::string src = "<r\tx\n=\r\"hello\"\r\ny\t=\t\"world\"\n/>";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "hello");
  EXPECT_EQ(ao.y, "world");
}

/// Production [42]: only whitespace may separate an end-tag's name from '>'.
TEST_F(Sec31Tags, CloseTagJunkAfterNameFails) {
  const std::string_view src = R"(<r><v>x</v y></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedCloseTagEnd);
}

/// Input truncated inside an end-tag.
TEST_F(Sec31Tags, TruncatedInsideCloseTagFails) {
  const std::string_view src = R"(<r><v>x</v)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedCloseTagEnd);
}

/// Production [40]: a start-tag needs a non-empty name; a lone ':' leaves the
/// local part empty.
TEST_F(Sec31Tags, EmptyLocalNameFails) {
  const std::string_view src = R"(<r><:>x</:></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedElementName);
}

/// Production [42]: whitespace before the end-tag's '>' is legal.
TEST_F(Sec31Tags, CloseTagTrailingWhitespace) {
  const std::string_view src = "<r><v>x</v  ></r>";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "x");
}

/// Input truncated inside an end-tag's trailing whitespace.
TEST_F(Sec31Tags, TruncatedInCloseTagWhitespaceFails) {
  const std::string_view src = "<r><v>x</v  ";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedCloseTagEnd);
}

/// Production [10]: single-quoted values (with and without whitespace around
/// '='); the 16-byte value probe hands off to the memchr scan for longer
/// values.
TEST_F(Sec31Tags, SingleQuotedAndProbeLengthAttrValues) {
  const std::string_view src = R"(<r x = 'hello' y="0123456789ABCDEF"/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "hello");
  EXPECT_EQ(ao.y, "0123456789ABCDEF");
}

/// Input truncated right after '=' (no value at all).
TEST_F(Sec31Tags, TruncatedAfterEqualsFails) {
  const std::string_view src = R"(<r x=)";
  xmlight::Parser p{src};
  AttrOnly ao;
  EXPECT_FALSE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedQuotedValue);
}

/// Production [44]: '/' in a start-tag must be immediately followed by '>'.
TEST_F(Sec31Tags, SlashNotFollowedByGtFails) {
  for (const std::string_view src :
       {std::string_view(R"(<r x="1"/ >)"), std::string_view(R"(<r x="1"/)")}) {
    xmlight::Parser p{src};
    AttrOnly ao;
    EXPECT_FALSE(xmlight::deserialize(p, "r", ao)) << src;
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedAttributeName) << src;
  }
}

/// WFC: Unique Att Spec compares prefix and local name; "a" and "p:a" share a
/// local name (and its hash) but are distinct attributes.
TEST_F(Sec31Tags, StrictPrefixedAttrSameLocalNameAllowed) {
  const std::string_view src = R"(<r a="1" p:a="2"><v>x</v></r>)";
  xmlight::StrictParser sp{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(sp, "r", leaf));
  EXPECT_EQ(leaf.text, "x");
}

/// Whitespace before '/>' is legal.
TEST_F(Sec31Tags, WhitespaceBeforeSelfClose) {
  const std::string_view src = R"(<r x="v"   />)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "v");
}

/// Whitespace before '>' in end-tag is legal.
/// Production [42]: ETag ::= '</' Name S? '>'
TEST_F(Sec31Tags, WhitespaceInEndTag) {
  const std::string_view src = R"(<r><v>ok</v  ></r  >)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Attribute values in single quotes are legal.
/// Production [10]: AttValue ::= '"' ... '"' | "'" ... "'"
TEST_F(Sec31Tags, SingleQuotedAttributes) {
  const std::string_view src = R"(<r x='hello' y='world'/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "hello");
  EXPECT_EQ(ao.y, "world");
}

/// Mixed single and double quotes across different attributes.
TEST_F(Sec31Tags, MixedQuoteStyles) {
  const std::string_view src = R"(<r x="hello" y='world'/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "hello");
  EXPECT_EQ(ao.y, "world");
}

/// Double quote inside single-quoted value and vice versa.
TEST_F(Sec31Tags, QuoteCharInsideOppositeDelimiter) {
  const std::string_view src = R"(<r x='say "hi"' y="it's"/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, R"(say "hi")");
  EXPECT_EQ(ao.y, "it's");
}

/// Unquoted attribute value - must fail.
TEST_F(Sec31Tags, UnquotedAttributeFails) {
  const std::string_view src = R"(<r x=hello/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  EXPECT_FALSE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedQuotedValue);
}

/// WFC: No < in Attribute Values.
/// Production [10]: AttValue must not contain '<'.
///
/// StrictParser enforces this; the default Parser accepts it.
TEST_F(Sec31Tags, WFC_NoLtInAttributeValue) {
  const std::string_view src = R"(<r x="a<b"/>)";

  xmlight::StrictParser sp{src};
  AttrOnly strict_ao;
  EXPECT_FALSE(xmlight::deserialize(sp, "r", strict_ao));
  EXPECT_EQ(sp.errorCode(), xmlight::ErrorCode::LtInAttributeValue);

  xmlight::Parser p{src};
  AttrOnly ao;
  EXPECT_TRUE(xmlight::deserialize(p, "r", ao));
}

/// WFC: Unique Att Spec - no two attributes in a start-tag may share
/// the same name.
///
/// StrictParser enforces this; the default Parser accepts duplicates and the
/// document-order match wins in attr().
TEST_F(Sec31Tags, WFC_UniqueAttSpec) {
  const std::string_view src = R"(<r x="first" x="second"/>)";

  xmlight::StrictParser sp{src};
  AttrOnly strict_ao;
  EXPECT_FALSE(xmlight::deserialize(sp, "r", strict_ao));
  EXPECT_EQ(sp.errorCode(), xmlight::ErrorCode::DuplicateAttribute);

  xmlight::Parser p{src};
  AttrOnly ao;
  EXPECT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "first");  // document-order match wins
}

/// WFC: Unique Att Spec on a start-tag far wider than any real one.
///
/// Detection must stay exact once the tag outgrows the direct-scan regime and
/// moves into the probe table, both for a duplicate at the very end (which
/// only the table can catch) and for a tag that is merely wide and legal. The
/// assertion here is correctness, not timing: a wall-clock bound would be
/// flaky in CI, and the linear behavior is what keeps this test quick enough
/// to notice if it ever regresses.
TEST_F(Sec31Tags, WFC_UniqueAttSpecWideStartTag) {
  constexpr int kCount = 20000;
  std::string wide;
  for (int i = 0; i < kCount; ++i) {
    wide += " a" + std::to_string(i) + R"(="v")";
  }

  {
    const std::string src = "<r" + wide + "/>";
    xmlight::StrictParser sp{src};
    AttrOnly ao;
    EXPECT_TRUE(xmlight::deserialize(sp, "r", ao));
    EXPECT_EQ(sp.errorCode(), xmlight::ErrorCode::None);
  }
  {
    // The duplicate names the very first attribute, so nothing but a complete
    // record of every name seen so far can catch it.
    const std::string src = "<r" + wide + R"( a0="dup"/>)";
    xmlight::StrictParser sp{src};
    AttrOnly ao;
    EXPECT_FALSE(xmlight::deserialize(sp, "r", ao));
    EXPECT_EQ(sp.errorCode(), xmlight::ErrorCode::DuplicateAttribute);
  }
}

/// Sec 4.3.3 - the parser reads UTF-8 only, so anything else must be rejected
/// outright rather than scanned as bytes and reported as a missing root.
TEST_F(Sec31Tags, RejectsNonUtf8Input) {
  using namespace std::string_view_literals;  // the wide forms embed NUL bytes
  const auto rejected = [](std::string_view src) {
    xmlight::Parser p{src};
    Leaf leaf;
    return !xmlight::deserialize(p, "r", leaf) &&
           p.errorCode() == xmlight::ErrorCode::UnsupportedEncoding;
  };

  EXPECT_TRUE(rejected("\xFF\xFE<\0r\0>\0"sv));          // UTF-16LE BOM
  EXPECT_TRUE(rejected("\xFE\xFF\0<\0r\0>"sv));          // UTF-16BE BOM
  EXPECT_TRUE(rejected("\xFF\xFE\0\0<\0\0\0"sv));        // UTF-32LE BOM
  EXPECT_TRUE(rejected("\0\0\xFE\xFF\0\0\0<"sv));        // UTF-32BE BOM
  EXPECT_TRUE(rejected("<\0r\0>\0"sv));                  // BOM-less UTF-16LE
  EXPECT_TRUE(rejected("\0<\0r\0>"sv));                  // BOM-less UTF-16BE
  EXPECT_TRUE(rejected(R"(<?xml version="1.0" encoding="UTF-16"?><r>x</r>)"sv));
  EXPECT_TRUE(rejected(R"(<?xml encoding='ISO-8859-1'?><r>x</r>)"sv));
}

/// The UTF-8 spellings the declaration may legally use must all be accepted,
/// as must a BOM, an absent declaration, and an absent encoding attribute.
TEST_F(Sec31Tags, AcceptsUtf8Declarations) {
  const auto accepted = [](std::string_view src) {
    xmlight::Parser p{src};
    Leaf leaf;
    return xmlight::deserialize(p, "r", leaf) && leaf.text == "x";
  };

  EXPECT_TRUE(accepted("\xEF\xBB\xBF<r><v>x</v></r>"));  // UTF-8 BOM
  EXPECT_TRUE(accepted("<r><v>x</v></r>"));
  EXPECT_TRUE(accepted(R"(<?xml version="1.0"?><r><v>x</v></r>)"));
  EXPECT_TRUE(accepted(R"(<?xml version="1.0" encoding="UTF-8"?><r><v>x</v></r>)"));
  EXPECT_TRUE(accepted(R"(<?xml version="1.0" encoding="utf-8"?><r><v>x</v></r>)"));
  EXPECT_TRUE(accepted(R"(<?xml version="1.0" encoding='US-ASCII'?><r><v>x</v></r>)"));
  // "encoding" appearing in content must not be mistaken for a declaration.
  EXPECT_TRUE(accepted(R"(<r><v>x</v><n>encoding="UTF-16"</n></r>)"));
}

/// Unclosed start tag - must fail.
TEST_F(Sec31Tags, UnclosedStartTag) {
  const std::string_view src = R"(<r x="v")";
  xmlight::Parser p{src};
  AttrOnly ao;
  EXPECT_FALSE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnclosedTag);
}

/// End-tag with no name - must fail.
TEST_F(Sec31Tags, EndTagNoName) {
  const std::string_view src = R"(<r></>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedNameInCloseTag);
}

/// Proper nesting is required - overlapping elements are ill-formed.
TEST_F(Sec31Tags, OverlappingElements) {
  const std::string_view src = R"(<r><a>text</r></a>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ElementMismatch);
}

// sec 3.1 - Namespace-prefixed elements
class Sec31Namespaces : public ::testing::Test {};

/// Elements with namespace prefixes should parse; the local name is
/// used for field matching.
TEST_F(Sec31Namespaces, PrefixedElementName) {
  const std::string_view src = R"(<ns:r xmlns:ns="urn:test"><v>ok</v></ns:r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  // LightningXML's begin_element compares the local name "r".
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Prefixed child elements - local name used for field matching.
TEST_F(Sec31Namespaces, PrefixedChildElement) {
  const std::string_view src = R"(<r><ns:v>ok</ns:v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Prefixed attributes - local name used for hash matching.
TEST_F(Sec31Namespaces, PrefixedAttribute) {
  const std::string_view src = R"(<r ns:x="hello" y="world"/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "hello");
  EXPECT_EQ(ao.y, "world");
}

// sec 4.1 - Character and Entity References [Production 66-68]
class Sec41References : public ::testing::Test {};

/// sec 4.6 - Predefined entities: &amp; &lt; &gt; &apos; &quot;
/// LightningXML does NOT expand entities (zero-copy). The raw text
/// including the ampersand and semicolon is preserved.
TEST_F(Sec41References, PredefinedEntitiesPreservedRaw) {
  const std::string_view src = R"(<r><v>a&amp;b &lt; c &gt; d &apos;e&apos; &quot;f&quot;</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  // Zero-copy: entities are NOT expanded.
  EXPECT_EQ(leaf.text, "a&amp;b &lt; c &gt; d &apos;e&apos; &quot;f&quot;");
}

/// Numeric character references (&#nnn; and &#xhh;) are also not
/// expanded in zero-copy mode.
TEST_F(Sec41References, NumericCharRefPreservedRaw) {
  const std::string_view src = R"(<r><v>&#65;&#x42;</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  // Raw text - not expanded to "AB".
  EXPECT_EQ(leaf.text, "&#65;&#x42;");
}

/// Entity references in attribute values - also preserved raw.
TEST_F(Sec41References, EntityRefInAttributeRaw) {
  const std::string_view src = R"(<r x="a&amp;b" y="c&lt;d"/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "a&amp;b");
  EXPECT_EQ(ao.y, "c&lt;d");
}

/// Owned strings (std::string) also preserve raw entity text.
TEST_F(Sec41References, OwnedStringPreservesEntities) {
  const std::string_view src = R"(<r><v>hello &amp; world</v></r>)";
  xmlight::Parser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "hello &amp; world");
}

/// sec 4.1 - NormalizingParser expands character references to UTF-8,
/// including two-byte (U+00E9) and four-byte (U+1F600) encodings.
TEST_F(Sec41References, CharRefMultiByteUtf8Expanded) {
  const std::string_view src = R"(<r><v>caf&#xE9; &#128512;</v></r>)";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "caf\xC3\xA9 \xF0\x9F\x98\x80");
}

/// Production [68]: a reference needs a terminating ';'.
TEST_F(Sec41References, UnterminatedReferenceFails) {
  const std::string_view src = R"(<r><v>a &amp b</v></r>)";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::InvalidCharRef);
}

/// "&;" names no entity.
TEST_F(Sec41References, EmptyReferenceFails) {
  const std::string_view src = R"(<r><v>a&;b</v></r>)";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UndefinedEntity);
}

/// A bad reference in a typed leaf surfaces the reference error, not a
/// numeric-parse error: the text is normalized before conversion.
TEST_F(Sec41References, BadReferenceInTypedLeafFails) {
  const std::string_view src = R"(<r><v>1&bogus;2</v></r>)";
  xmlight::NormalizingParser p{src};
  LeafInt leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UndefinedEntity);
}

/// Runs past the 32-byte memchr gate still get CR/CRLF folded in text and
/// whitespace collapsed in attribute values.
TEST_F(Sec41References, LongRunNormalization) {
  const std::string text_run = std::string(40, 'a') + "\r\n" + std::string(20, 'b') + "\rz";
  const std::string src = "<r><v>" + text_run + "</v></r>";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, std::string(40, 'a') + "\n" + std::string(20, 'b') + "\nz");

  const std::string attr_run =
      std::string(25, 'q') + "\t" + std::string(9, 'r') + "\n" + std::string(6, 's');
  const std::string src2 = R"(<r x=")" + attr_run + R"("/>)";
  xmlight::NormalizingParser p2{src2};
  OwnedAttr oa;
  ASSERT_TRUE(xmlight::deserialize(p2, "r", oa));
  EXPECT_EQ(oa.x, std::string(25, 'q') + " " + std::string(9, 'r') + " " + std::string(6, 's'));

  // A long ordinary run (no special byte at all), and a long run whose only
  // special is a CR/CRLF pair: attribute mode folds both to single spaces.
  const std::string src3 = R"(<r x=")" + std::string(40, 'c') + R"("/>)";
  xmlight::NormalizingParser p3{src3};
  OwnedAttr ob;
  ASSERT_TRUE(xmlight::deserialize(p3, "r", ob));
  EXPECT_EQ(ob.x, std::string(40, 'c'));

  const std::string src4 = R"(<r x=")" + std::string(40, 'd') + "\r\ne\r" + R"("/>)";
  xmlight::NormalizingParser p4{src4};
  OwnedAttr oc;
  ASSERT_TRUE(xmlight::deserialize(p4, "r", oc));
  EXPECT_EQ(oc.x, std::string(40, 'd') + " e ");
}

/// sec 2.7 + 3.3.3: CDATA content under the normalizing parser keeps '&'
/// literal (no reference expansion) but still folds line endings.
TEST_F(Sec41References, NormalizedCDataKeepsAmpFoldsEol) {
  const std::string src =
      "<r><v><![CDATA[" + std::string(30, 'a') + "&amp; stays\r\nliteral]]></v></r>";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, std::string(30, 'a') + "&amp; stays\nliteral");
}

/// Character references for whitespace controls expand to the literal bytes;
/// the expansion result is not re-normalized (a &#13; stays a carriage
/// return).
TEST_F(Sec41References, CharRefWhitespaceControls) {
  const std::string_view src = R"(<r><v>&#9;&#10;&#13;</v></r>)";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "\t\n\r");
}

/// Uppercase 'X' hex prefix is accepted alongside lowercase.
TEST_F(Sec41References, CharRefUppercaseHexPrefix) {
  const std::string_view src = R"(<r><v>&#X41;&#x42;</v></r>)";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "AB");
}

/// Production [2] Char: surrogates, out-of-range code points, forbidden
/// controls, and malformed digit strings are all fatal.
TEST_F(Sec41References, CharRefInvalidCodePointsFail) {
  for (const std::string_view bad : {"&#xD800;", "&#x110000;", "&#8;", "&#;", "&#x;", "&#12a4;"}) {
    const std::string src = "<r><v>" + std::string(bad) + "</v></r>";
    xmlight::NormalizingParser p{src};
    OwnedLeaf leaf;
    EXPECT_FALSE(xmlight::deserialize(p, "r", leaf)) << bad;
    EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::InvalidCharRef) << bad;
  }
}

/// U+10FFFF is the last legal code point.
TEST_F(Sec41References, CharRefMaxCodePoint) {
  const std::string_view src = R"(<r><v>&#x10FFFF;</v></r>)";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "\xF4\x8F\xBF\xBF");
}

/// A bad reference inside an attribute value of an owning field.
TEST_F(Sec41References, AttrOwnedBadReferenceFails) {
  const std::string_view src = R"(<r x="&bogus;"/>)";
  xmlight::NormalizingParser p{src};
  OwnedAttr oa;
  EXPECT_FALSE(xmlight::deserialize(p, "r", oa));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UndefinedEntity);
}

// sec 5 - Conformance / sec 5.1 - Processor Classification
class Sec5Conformance : public ::testing::Test {};

/// A non-validating processor MUST report violations of well-formedness
/// constraints as fatal errors (returning false / error token).
/// This test aggregates the minimal set of fatal-error scenarios.

/// Missing end tag for root - must fail.
TEST_F(Sec5Conformance, FatalError_MissingEndTag) {
  const std::string_view src = R"(<r><v>ok</v>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnexpectedEof);
}

/// Production [14]: '<' may only appear in content as the start of markup.
/// A char that cannot begin markup after '<' (here a space) is a fatal error.
TEST_F(Sec5Conformance, FatalError_BareLtInContent) {
  const std::string_view src = R"(<r><v>a < b</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnexpectedCharAfterLt);
}

/// Bare '&' that doesn't form a valid entity reference.
/// For a non-validating processor that doesn't expand entities, this
/// is technically an error but the spec allows non-validating processors
/// to not report it (sec 4.4.1). LightningXML's zero-copy pass-through is
/// compliant here.
TEST_F(Sec5Conformance, BareAmpersandPassedThrough) {
  const std::string_view src = R"(<r><v>a & b</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  // Zero-copy: raw text is preserved including the bare '&'.
  EXPECT_EQ(leaf.text, "a & b");
}

class WellFormedness : public ::testing::Test {};

/// Multiple root elements - only the first should be deserialized.
TEST_F(WellFormedness, TwoRootElements) {
  const std::string_view src = R"(<r><v>first</v></r><r><v>second</v></r>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "first");
}

/// Deeply nested elements should parse up to kMaxDepth.
TEST_F(WellFormedness, DeepNesting) {
  // Build a 128-level nested structure:
  // <r><inner><inner>...<v>ok</v>...</inner></inner></r>
  std::string src = "<r>";
  const int depth = 100;
  for (int i = 0; i < depth; ++i) {
    src += "<inner>";
  }
  src += "<v>ok</v>";
  for (int i = 0; i < depth; ++i) {
    src += "</inner>";
  }
  src += "</r>";
  xmlight::Parser p{src};
  Leaf leaf;
  // The parser will skip unknown "inner" elements, but it should not
  // crash on deep nesting.
  std::ignore = xmlight::deserialize(p, "r", leaf);
}

/// Interleaved CDATA, comments, PIs - all should be skipped cleanly.
TEST_F(WellFormedness, InterleavedMisc) {
  const std::string_view src = R"(
<?xml version="1.0"?>
<!-- top comment -->
<?style sheet="none"?>
<r>
  <!-- inner comment -->
  <![CDATA[some <data>]]>
  <?inner-pi stuff?>
  <v>ok</v>
  <!-- trailing comment -->
</r>
)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Parser::reset() should allow re-parsing the same document.
TEST_F(WellFormedness, ResetAndReparse) {
  const std::string_view src = R"(<r><v>hello</v></r>)";
  xmlight::Parser p{src};

  Leaf first;
  ASSERT_TRUE(xmlight::deserialize(p, "r", first));
  EXPECT_EQ(first.text, "hello");

  p.reset();

  Leaf second;
  ASSERT_TRUE(xmlight::deserialize(p, "r", second));
  EXPECT_EQ(second.text, "hello");
}

/// Vector field with zero children - empty vector, no error.
TEST_F(WellFormedness, EmptyVectorField) {
  const std::string_view src = R"(<r></r>)";
  xmlight::Parser p{src};
  VecLeaf vl;
  ASSERT_TRUE(xmlight::deserialize(p, "r", vl));
  EXPECT_TRUE(vl.items.empty());
}

/// Multiple children in a vector field.
TEST_F(WellFormedness, VectorMultipleChildren) {
  const std::string_view src = R"(<r><item>a</item><item>b</item><item>c</item></r>)";
  xmlight::Parser p{src};
  VecLeaf vl;
  ASSERT_TRUE(xmlight::deserialize(p, "r", vl));
  ASSERT_EQ(vl.items.size(), 3U);
  EXPECT_EQ(vl.items[0], "a");
  EXPECT_EQ(vl.items[1], "b");
  EXPECT_EQ(vl.items[2], "c");
}

/// Integer attribute parsing.
TEST_F(WellFormedness, IntegerAttribute) {
  const std::string_view src = R"(<r id="42" name="test"/>)";
  xmlight::Parser p{src};
  AttrInt ai;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ai));
  EXPECT_EQ(ai.id, 42);
  EXPECT_EQ(ai.name, "test");
}

/// Negative integer in element content.
TEST_F(WellFormedness, NegativeInteger) {
  const std::string_view src = R"(<r><v>-7</v></r>)";
  xmlight::Parser p{src};
  LeafInt li;
  ASSERT_TRUE(xmlight::deserialize(p, "r", li));
  EXPECT_EQ(li.value, -7);
}

/// Large document - many siblings.
TEST_F(WellFormedness, ManySiblings) {
  std::string src = "<r>";
  for (int i = 0; i < 10000; ++i) {
    src += "<item>" + std::to_string(i) + "</item>";
  }
  src += "</r>";
  xmlight::Parser p{src};
  VecLeaf vl;
  ASSERT_TRUE(xmlight::deserialize(p, "r", vl));
  EXPECT_EQ(vl.items.size(), 10000U);
  EXPECT_EQ(vl.items[0], "0");
  EXPECT_EQ(vl.items[9999], "9999");
}

/// Nested struct deserialization.
TEST_F(WellFormedness, NestedStruct) {
  const std::string_view src = R"(<r><inner><v>deep</v></inner></r>)";
  xmlight::Parser p{src};
  Nested n;
  ASSERT_TRUE(xmlight::deserialize(p, "r", n));
  EXPECT_EQ(n.inner.text, "deep");
}

/// Self-closing root with no fields to read - should succeed.
TEST_F(WellFormedness, SelfClosingRoot) {
  const std::string_view src = R"(<r/>)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_TRUE(leaf.text.empty());
}

// Attribute edge cases (sec 3.1 / sec 3.3)
class AttributeEdges : public ::testing::Test {};

/// Empty attribute value is legal.
TEST_F(AttributeEdges, EmptyAttributeValue) {
  const std::string_view src = R"(<r x="" y=""/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_TRUE(ao.x.empty());
  EXPECT_TRUE(ao.y.empty());
}

/// Attribute value containing whitespace.
TEST_F(AttributeEdges, WhitespaceInAttributeValue) {
  const std::string_view src = R"(<r x="  spaces  " y="	tab	"/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "  spaces  ");
  EXPECT_EQ(ao.y, "\ttab\t");
}

/// Attribute with numeric value parsed as string.
TEST_F(AttributeEdges, NumericAttrAsString) {
  const std::string_view src = R"(<r x="12345" y="0"/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "12345");
  EXPECT_EQ(ao.y, "0");
}

/// Attributes appear after unknown attributes - registered ones are
/// still found via hash lookup.
TEST_F(AttributeEdges, UnknownAttributesSkipped) {
  const std::string_view src = R"(<r foo="bar" x="found" baz="quux" y="also"/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "found");
  EXPECT_EQ(ao.y, "also");
}

class Robustness : public ::testing::Test {};

/// Truncated document mid-tag.
TEST_F(Robustness, TruncatedMidTag) {
  const std::string_view src = R"(<r><v>hel)";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnexpectedEof);
}

/// Truncated in attribute name.
TEST_F(Robustness, TruncatedInAttribute) {
  const std::string_view src = R"(<r x)";
  xmlight::Parser p{src};
  AttrOnly ao;
  EXPECT_FALSE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedEquals);
}

/// Truncated in attribute value (no closing quote).
TEST_F(Robustness, TruncatedInAttrValue) {
  const std::string_view src = R"(<r x="hello)";
  xmlight::Parser p{src};
  AttrOnly ao;
  EXPECT_FALSE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnterminatedAttributeValue);
}

/// Just a '<' - must not crash.
TEST_F(Robustness, JustLessThan) {
  const std::string_view src = "<";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UnexpectedEndAfterLt);
}

/// Just "</" - must not crash.
TEST_F(Robustness, JustCloseTagStart) {
  const std::string_view src = "</";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedNameInCloseTag);
}

/// Just "<!-" - partial comment start, must not crash.
TEST_F(Robustness, PartialCommentStart) {
  const std::string_view src = "<!-";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::RootElementNotFound);
}

/// Just "<?" - partial PI, must not crash.
TEST_F(Robustness, PartialPIStart) {
  const std::string_view src = "<?";
  xmlight::Parser p{src};
  Leaf leaf;
  EXPECT_FALSE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::ExpectedPiTarget);
}

/// Very long element name (64KB).
TEST_F(Robustness, VeryLongElementName) {
  const std::string name(65536, 'a');
  const std::string src = "<" + name + "><v>ok</v></" + name + ">";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, name, leaf));
  EXPECT_EQ(leaf.text, "ok");
}

/// Very long attribute value (1MB).
TEST_F(Robustness, VeryLongAttributeValue) {
  const std::string val(1 << 20, 'x');
  const std::string src = R"(<r x=")" + val + R"("/>)";
  xmlight::Parser p{src};
  AttrOnly ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x.size(), val.size());
}

/// Very long text content (1MB).
TEST_F(Robustness, VeryLongTextContent) {
  const std::string val(1 << 20, 'y');
  const std::string src = "<r><v>" + val + "</v></r>";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text.size(), val.size());
}

/// Random garbage after valid XML - doesn't affect the first parse.
TEST_F(Robustness, GarbageAfterDocument) {
  const std::string_view src = R"(<r><v>ok</v></r>@#$%^&*garbage)";
  xmlight::Parser p{src};
  Leaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "ok");
}

// Conformance areas closed by the NormalizingParser opt-in: entity/char-ref
// expansion and attribute-value normalization (secs 3.3.3, 4.1, 4.6).
class OptInConformance : public ::testing::Test {};

/// sec 3.3.3 - Attribute-value normalization. For CDATA-typed attributes
/// (the default without a DTD), processors replace character/entity references,
/// then normalize whitespace. Supported on the normalizing parser for owning
/// std::string fields (a zero-copy std::string_view cannot hold the result).
TEST_F(OptInConformance, AttrValueNormalization) {
  const std::string_view src = R"(<r x="a&#x20;&#x20;b"/>)";
  xmlight::NormalizingParser p{src};
  OwnedAttr ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, "a  b");
}

/// sec 4.6 - The predefined entities also expand inside attribute values.
TEST_F(OptInConformance, PredefinedEntitiesInAttributeValue) {
  const std::string_view src = R"(<r x="a&amp;b &quot;c&quot; &lt;d&gt; &apos;e&apos;"/>)";
  xmlight::NormalizingParser p{src};
  OwnedAttr ao;
  ASSERT_TRUE(xmlight::deserialize(p, "r", ao));
  EXPECT_EQ(ao.x, R"(a&b "c" <d> 'e')");
}

/// sec 4.6 - Predefined entity expansion. A conforming processor MUST
/// recognize &lt; &gt; &amp; &apos; &quot; and expand them.
TEST_F(OptInConformance, PredefinedEntityExpansion) {
  const std::string_view src = R"(<r><v>&lt;hello&gt;</v></r>)";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "<hello>");
}

/// sec 4.1 - Character reference expansion (&#nnn; / &#xhh;).
TEST_F(OptInConformance, CharRefExpansion) {
  const std::string_view src = R"(<r><v>&#65;</v></r>)";
  xmlight::NormalizingParser p{src};
  OwnedLeaf leaf;
  ASSERT_TRUE(xmlight::deserialize(p, "r", leaf));
  EXPECT_EQ(leaf.text, "A");
}

struct NormList {
  std::vector<std::string> words;
  std::vector<int> nums;
};
template<>
struct xmlight::XmlMetadata<NormList> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("nums", &NormList::nums),
                                                 xmlight::listField("words", &NormList::words));
};

struct RawViewList {
  std::vector<std::string_view> words;
};
template<>
struct xmlight::XmlMetadata<RawViewList> {
  static constexpr auto fields = std::make_tuple(xmlight::listField("words", &RawViewList::words));
};

/// sec 4.4/3.3.3 - References inside an xs:list element value are expanded
/// before whitespace splitting on the normalizing parser.
TEST_F(OptInConformance, ListElementValueIsNormalized) {
  const std::string_view src = "<r nums='1 2'><words>&#65;B c&amp;d</words></r>";
  xmlight::NormalizingParser p{src};
  NormList list;
  ASSERT_TRUE(xmlight::deserialize(p, "r", list));
  ASSERT_EQ(list.words.size(), 2U);
  EXPECT_EQ(list.words[0], "AB");
  EXPECT_EQ(list.words[1], "c&d");
}

/// sec 3.3.3 - References inside a list-valued attribute are expanded before
/// splitting; character references can even form the list items.
TEST_F(OptInConformance, ListAttributeValueIsNormalized) {
  const std::string_view src = "<r nums='1 &#50; 3'><words>x</words></r>";
  xmlight::NormalizingParser p{src};
  NormList list;
  ASSERT_TRUE(xmlight::deserialize(p, "r", list));
  ASSERT_EQ(list.nums.size(), 3U);
  EXPECT_EQ(list.nums[1], 2);
}

/// string_view list items stay raw zero-copy even on the normalizing parser,
/// matching the string-field contract (a view cannot hold transformed bytes).
TEST_F(OptInConformance, ListOfViewsStaysRaw) {
  const std::string_view src = "<r><words>a&amp;b c</words></r>";
  xmlight::NormalizingParser p{src};
  RawViewList list;
  ASSERT_TRUE(xmlight::deserialize(p, "r", list));
  ASSERT_EQ(list.words.size(), 2U);
  EXPECT_EQ(list.words[0], "a&amp;b");
}

/// A bad reference inside a list value is a fatal error on the normalizing
/// parser, consistent with string-field normalization.
TEST_F(OptInConformance, ListValueBadReferenceFails) {
  const std::string_view src = "<r><words>a &undefined; b</words></r>";
  xmlight::NormalizingParser p{src};
  NormList list;
  EXPECT_FALSE(xmlight::deserialize(p, "r", list));
  EXPECT_EQ(p.errorCode(), xmlight::ErrorCode::UndefinedEntity);
}

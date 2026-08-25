/// @file test_UCI.cc
/// @brief Deserializes, validates, and round-trips OMS UCI v2.5 messages from
/// the header xsdgen generates for the published schema. A compile failure here
/// means the generator emitted invalid C++ for the full 4,612-type message set;
/// a test failure means the mapping does not match the schema.
#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <variant>

#include "uci_generated.hh"

namespace {

// Minimal schema-valid instances: every element below is required by the
// schema (minOccurs >= 1) unless a comment says otherwise. UCI declares
// elementFormDefault="qualified", so real traffic is namespace-prefixed;
// LightningXML matches on the local name and ignores the prefix.
constexpr std::string_view SUBSYSTEM_STATUS =
    R"(<uci:SubsystemStatus xmlns:uci="https://www.vdl.afrl.af.mil/programs/oam">
  <uci:SecurityInformation>
    <uci:Classification>U</uci:Classification>
    <uci:OwnerProducer>
      <uci:GovernmentIdentifier>USA</uci:GovernmentIdentifier>
    </uci:OwnerProducer>
  </uci:SecurityInformation>
  <uci:MessageHeader>
    <uci:SystemID>
      <uci:UUID>8f1a2b3c-4d5e-4f60-8a9b-0c1d2e3f4a5b</uci:UUID>
    </uci:SystemID>
    <uci:Timestamp>2026-08-25T12:00:00Z</uci:Timestamp>
    <uci:SchemaVersion>002.5.0</uci:SchemaVersion>
    <uci:Mode>LIVE</uci:Mode>
  </uci:MessageHeader>
  <uci:MessageData>
    <uci:SubsystemID>
      <uci:UUID>1c9e6679-7425-40de-944b-e07fc1f90ae7</uci:UUID>
    </uci:SubsystemID>
    <uci:SubsystemState>OPERATE</uci:SubsystemState>
    <uci:About>
      <uci:Model>LXML-RADAR-1</uci:Model>
      <uci:SerialNumber>SN-0007</uci:SerialNumber>
    </uci:About>
  </uci:MessageData>
</uci:SubsystemStatus>)";

// The same message with no prefixes and a default namespace.
constexpr std::string_view SUBSYSTEM_STATUS_UNPREFIXED =
    R"(<SubsystemStatus xmlns="https://www.vdl.afrl.af.mil/programs/oam">
  <SecurityInformation>
    <Classification>U</Classification>
    <OwnerProducer>
      <GovernmentIdentifier>USA</GovernmentIdentifier>
    </OwnerProducer>
  </SecurityInformation>
  <MessageHeader>
    <SystemID>
      <UUID>8f1a2b3c-4d5e-4f60-8a9b-0c1d2e3f4a5b</UUID>
    </SystemID>
    <Timestamp>2026-08-25T12:00:00Z</Timestamp>
    <SchemaVersion>002.5.0</SchemaVersion>
    <Mode>LIVE</Mode>
  </MessageHeader>
  <MessageData>
    <SubsystemID>
      <UUID>1c9e6679-7425-40de-944b-e07fc1f90ae7</UUID>
    </SubsystemID>
    <SubsystemState>OPERATE</SubsystemState>
    <About>
      <Model>LXML-RADAR-1</Model>
      <SerialNumber>SN-0007</SerialNumber>
    </About>
  </MessageData>
</SubsystemStatus>)";

// SubsystemID and CapabilityID repeat (maxOccurs="unbounded").
constexpr std::string_view SYSTEM_STATUS =
    R"(<uci:SystemStatus xmlns:uci="https://www.vdl.afrl.af.mil/programs/oam">
  <uci:SecurityInformation>
    <uci:Classification>U</uci:Classification>
    <uci:OwnerProducer>
      <uci:GovernmentIdentifier>USA</uci:GovernmentIdentifier>
    </uci:OwnerProducer>
  </uci:SecurityInformation>
  <uci:MessageHeader>
    <uci:SystemID>
      <uci:UUID>8f1a2b3c-4d5e-4f60-8a9b-0c1d2e3f4a5b</uci:UUID>
    </uci:SystemID>
    <uci:Timestamp>2026-08-25T12:00:00Z</uci:Timestamp>
    <uci:SchemaVersion>002.5.0</uci:SchemaVersion>
    <uci:Mode>EXERCISE</uci:Mode>
  </uci:MessageHeader>
  <uci:MessageData>
    <uci:SystemID>
      <uci:UUID>8f1a2b3c-4d5e-4f60-8a9b-0c1d2e3f4a5b</uci:UUID>
    </uci:SystemID>
    <uci:SystemState>DEGRADED</uci:SystemState>
    <uci:Source>ACTUAL</uci:Source>
    <uci:Model>LXML-AIRCRAFT</uci:Model>
    <uci:SubsystemID>
      <uci:UUID>1c9e6679-7425-40de-944b-e07fc1f90ae7</uci:UUID>
    </uci:SubsystemID>
    <uci:SubsystemID>
      <uci:UUID>2c9e6679-7425-40de-944b-e07fc1f90ae7</uci:UUID>
      <uci:DescriptiveLabel>EO/IR</uci:DescriptiveLabel>
    </uci:SubsystemID>
  </uci:MessageData>
</uci:SystemStatus>)";

// StrikeCapabilityMDT extends CapabilityBaseType and has no required content,
// so MessageData is legitimately empty here.
constexpr std::string_view STRIKE_CAPABILITY =
    R"(<uci:StrikeCapability xmlns:uci="https://www.vdl.afrl.af.mil/programs/oam">
  <uci:SecurityInformation>
    <uci:Classification>U</uci:Classification>
    <uci:OwnerProducer>
      <uci:GovernmentIdentifier>USA</uci:GovernmentIdentifier>
    </uci:OwnerProducer>
  </uci:SecurityInformation>
  <uci:MessageHeader>
    <uci:SystemID>
      <uci:UUID>8f1a2b3c-4d5e-4f60-8a9b-0c1d2e3f4a5b</uci:UUID>
    </uci:SystemID>
    <uci:Timestamp>2026-08-25T12:00:00Z</uci:Timestamp>
    <uci:SchemaVersion>002.5.0</uci:SchemaVersion>
    <uci:Mode>LIVE</uci:Mode>
  </uci:MessageHeader>
  <uci:ObjectState>NEW</uci:ObjectState>
  <uci:MessageData/>
</uci:StrikeCapability>)";

// Command is an xs:choice; the Capability branch is taken here.
constexpr std::string_view ACTION_COMMAND =
    R"(<uci:ActionCommand xmlns:uci="https://www.vdl.afrl.af.mil/programs/oam">
  <uci:SecurityInformation>
    <uci:Classification>U</uci:Classification>
    <uci:OwnerProducer>
      <uci:GovernmentIdentifier>USA</uci:GovernmentIdentifier>
    </uci:OwnerProducer>
  </uci:SecurityInformation>
  <uci:MessageHeader>
    <uci:SystemID>
      <uci:UUID>8f1a2b3c-4d5e-4f60-8a9b-0c1d2e3f4a5b</uci:UUID>
    </uci:SystemID>
    <uci:Timestamp>2026-08-25T12:00:00Z</uci:Timestamp>
    <uci:SchemaVersion>002.5.0</uci:SchemaVersion>
    <uci:Mode>LIVE</uci:Mode>
  </uci:MessageHeader>
  <uci:MessageData>
    <uci:Command>
      <uci:Capability>
        <uci:CommandID>
          <uci:UUID>3c9e6679-7425-40de-944b-e07fc1f90ae7</uci:UUID>
        </uci:CommandID>
        <uci:CommandState>NEW</uci:CommandState>
        <uci:CapabilityID>
          <uci:UUID>4c9e6679-7425-40de-944b-e07fc1f90ae7</uci:UUID>
        </uci:CapabilityID>
        <uci:Ranking>
          <uci:Rank>
            <uci:Priority>3</uci:Priority>
          </uci:Rank>
        </uci:Ranking>
        <uci:ActionID>
          <uci:UUID>5c9e6679-7425-40de-944b-e07fc1f90ae7</uci:UUID>
        </uci:ActionID>
      </uci:Capability>
    </uci:Command>
  </uci:MessageData>
</uci:ActionCommand>)";

constexpr std::string_view ENTITY =
    R"(<uci:Entity xmlns:uci="https://www.vdl.afrl.af.mil/programs/oam">
  <uci:SecurityInformation>
    <uci:Classification>U</uci:Classification>
    <uci:OwnerProducer>
      <uci:GovernmentIdentifier>USA</uci:GovernmentIdentifier>
    </uci:OwnerProducer>
  </uci:SecurityInformation>
  <uci:MessageHeader>
    <uci:SystemID>
      <uci:UUID>8f1a2b3c-4d5e-4f60-8a9b-0c1d2e3f4a5b</uci:UUID>
    </uci:SystemID>
    <uci:Timestamp>2026-08-25T12:00:00Z</uci:Timestamp>
    <uci:SchemaVersion>002.5.0</uci:SchemaVersion>
    <uci:Mode>SIMULATION</uci:Mode>
  </uci:MessageHeader>
  <uci:MessageData>
    <uci:EntityID>
      <uci:UUID>6c9e6679-7425-40de-944b-e07fc1f90ae7</uci:UUID>
    </uci:EntityID>
    <uci:CreationTimestamp>
      <uci:DateTime>2026-08-25T11:59:30Z</uci:DateTime>
    </uci:CreationTimestamp>
    <uci:Source>
      <uci:SystemID>
        <uci:UUID>8f1a2b3c-4d5e-4f60-8a9b-0c1d2e3f4a5b</uci:UUID>
      </uci:SystemID>
      <uci:SourceType>FUSION_SYSTEM</uci:SourceType>
    </uci:Source>
    <uci:EntityStatus>CONFIRMED</uci:EntityStatus>
    <uci:Identity>
      <uci:IdentityTimestamp>2026-08-25T12:00:00Z</uci:IdentityTimestamp>
    </uci:Identity>
  </uci:MessageData>
</uci:Entity>)";

template<typename ParserT, typename T>
auto read(const std::string_view doc, const std::string_view root) -> T {
  ParserT parser{doc};
  T msg;
  EXPECT_TRUE(xmlight::deserialize(parser, root, msg))
      << root << ": error " << static_cast<int>(parser.errorCode());
  return msg;
}

template<typename T>
auto parse(const std::string_view doc, const std::string_view root) -> T {
  return read<xmlight::Parser, T>(doc, root);
}

template<typename T>
auto parseStrict(const std::string_view doc, const std::string_view root) -> T {
  return read<xmlight::StrictParser, T>(doc, root);
}

auto subsystemStatus() -> SubsystemStatusMT {
  return parse<SubsystemStatusMT>(SUBSYSTEM_STATUS, "SubsystemStatus");
}

}  // namespace

TEST(UciMessages, SubsystemStatusDeserializes) {
  const SubsystemStatusMT msg = subsystemStatus();
  EXPECT_EQ(msg.SecurityInformation.Classification, ClassificationEnum::U);
  EXPECT_EQ(msg.MessageHeader.SystemID.UUID, "8f1a2b3c-4d5e-4f60-8a9b-0c1d2e3f4a5b");
  EXPECT_EQ(msg.MessageHeader.SchemaVersion, "002.5.0");
  EXPECT_EQ(msg.MessageHeader.Mode, MessageModeEnum::LIVE);
  EXPECT_EQ(msg.MessageData.SubsystemState, SubsystemStateEnum::OPERATE);
  EXPECT_EQ(msg.MessageData.About.Model, "LXML-RADAR-1");
  ASSERT_TRUE(msg.MessageData.About.SerialNumber.has_value());
  EXPECT_EQ(*msg.MessageData.About.SerialNumber, "SN-0007");
}

// SecurityInformation and MessageHeader are inherited from MessageType; the
// generator flattens the base's fields into the derived type's metadata.
TEST(UciMessages, InheritedMessageFieldsArePopulated) {
  const SubsystemStatusMT msg = subsystemStatus();
  const MessageType& base = msg;
  EXPECT_EQ(base.SecurityInformation.Classification, ClassificationEnum::U);
  EXPECT_EQ(base.MessageHeader.Mode, MessageModeEnum::LIVE);
}

// Prefixes are split off and never resolved, so the two spellings of the same
// message bind to the same fields.
TEST(UciMessages, PrefixedAndUnprefixedDocumentsAgree) {
  const SubsystemStatusMT prefixed = subsystemStatus();
  const SubsystemStatusMT plain =
      parse<SubsystemStatusMT>(SUBSYSTEM_STATUS_UNPREFIXED, "SubsystemStatus");
  EXPECT_EQ(plain.MessageData.SubsystemID.UUID, prefixed.MessageData.SubsystemID.UUID);
  EXPECT_EQ(plain.MessageData.SubsystemState, prefixed.MessageData.SubsystemState);
  EXPECT_EQ(plain.MessageHeader.Timestamp, prefixed.MessageHeader.Timestamp);
}

// OwnerProducer is an xs:choice of an enumeration and a NATO special word.
TEST(UciMessages, SecurityMarkingChoiceSelectsTheEnumeratedBranch) {
  const SubsystemStatusMT msg = subsystemStatus();
  ASSERT_EQ(msg.SecurityInformation.OwnerProducer.size(), 1U);
  const auto& owner = msg.SecurityInformation.OwnerProducer.front().choice;
  ASSERT_TRUE(std::holds_alternative<OwnerProducerEnum>(owner));
  EXPECT_EQ(std::get<OwnerProducerEnum>(owner), OwnerProducerEnum::USA);
}

TEST(UciMessages, TimestampParsesAsDateTime) {
  const SubsystemStatusMT msg = subsystemStatus();
  const xmlight::DateTime& stamp = msg.MessageHeader.Timestamp;
  EXPECT_EQ(stamp.date.year, 2026);
  EXPECT_EQ(stamp.date.month, 8);
  EXPECT_EQ(stamp.date.day, 25);
  EXPECT_EQ(stamp.time.hour, 12);
  EXPECT_TRUE(stamp.time.tz.has_value());
}

TEST(UciMessages, SystemStatusRepeatedElementsFillVectors) {
  const SystemStatusMT msg = parse<SystemStatusMT>(SYSTEM_STATUS, "SystemStatus");
  EXPECT_EQ(msg.MessageHeader.Mode, MessageModeEnum::EXERCISE);
  EXPECT_EQ(msg.MessageData.SystemState, SystemStateEnum::DEGRADED);
  EXPECT_EQ(msg.MessageData.Source, SystemSourceEnum::ACTUAL);
  ASSERT_EQ(msg.MessageData.SubsystemID.size(), 2U);
  EXPECT_EQ(msg.MessageData.SubsystemID[0].UUID, "1c9e6679-7425-40de-944b-e07fc1f90ae7");
  ASSERT_TRUE(msg.MessageData.SubsystemID[1].DescriptiveLabel.has_value());
  EXPECT_EQ(*msg.MessageData.SubsystemID[1].DescriptiveLabel, "EO/IR");
  EXPECT_TRUE(msg.MessageData.CapabilityID.empty());
}

// A self-closing element satisfies a required struct member with no required
// content of its own.
TEST(UciMessages, StrikeCapabilityAcceptsEmptyMessageData) {
  const StrikeCapabilityMT msg = parse<StrikeCapabilityMT>(STRIKE_CAPABILITY, "StrikeCapability");
  ASSERT_TRUE(msg.ObjectState.has_value());
  EXPECT_EQ(*msg.ObjectState, ObjectStateEnum::NEW);
  EXPECT_TRUE(msg.MessageData.Capability.empty());
}

TEST(UciMessages, ActionCommandChoiceSelectsTheCapabilityBranch) {
  const ActionCommandMT msg = parse<ActionCommandMT>(ACTION_COMMAND, "ActionCommand");
  ASSERT_EQ(msg.MessageData.Command.size(), 1U);
  const auto& command = msg.MessageData.Command.front().choice;
  ASSERT_TRUE(std::holds_alternative<ActionCapabilityCommandType>(command));
  const ActionCapabilityCommandType& capability = std::get<ActionCapabilityCommandType>(command);
  EXPECT_EQ(capability.CommandID.UUID, "3c9e6679-7425-40de-944b-e07fc1f90ae7");
  EXPECT_EQ(capability.ActionID.UUID, "5c9e6679-7425-40de-944b-e07fc1f90ae7");
}

TEST(UciMessages, EntityDeserializes) {
  const EntityMT msg = parse<EntityMT>(ENTITY, "Entity");
  EXPECT_EQ(msg.MessageHeader.Mode, MessageModeEnum::SIMULATION);
  EXPECT_EQ(msg.MessageData.EntityID.UUID, "6c9e6679-7425-40de-944b-e07fc1f90ae7");
  EXPECT_EQ(msg.MessageData.EntityStatus, EntityStatusEnum::CONFIRMED);
  EXPECT_EQ(msg.MessageData.Source.SourceType, EntitySourceEnum::FUSION_SYSTEM);
  EXPECT_EQ(msg.MessageData.CreationTimestamp.DateTime.time.minute, 59);
  EXPECT_EQ(msg.MessageData.Identity.IdentityTimestamp.time.hour, 12);
}

// The strict tier adds the well-formedness scans and normalizes owning strings.
TEST(UciMessages, StrictParserReadsEveryDocument) {
  EXPECT_EQ(
      parseStrict<SubsystemStatusMT>(SUBSYSTEM_STATUS, "SubsystemStatus").MessageData.About.Model,
      "LXML-RADAR-1");
  EXPECT_EQ(parseStrict<SystemStatusMT>(SYSTEM_STATUS, "SystemStatus").MessageData.SystemState,
            SystemStateEnum::DEGRADED);
  EXPECT_TRUE(parseStrict<StrikeCapabilityMT>(STRIKE_CAPABILITY, "StrikeCapability")
                  .ObjectState.has_value());
  EXPECT_EQ(
      parseStrict<ActionCommandMT>(ACTION_COMMAND, "ActionCommand").MessageData.Command.size(), 1U);
  EXPECT_EQ(parseStrict<EntityMT>(ENTITY, "Entity").MessageData.EntityStatus,
            EntityStatusEnum::CONFIRMED);
}

TEST(UciMessages, MissingRequiredElementFails) {
  constexpr std::string_view no_header =
      R"(<uci:SubsystemStatus xmlns:uci="https://www.vdl.afrl.af.mil/programs/oam">
  <uci:SecurityInformation>
    <uci:Classification>U</uci:Classification>
  </uci:SecurityInformation>
</uci:SubsystemStatus>)";
  xmlight::Parser parser{no_header};
  SubsystemStatusMT msg;
  EXPECT_FALSE(xmlight::deserialize(parser, "SubsystemStatus", msg));
  EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::MissingRequiredField);
}

TEST(UciMessages, UnknownEnumTokenFails) {
  constexpr std::string_view bad_state =
      R"(<uci:SubsystemStatus xmlns:uci="https://www.vdl.afrl.af.mil/programs/oam">
  <uci:MessageData>
    <uci:SubsystemState>SPINNING_UP</uci:SubsystemState>
  </uci:MessageData>
</uci:SubsystemStatus>)";
  xmlight::Parser parser{bad_state};
  SubsystemStatusMT msg;
  EXPECT_FALSE(xmlight::deserialize(parser, "SubsystemStatus", msg));
  EXPECT_EQ(parser.errorCode(), xmlight::ErrorCode::InvalidEnumValue);
}

TEST(UciMessages, ValidatePassesOnEveryDocument) {
  EXPECT_FALSE(xmlight::validate(subsystemStatus()).has_value());
  EXPECT_FALSE(xmlight::validate(parse<SystemStatusMT>(SYSTEM_STATUS, "SystemStatus")).has_value());
  EXPECT_FALSE(xmlight::validate(parse<EntityMT>(ENTITY, "Entity")).has_value());
}

// UniversallyUniqueIdentifierType carries length="36" and a UUID pattern.
TEST(UciMessages, ValidateRejectsAMalformedUuid) {
  SubsystemStatusMT msg = subsystemStatus();
  msg.MessageData.SubsystemID.UUID = "not-a-uuid";
  const auto err = xmlight::validate(msg);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->message.find("UUID"), std::string::npos) << err->message;
}

// VisibleString32Type caps Model at 32 characters.
TEST(UciMessages, ValidateRejectsAnOverlongModel) {
  SubsystemStatusMT msg = subsystemStatus();
  msg.MessageData.About.Model = std::string(33, 'x');
  const auto err = xmlight::validate(msg);
  ASSERT_TRUE(err.has_value());
  EXPECT_NE(err->message.find("Model"), std::string::npos) << err->message;
}

TEST(UciMessages, ValidateReachesIntoAVariantBranch) {
  ActionCommandMT msg = parse<ActionCommandMT>(ACTION_COMMAND, "ActionCommand");
  auto& capability = std::get<ActionCapabilityCommandType>(msg.MessageData.Command.front().choice);
  capability.CommandID.UUID = "short";
  EXPECT_TRUE(xmlight::validate(msg).has_value());
}

// Serializing drops the namespace prefixes, so the round trip is compared on
// the deserialized values rather than the bytes.
TEST(UciMessages, RoundTripsThroughTheSerializer) {
  const SubsystemStatusMT original = subsystemStatus();
  const std::string xml = xmlight::serialize("SubsystemStatus", original);
  xmlight::Parser parser{xml};
  SubsystemStatusMT again;
  ASSERT_TRUE(xmlight::deserialize(parser, "SubsystemStatus", again))
      << static_cast<int>(parser.errorCode()) << '\n'
      << xml;
  EXPECT_EQ(again.SecurityInformation.Classification, original.SecurityInformation.Classification);
  EXPECT_EQ(again.MessageHeader.SchemaVersion, original.MessageHeader.SchemaVersion);
  EXPECT_EQ(again.MessageHeader.Timestamp, original.MessageHeader.Timestamp);
  EXPECT_EQ(again.MessageData.SubsystemID.UUID, original.MessageData.SubsystemID.UUID);
  EXPECT_EQ(again.MessageData.SubsystemState, original.MessageData.SubsystemState);
  EXPECT_EQ(again.MessageData.About.Model, original.MessageData.About.Model);
}

TEST(UciMessages, RoundTripKeepsRepeatedElements) {
  const SystemStatusMT original = parse<SystemStatusMT>(SYSTEM_STATUS, "SystemStatus");
  const std::string xml = xmlight::serialize("SystemStatus", original);
  xmlight::Parser parser{xml};
  SystemStatusMT again;
  ASSERT_TRUE(xmlight::deserialize(parser, "SystemStatus", again))
      << static_cast<int>(parser.errorCode());
  ASSERT_EQ(again.MessageData.SubsystemID.size(), 2U);
  EXPECT_EQ(again.MessageData.SubsystemID[1].UUID, original.MessageData.SubsystemID[1].UUID);
}

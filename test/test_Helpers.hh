#pragma once
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "LightningXML.hh"

struct Skills {
  std::vector<std::string_view> items;
};

template<>
struct xmlight::XmlMetadata<Skills> {
  static constexpr auto fields = std::make_tuple(xmlight::vecField("Skill", &Skills::items));
};

struct OrgMember {
  int id{0};
  std::string_view role;
  std::string_view full_name;
  std::string_view email;
  Skills skills;
};

template<>
struct xmlight::XmlMetadata<OrgMember> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("id", &OrgMember::id),
                                                 xmlight::attrField("role", &OrgMember::role),
                                                 xmlight::field("FullName", &OrgMember::full_name),
                                                 xmlight::field("Email", &OrgMember::email),
                                                 xmlight::field("Skills", &OrgMember::skills));
};

struct OrgTeam {
  int id{0};
  std::string_view name;
  std::vector<OrgMember> members;
};

template<>
struct xmlight::XmlMetadata<OrgTeam> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("id", &OrgTeam::id), xmlight::attrField("name", &OrgTeam::name),
                      xmlight::vecField("Member", &OrgTeam::members));
};

struct OrgDepartment {
  int id{0};
  std::string_view name;
  std::vector<OrgTeam> teams;
};

template<>
struct xmlight::XmlMetadata<OrgDepartment> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("id", &OrgDepartment::id),
                                                 xmlight::attrField("name", &OrgDepartment::name),
                                                 xmlight::vecField("Team", &OrgDepartment::teams));
};

struct Organization {
  int id{0};
  std::string_view name;
  std::vector<OrgDepartment> departments;
};

template<>
struct xmlight::XmlMetadata<Organization> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("id", &Organization::id),
                      xmlight::attrField("name", &Organization::name),
                      xmlight::vecField("Department", &Organization::departments));
};

struct User {
  int id{0};
  std::string_view name;
  std::string_view email;
};

template<>
struct xmlight::XmlMetadata<User> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("id", &User::id), xmlight::field("Name", &User::name),
                      xmlight::field("Email", &User::email));
};

struct Users {
  std::vector<User> items;
};

template<>
struct xmlight::XmlMetadata<Users> {
  static constexpr auto fields = std::make_tuple(xmlight::vecField("User", &Users::items));
};

struct Address {
  std::string_view street;
  int zip{};
};

template<>
struct xmlight::XmlMetadata<Address> {
  static constexpr auto fields =
      std::make_tuple(xmlight::field("street", &Address::street), xmlight::field("zip", &Address::zip));
};

struct Person {
  std::string_view name;
  int age{};
  Address address;
};

template<>
struct xmlight::XmlMetadata<Person> {
  static constexpr auto fields =
      std::make_tuple(xmlight::field("name", &Person::name), xmlight::field("age", &Person::age),
                      xmlight::field("address", &Person::address));
};

struct FlatItem {
  int id{};
  std::string_view title;
  std::string_view description;
  int status{};
};

template<>
struct xmlight::XmlMetadata<FlatItem> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("id", &FlatItem::id), xmlight::field("title", &FlatItem::title),
                      xmlight::field("desc", &FlatItem::description),
                      xmlight::field("status", &FlatItem::status));
};

struct FlatList {
  std::vector<FlatItem> items;
};

template<>
struct xmlight::XmlMetadata<FlatList> {
  static constexpr auto fields = std::make_tuple(xmlight::vecField("Item", &FlatList::items));
};

struct DeepL5 {
  int value{};
};
template<>
struct xmlight::XmlMetadata<DeepL5> {
  static constexpr auto fields = std::make_tuple(xmlight::field("v", &DeepL5::value));
};

struct DeepL4 {
  DeepL5 next;
};
template<>
struct xmlight::XmlMetadata<DeepL4> {
  static constexpr auto fields = std::make_tuple(xmlight::field("L5", &DeepL4::next));
};

struct DeepL3 {
  DeepL4 next;
};
template<>
struct xmlight::XmlMetadata<DeepL3> {
  static constexpr auto fields = std::make_tuple(xmlight::field("L4", &DeepL3::next));
};

struct DeepL2 {
  DeepL3 next;
};
template<>
struct xmlight::XmlMetadata<DeepL2> {
  static constexpr auto fields = std::make_tuple(xmlight::field("L3", &DeepL2::next));
};

struct DeepL1 {
  DeepL2 next;
};
template<>
struct xmlight::XmlMetadata<DeepL1> {
  static constexpr auto fields = std::make_tuple(xmlight::field("L2", &DeepL1::next));
};

struct DeepList {
  std::vector<DeepL1> items;
};
template<>
struct xmlight::XmlMetadata<DeepList> {
  static constexpr auto fields = std::make_tuple(xmlight::vecField("L1", &DeepList::items));
};

struct AttrItem {
  int a1{}, a2{}, a3{}, a4{}, a5{};
  std::string_view s1, s2, s3, s4, s5;
};

template<>
struct xmlight::XmlMetadata<AttrItem> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("a1", &AttrItem::a1), xmlight::attrField("a2", &AttrItem::a2),
                      xmlight::attrField("a3", &AttrItem::a3), xmlight::attrField("a4", &AttrItem::a4),
                      xmlight::attrField("a5", &AttrItem::a5), xmlight::attrField("s1", &AttrItem::s1),
                      xmlight::attrField("s2", &AttrItem::s2), xmlight::attrField("s3", &AttrItem::s3),
                      xmlight::attrField("s4", &AttrItem::s4), xmlight::attrField("s5", &AttrItem::s5));
};

struct AttrList {
  std::vector<AttrItem> items;
};

template<>
struct xmlight::XmlMetadata<AttrList> {
  static constexpr auto fields = std::make_tuple(xmlight::vecField("Item", &AttrList::items));
};

struct TreeNode {
  std::vector<TreeNode> children;
};

template<>
struct xmlight::XmlMetadata<TreeNode> {
  static constexpr auto fields = std::make_tuple(xmlight::vecField("Node", &TreeNode::children));
};

struct OwnedPerson {
  std::string name;
  int age{};
  std::string email;
};

template<>
struct xmlight::XmlMetadata<OwnedPerson> {
  static constexpr auto fields =
      std::make_tuple(xmlight::field("name", &OwnedPerson::name), xmlight::field("age", &OwnedPerson::age),
                      xmlight::field("email", &OwnedPerson::email));
};

struct OwnedUser {
  int id{};
  std::string role;
  std::string name;
};

template<>
struct xmlight::XmlMetadata<OwnedUser> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("id", &OwnedUser::id),
                                                 xmlight::attrField("role", &OwnedUser::role),
                                                 xmlight::field("Name", &OwnedUser::name));
};

struct OwnedList {
  std::vector<std::string> tags;
};

template<>
struct xmlight::XmlMetadata<OwnedList> {
  static constexpr auto fields = std::make_tuple(xmlight::vecField("Tag", &OwnedList::tags));
};

struct Book {
  std::string id;
  std::string author;
  std::string title;
  std::string genre;
  std::string price;
  std::string publish_date;
  std::string description;
};

template<>
struct xmlight::XmlMetadata<Book> {
  static constexpr auto fields =
      std::make_tuple(xmlight::attrField("id", &Book::id), xmlight::field("author", &Book::author),
                      xmlight::field("title", &Book::title), xmlight::field("genre", &Book::genre),
                      xmlight::field("price", &Book::price),
                      xmlight::field("publish_date", &Book::publish_date),
                      xmlight::field("description", &Book::description));
};

struct Catalog {
  std::vector<Book> books;
};

template<>
struct xmlight::XmlMetadata<Catalog> {
  static constexpr auto fields = std::make_tuple(xmlight::vecField("book", &Catalog::books));
};

struct FixedSkills {
  std::array<std::string_view, 3> items{};
};

template<>
struct xmlight::XmlMetadata<FixedSkills> {
  static constexpr auto fields = std::make_tuple(xmlight::arrField("Skill", &FixedSkills::items));
};

struct Toggle {
  bool enabled{};
  bool active{};
  bool verbose{};
};

template<>
struct xmlight::XmlMetadata<Toggle> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("enabled", &Toggle::enabled),
                                                 xmlight::field("active", &Toggle::active),
                                                 xmlight::field("verbose", &Toggle::verbose));
};

struct MixedRecord {
  int id{};
  std::string_view name;
  std::array<int, 4> scores{};
};

template<>
struct xmlight::XmlMetadata<MixedRecord> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("id", &MixedRecord::id),
                                                 xmlight::field("Name", &MixedRecord::name),
                                                 xmlight::arrField("Score", &MixedRecord::scores));
};

/// A fixed container whose item type carries attributes, so overflowing items
/// reach the streamed attribute capture and are then skipped. Slot holds the
/// container alone: with one element field the document-order hint stays on it,
/// which is what puts repeats on the streamed path.
struct AttrSlot {
  std::string_view sid;
};

template<>
struct xmlight::XmlMetadata<AttrSlot> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("sid", &AttrSlot::sid));
};

struct SlotGroup {
  std::array<AttrSlot, 1> slots{};
};

template<>
struct xmlight::XmlMetadata<SlotGroup> {
  static constexpr auto fields = std::make_tuple(xmlight::arrField("Slot", &SlotGroup::slots));
};

struct SlotSummary {
  std::string_view label;
};

template<>
struct xmlight::XmlMetadata<SlotSummary> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("label", &SlotSummary::label));
};

struct SlotDoc {
  SlotGroup group{};
  SlotSummary summary{};
};

template<>
struct xmlight::XmlMetadata<SlotDoc> {
  static constexpr auto fields = std::make_tuple(xmlight::field("Group", &SlotDoc::group),
                                                 xmlight::field("Summary", &SlotDoc::summary));
};

/// A required attribute whose name is also a plausible child element name, for
/// the findFieldIndex collision between attribute and element kinds.
struct ReqAttrRecord {
  std::string_view id;
  std::string_view name;
};

template<>
struct xmlight::XmlMetadata<ReqAttrRecord> {
  static constexpr auto fields = std::make_tuple(xmlight::attrField("id", &ReqAttrRecord::id, true),
                                                 xmlight::field("Name", &ReqAttrRecord::name));
};

/// An xs:list of owning strings: the item type that makes list-element
/// escaping observable (a string item may carry markup bytes; an int cannot).
struct StringList {
  std::vector<std::string> tags;
};

template<>
struct xmlight::XmlMetadata<StringList> {
  static constexpr auto fields = std::make_tuple(xmlight::listField("Tags", &StringList::tags));
};
#pragma once
#include <string_view>
#include <vector>

#include "TurboXML.hh"

struct Skills {
  std::vector<std::string_view> items;
};

template <>
struct xml::XmlMetadata<Skills> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("Skill", &Skills::items));
};

struct OrgMember {
  int id{0};
  std::string_view role;
  std::string_view full_name;
  std::string_view email;
  Skills skills;
};

template <>
struct xml::XmlMetadata<OrgMember> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("id", &OrgMember::id),
                      xml::attr_field("role", &OrgMember::role),
                      xml::field("FullName", &OrgMember::full_name),
                      xml::field("Email", &OrgMember::email),
                      xml::field("Skills", &OrgMember::skills));
};

struct OrgTeam {
  int id{0};
  std::string_view name;
  std::vector<OrgMember> members;
};

template <>
struct xml::XmlMetadata<OrgTeam> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("id", &OrgTeam::id),
                      xml::attr_field("name", &OrgTeam::name),
                      xml::vec_field("Member", &OrgTeam::members));
};

struct OrgDepartment {
  int id{0};
  std::string_view name;
  std::vector<OrgTeam> teams;
};

template <>
struct xml::XmlMetadata<OrgDepartment> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("id", &OrgDepartment::id),
                      xml::attr_field("name", &OrgDepartment::name),
                      xml::vec_field("Team", &OrgDepartment::teams));
};

struct Organization {
  int id{0};
  std::string_view name;
  std::vector<OrgDepartment> departments;
};

template <>
struct xml::XmlMetadata<Organization> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("id", &Organization::id),
                      xml::attr_field("name", &Organization::name),
                      xml::vec_field("Department", &Organization::departments));
};

struct User {
  int id{0};
  std::string_view name;
  std::string_view email;
};

template <>
struct xml::XmlMetadata<User> {
  static constexpr auto fields = std::make_tuple(
      xml::attr_field("id", &User::id), xml::field("Name", &User::name),
      xml::field("Email", &User::email));
};

struct Users {
  std::vector<User> items;
};

template <>
struct xml::XmlMetadata<Users> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("User", &Users::items));
};

struct Address {
  std::string_view street;
  int zip{};
};

template <>
struct xml::XmlMetadata<Address> {
  static constexpr auto fields = std::make_tuple(
      xml::field("street", &Address::street), xml::field("zip", &Address::zip));
};

struct Person {
  std::string_view name;
  int age{};
  Address address;
};

template <>
struct xml::XmlMetadata<Person> {
  static constexpr auto fields = std::make_tuple(
      xml::field("name", &Person::name), xml::field("age", &Person::age),
      xml::field("address", &Person::address));
};

struct FlatItem {
  int id{};
  std::string_view title{};
  std::string_view description{};
  int status{};
};

template <>
struct xml::XmlMetadata<FlatItem> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("id", &FlatItem::id),
                      xml::field("title", &FlatItem::title),
                      xml::field("desc", &FlatItem::description),
                      xml::field("status", &FlatItem::status));
};

struct FlatList {
  std::vector<FlatItem> items;
};

template <>
struct xml::XmlMetadata<FlatList> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("Item", &FlatList::items));
};

struct DeepL5 {
  int value{};
};
template <>
struct xml::XmlMetadata<DeepL5> {
  static constexpr auto fields =
      std::make_tuple(xml::field("v", &DeepL5::value));
};

struct DeepL4 {
  DeepL5 next;
};
template <>
struct xml::XmlMetadata<DeepL4> {
  static constexpr auto fields =
      std::make_tuple(xml::field("L5", &DeepL4::next));
};

struct DeepL3 {
  DeepL4 next;
};
template <>
struct xml::XmlMetadata<DeepL3> {
  static constexpr auto fields =
      std::make_tuple(xml::field("L4", &DeepL3::next));
};

struct DeepL2 {
  DeepL3 next;
};
template <>
struct xml::XmlMetadata<DeepL2> {
  static constexpr auto fields =
      std::make_tuple(xml::field("L3", &DeepL2::next));
};

struct DeepL1 {
  DeepL2 next;
};
template <>
struct xml::XmlMetadata<DeepL1> {
  static constexpr auto fields =
      std::make_tuple(xml::field("L2", &DeepL1::next));
};

struct DeepList {
  std::vector<DeepL1> items;
};
template <>
struct xml::XmlMetadata<DeepList> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("L1", &DeepList::items));
};

struct AttrItem {
  int a1{}, a2{}, a3{}, a4{}, a5{};
  std::string_view s1, s2, s3, s4, s5;
};

template <>
struct xml::XmlMetadata<AttrItem> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("a1", &AttrItem::a1),
                      xml::attr_field("a2", &AttrItem::a2),
                      xml::attr_field("a3", &AttrItem::a3),
                      xml::attr_field("a4", &AttrItem::a4),
                      xml::attr_field("a5", &AttrItem::a5),
                      xml::attr_field("s1", &AttrItem::s1),
                      xml::attr_field("s2", &AttrItem::s2),
                      xml::attr_field("s3", &AttrItem::s3),
                      xml::attr_field("s4", &AttrItem::s4),
                      xml::attr_field("s5", &AttrItem::s5));
};

struct AttrList {
  std::vector<AttrItem> items;
};

template <>
struct xml::XmlMetadata<AttrList> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("Item", &AttrList::items));
};

struct TreeNode {
  std::vector<TreeNode> children;
};

template <>
struct xml::XmlMetadata<TreeNode> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("Node", &TreeNode::children));
};

struct OwnedPerson {
  std::string name;
  int age{};
  std::string email;
};

template <>
struct xml::XmlMetadata<OwnedPerson> {
  static constexpr auto fields =
      std::make_tuple(xml::field("name", &OwnedPerson::name),
                      xml::field("age", &OwnedPerson::age),
                      xml::field("email", &OwnedPerson::email));
};

struct OwnedUser {
  int id{};
  std::string role;
  std::string name;
};

template <>
struct xml::XmlMetadata<OwnedUser> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("id", &OwnedUser::id),
                      xml::attr_field("role", &OwnedUser::role),
                      xml::field("Name", &OwnedUser::name));
};

struct OwnedList {
  std::vector<std::string> tags;
};

template <>
struct xml::XmlMetadata<OwnedList> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("Tag", &OwnedList::tags));
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

template <>
struct xml::XmlMetadata<Book> {
  static constexpr auto fields = std::make_tuple(
      xml::attr_field("id", &Book::id), xml::field("author", &Book::author),
      xml::field("title", &Book::title), xml::field("genre", &Book::genre),
      xml::field("price", &Book::price),
      xml::field("publish_date", &Book::publish_date),
      xml::field("description", &Book::description));
};

struct Catalog {
  std::vector<Book> books;
};

template <>
struct xml::XmlMetadata<Catalog> {
  static constexpr auto fields =
      std::make_tuple(xml::vec_field("book", &Catalog::books));
};

struct FixedSkills {
  std::array<std::string_view, 3> items{};
};

template <>
struct xml::XmlMetadata<FixedSkills> {
  static constexpr auto fields =
      std::make_tuple(xml::arr_field("Skill", &FixedSkills::items));
};

struct Toggle {
  bool enabled{};
  bool active{};
  bool verbose{};
};

template <>
struct xml::XmlMetadata<Toggle> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("enabled", &Toggle::enabled),
                      xml::field("active", &Toggle::active),
                      xml::field("verbose", &Toggle::verbose));
};

struct MixedRecord {
  int id{};
  std::string_view name;
  std::array<int, 4> scores{};
};

template <>
struct xml::XmlMetadata<MixedRecord> {
  static constexpr auto fields =
      std::make_tuple(xml::attr_field("id", &MixedRecord::id),
                      xml::field("Name", &MixedRecord::name),
                      xml::arr_field("Score", &MixedRecord::scores));
};
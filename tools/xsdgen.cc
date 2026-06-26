/// @file xsdgen.cc
/// @brief CLI: turboxml_xsdgen <schema.xsd> [-o out.hh]
///
/// Reads an XSD, emits TurboXML XmlMetadata to stdout (or -o file), and prints
/// any unsupported-construct notes to stderr. Exits non-zero only when the XSD
/// cannot be parsed at all.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "XsdCodegen.hh"

namespace {

auto read_file(const std::string& path, std::string& out) -> bool {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  std::string input;
  std::string output;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      output = argv[++i];
    } else if (arg == "-h" || arg == "--help") {
      std::cout << "usage: turboxml_xsdgen <schema.xsd> [-o out.hh]\n";
      return 0;
    } else if (input.empty()) {
      input = arg;
    }
  }
  if (input.empty()) {
    std::cerr << "error: no input schema given (try --help)\n";
    return 2;
  }

  std::string xsd;
  if (!read_file(input, xsd)) {
    std::cerr << "error: cannot read '" << input << "'\n";
    return 2;
  }

  const xsd::GenResult result = xsd::generate(xsd);
  if (!result.ok) {
    for (const auto& n : result.notes) std::cerr << "error: " << n << '\n';
    return 1;
  }

  if (output.empty()) {
    std::cout << result.code;
  } else {
    std::ofstream out(output, std::ios::binary);
    if (!out) {
      std::cerr << "error: cannot write '" << output << "'\n";
      return 2;
    }
    out << result.code;
  }

  for (const auto& n : result.notes) {
    std::cerr << "note: unsupported: " << n << '\n';
  }
  std::cerr << "note: " << result.notes.size() << " unsupported construct(s)\n";
  return 0;
}

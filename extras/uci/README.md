# OMS UCI v2.5 add-on

Generates LightningXML type definitions for the **Universal Command and Control
Interface (UCI) v2.5** message set published by the [Open Architecture
Consortium](https://github.com/open-arsenal/uci), and tests them against
hand-written messages.

The add-on is optional and off by default: it downloads ~8.6 MB of schema at
configure time and produces a translation unit large enough to be worth opting
into deliberately.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLIGHTNINGXML_WITH_UCI=ON
cmake --build build --target lightningxml_uci_tests
ctest --test-dir build -R Uci
```

## Schema provenance

Both files are fetched from `open-arsenal/UCI`, pinned to commit
`6d5af178bcd3b18b3a102bede101dea0af9a0ad5`, and checked against a SHA256 before
use. Nothing is vendored into this repository.

| File | SHA256 |
|---|---|
| `OAC-STD-UCI_V2.5/UCI_MessageDefinitions_v2_5_0.xsd` | `ac9430499e1107371345e04430895c8c9f18578c1a6b022958ca43ae8aa7bf27` |
| `OAC-STD-UCI_V2.5/UCI_SecurityMarkings_v2_5_0.xsd` | `4a8056f1503234d423a0c2495844ab65387febced62bea58b87d4f343e9e7a0b` |

Schema version `002.5.0`. The schema carries the notice: *"Distribution
Statement A. Approved for public release: distribution is unlimited."*

For an air-gapped build, point the configure step at a local copy of the
`OAC-STD-UCI_V2.5` directory instead - both XSDs must sit side by side, since
`xs:include` is resolved relative to the message-definitions file:

```bash
cmake -S . -B build -DLIGHTNINGXML_WITH_UCI=ON \
      -DLIGHTNINGXML_UCI_SCHEMA_DIR=/path/to/OAC-STD-UCI_V2.5
```

## What is generated

`xsdgen` runs over the whole schema, so every message is available.

| | |
|---|---|
| Message roots (`xs:element`) | 722 |
| Structs (`xs:complexType`) | 4,612 |
| Enums (`xs:simpleType` enumerations) | 725 (7,766 enumerators) |
| `XmlConstraints` specializations | 1,155 |
| Generated header | 4.75 MB, ~112k lines |

Measured on an AMD Ryzen 9 7950X3D, Ubuntu 24.04:

| Step | Time | Peak RSS |
|---|---|---|
| `xsdgen` over the 8.3 MB schema | 0.09 s | 63 MB |
| Compiling the test TU (Clang 19, `-O2`) | ~3 min | ~6.9 GB |
| Syntax-checking it (GCC 15, `-O2`) | ~65 s | ~10.3 GB |

The generated header is a single translation unit's worth of work. If you only
need a few messages, generating from a trimmed schema is far cheaper than
paying that per TU.

## Using it

The header ends with a `// root:` comment per message naming its root element
and struct, e.g. `SystemStatus` -> `SystemStatusMT`. Every message type extends
`MessageType`, which carries `SecurityInformation` and `MessageHeader`.

```cpp
#include "uci_generated.hh"

xmlight::Parser parser{xml};
SystemStatusMT msg;
if (xmlight::deserialize(parser, "SystemStatus", msg)) {
  msg.MessageHeader.Mode;          // MessageModeEnum::LIVE
  msg.MessageData.SystemState;     // SystemStateEnum::DEGRADED
  msg.MessageData.SubsystemID[0].UUID;
}
if (const auto err = xmlight::validate(msg)) {
  // facet violation: err->message names the member
}
```

UCI declares `elementFormDefault="qualified"`, so real traffic is
namespace-prefixed (`<uci:SystemStatus>`). LightningXML matches on the local
name and ignores the prefix, so prefixed and unprefixed documents both bind.
Serializing writes unprefixed names.

## Naming

The C++ spelling of a name differs from the schema's in three cases. The XML
name in the metadata is never affected, so parsing and serializing are
unchanged either way.

- A member whose name matches the struct declaring it, or matches its own
  type's name, takes a trailing `_` (`CargoStatusMDT::CargoType_`). C++ forbids
  both spellings.
- An enumerator or member spelled like a C or GoogleTest macro takes a trailing
  `_`: `SensingTypeEnum::SIGINT_`, `ComponentSettingEnum::TEST_`,
  `CTR_StateEnum::FAIL_`. Otherwise `<csignal>` or `<gtest/gtest.h>` would
  macro-expand them.
- A member that hides a type name is followed by members declared in elaborated
  form (`struct WeatherReportType WeatherData;`).

## Known gaps

`xsdgen` reports 207 notes on this schema, none fatal:

- **60 `xs:choice` groups are skipped** where two branches map to the same C++
  type; `std::variant` needs distinct alternatives, so those elements have no
  member at all.
- **110 facet constraints on choice branches are not enforced** - a branch
  lives in a `std::variant`, not in a member named after it.
- **37 elements fall back to `std::string`**: `xs:unsignedByte` (25),
  `xs:duration` (8), and `xs:hexBinary` (4) have no mapping.
- Namespaces are matched by local name only; the add-on does not distinguish
  two elements that differ only by namespace (LightningXML resolves no
  prefixes).

## Regenerating by hand

```bash
./build/lightningxml_xsdgen /path/to/UCI_MessageDefinitions_v2_5_0.xsd -o uci_generated.hh
```

The notes go to stderr; the header repeats them in its leading comment block.

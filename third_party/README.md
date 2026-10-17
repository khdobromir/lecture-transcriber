`nlohmann/json.hpp` is the unmodified single header from nlohmann/json v3.12.0:
https://github.com/nlohmann/json/blob/v3.12.0/single_include/nlohmann/json.hpp

SHA-256: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`.
Its MIT license is preserved in `nlohmann/LICENSE.MIT` and in the header itself.
The header is used for the CLI JSONL protocol and result manifest. GUI parsing
uses Qt's JSON parser, allowing independently encoded protocol fixtures in tests.

## Material Color Utilities

The GUI embeds the C++ Material Color Utilities library (Apache-2.0), revision
`dd62982dfdefdfc9232ce8d76e122fb1c6d188ae`. Sources, license, archive digest and
local portability changes are retained in `third_party/material-color-utilities`.
It generates local Tonal Spot palettes; no online service is used.

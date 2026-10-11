`nlohmann/json.hpp` is the unmodified single header from nlohmann/json v3.12.0:
https://github.com/nlohmann/json/blob/v3.12.0/single_include/nlohmann/json.hpp

SHA-256: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`.
Its MIT license is preserved in `nlohmann/LICENSE.MIT` and in the header itself.
The header is used for the CLI JSONL protocol and result manifest. GUI parsing
uses Qt's JSON parser, allowing independently encoded protocol fixtures in tests.

"""Apply narrowly scoped Unicode fixes to the exact pinned whisper.cpp sources."""
import hashlib
from pathlib import Path
import sys

PIN = "927cfce34f31707e17f2bff35c349632fb9e2c3a"
HASHES = {
    "examples/cli/cli.cpp": "840f331f80a98c41fc21eb4cf109c4c6a5496b8f248e9bbce58dd733dece76b2",
    "examples/common.cpp": "568d38cf668a74c9342533b082b387a16211bec8d0f57ceb2b4900b17b17ae58",
    "examples/common-whisper.cpp": "852fbc77d2461322a82b9c571cf4703bac3c78c5c51d3a90e80792ce0c04e313",
    "src/whisper.cpp": "c48686fbc2cba1b0ac0f9c8e964188c691e67fff5906f2629f3223ff64f92d16",
}


def patch(root: Path):
    originals = {}
    for name, expected in HASHES.items():
        raw = (root / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError(f"Source mismatch: {name}; requires unmodified whisper.cpp {PIN}")
        originals[name] = raw.decode("utf-8")
    cli = originals["examples/cli/cli.cpp"]
    cli = '#include "../transcribe-windows.hpp"\n' + cli
    cli = cli.replace("int main(int argc, char ** argv) {", "#ifdef _WIN32\nint transcribe_whisper_main(int argc, char ** argv) {\n#else\nint main(int argc, char ** argv) {\n#endif")
    cli = cli.replace("std::ifstream fin(font);", "std::ifstream fin(transcribe_windows::path(font));")
    cli = cli.replace("std::ifstream fin(rspfile);", "std::ifstream fin(transcribe_windows::path(rspfile));")
    cli = cli.replace("std::ifstream ifs(params.grammar.c_str());", "std::ifstream ifs(transcribe_windows::path(params.grammar));")
    cli = cli.replace("std::ofstream{fname_out}", "std::ofstream{transcribe_windows::path(fname_out)}")
    cli += """
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    std::vector<std::string> storage;
    storage.reserve(argc);
    for (int i = 0; i < argc; ++i) storage.push_back(transcribe_windows::utf8(argv[i]));
    std::vector<char*> args;
    for (auto& value : storage) args.push_back(value.data());
    args.push_back(nullptr);
    return transcribe_whisper_main(argc, args.data());
}
#endif
"""
    common = '#include "transcribe-windows.hpp"\n' + originals["examples/common-whisper.cpp"]
    common = common.replace("result = ma_decoder_init_file(fname.c_str(), &decoder_config, &decoder);", """#ifdef _WIN32
                result = ma_decoder_init_file_w(transcribe_windows::path(fname).c_str(), &decoder_config, &decoder);
#else
                result = ma_decoder_init_file(fname.c_str(), &decoder_config, &decoder);
#endif""")
    core = originals["src/whisper.cpp"].replace("std::codecvt_utf8<wchar_t>", "std::codecvt_utf8_utf16<wchar_t>")
    utilities = originals["examples/common.cpp"].replace('#include "common.h"', '#include "transcribe-windows.hpp"\n#include "common.h"', 1)
    utilities = utilities.replace("std::ifstream infile(filename);", "std::ifstream infile(transcribe_windows::path(filename));")
    updates = {"examples/cli/cli.cpp": cli, "examples/common.cpp": utilities,
               "examples/common-whisper.cpp": common, "src/whisper.cpp": core}
    for name, data in updates.items():
        (root / name).write_bytes(data.encode("utf-8"))
    helper = Path(__file__).resolve().parents[1] / "packaging/windows/whisper-unicode.hpp"
    (root / "examples/transcribe-windows.hpp").write_bytes(helper.read_bytes())
    print(f"Applied Windows Unicode integration to whisper.cpp {PIN}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python scripts/patch-whisper-windows.py WHISPER-SOURCE")
    patch(Path(sys.argv[1]))

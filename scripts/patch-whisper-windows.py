"""Apply Unicode I/O and trusted Windows backend loading to pinned whisper.cpp."""
import hashlib
from pathlib import Path
import sys

PIN = "927cfce34f31707e17f2bff35c349632fb9e2c3a"
HASHES = {
    "examples/cli/cli.cpp": "840f331f80a98c41fc21eb4cf109c4c6a5496b8f248e9bbce58dd733dece76b2",
    "examples/cli/CMakeLists.txt": "ea772eceff30b24f9d4fe96972d58d1f34e735152dfb4b52e0456bc23662a746",
    "examples/common.cpp": "568d38cf668a74c9342533b082b387a16211bec8d0f57ceb2b4900b17b17ae58",
    "examples/common-whisper.cpp": "852fbc77d2461322a82b9c571cf4703bac3c78c5c51d3a90e80792ce0c04e313",
    "src/whisper.cpp": "c48686fbc2cba1b0ac0f9c8e964188c691e67fff5906f2629f3223ff64f92d16",
    "ggml/src/ggml-backend-reg.cpp": "0f63c69ad083e0744872f57d59d049b069283303f5bbc646222907d779afe4d1",
    "ggml/src/ggml-backend-dl.cpp": "acd76df8b83bcdef6807bc7b0520f57b379d61b4ff1027c04aa4d140d16ad07f",
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
    transcribe_windows::isolate_dll_search();
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
    registry = originals["ggml/src/ggml-backend-reg.cpp"]
    start = registry.index("    std::vector<fs::path> search_paths;", registry.index("static ggml_backend_reg_t ggml_backend_load_best"))
    end = registry.index("\n    int best_score", start)
    registry = registry[:start] + """#ifdef _WIN32
    // This packaged CPU engine only trusts its own executable directory.
    // Preserve scoring/runtime dispatch among the bundled CPU variants.
    (void) user_search_path;
    const auto trusted = get_executable_path();
    if (trusted.empty()) return nullptr;
    const std::vector<fs::path> search_paths{trusted};
#else
""" + registry[start:end] + "\n#endif\n" + registry[end:]
    start = registry.index("    // check the environment variable GGML_BACKEND_PATH")
    registry = registry[:start] + "#ifndef _WIN32\n" + registry[start:]
    end = registry.rindex("\n}")
    registry = registry[:end] + "\n#endif" + registry[end:]
    # GetModuleFileNameW must not silently truncate a Unicode install path.
    registry = registry.replace("std::vector<wchar_t> path(MAX_PATH);", "std::vector<wchar_t> path(32768);")
    registry = registry.replace("GetModuleFileNameW(NULL, path.data(), path.size())", "GetModuleFileNameW(NULL, path.data(), static_cast<DWORD>(path.size()))")
    registry = registry.replace("if (len == 0)", "if (len == 0 || len >= path.size())")
    loader = originals["ggml/src/ggml-backend-dl.cpp"].replace(
        "LoadLibraryW(path.wstring().c_str())",
        "LoadLibraryExW(fs::absolute(path).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)")
    cli_cmake = originals["examples/cli/CMakeLists.txt"] + """
if(MSVC)
    target_link_options(${TARGET} PRIVATE /DEPENDENTLOADFLAG:0xA00)
endif()
"""
    updates = {"examples/cli/cli.cpp": cli, "examples/common.cpp": utilities,
               "examples/common-whisper.cpp": common, "src/whisper.cpp": core,
               "examples/cli/CMakeLists.txt": cli_cmake,
               "ggml/src/ggml-backend-reg.cpp": registry, "ggml/src/ggml-backend-dl.cpp": loader}
    for name, data in updates.items():
        (root / name).write_bytes(data.encode("utf-8"))
    helper = Path(__file__).resolve().with_name("whisper-unicode.hpp")
    if not helper.is_file():
        helper = Path(__file__).resolve().parents[1] / "packaging/windows/whisper-unicode.hpp"
    (root / "examples/transcribe-windows.hpp").write_bytes(helper.read_bytes())
    print(f"Applied Windows Unicode and backend isolation to whisper.cpp {PIN}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python scripts/patch-whisper-windows.py WHISPER-SOURCE")
    patch(Path(sys.argv[1]))

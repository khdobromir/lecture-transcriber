"""Select owned translation units and the Qt SDK's actual MOC revision."""
import json
from pathlib import Path
import re
import shlex
import sys

build, root, destination = map(lambda value: Path(value).resolve(), sys.argv[1:4])
entries = []
tidy_entries = []
revisions = set()
qt_defines = set()
for entry in json.loads((build / "compile_commands.json").read_text()):
    directory = Path(entry["directory"])
    source = (directory / entry["file"]).resolve()
    # Owned TUs are directly in these source directories. Nested Qt build/vendor
    # trees must remain excluded even when somebody builds inside gui/.
    if source.parent not in {root / "src", root / "tests", root / "gui"}:
        continue
    entries.append(entry)
    entry = dict(entry)
    arguments = list(entry.get("arguments") or shlex.split(entry["command"]))
    includes = []
    for index, argument in enumerate(arguments):
        if argument in {"-I", "-isystem"} and index + 1 < len(arguments):
            includes.append(arguments[index + 1])
        elif argument.startswith("-I"):
            includes.append(argument[2:])
    qt_includes = set()
    for include in includes:
        for header in [directory / include / "qtmetamacros.h", directory / include / "QtCore/qtmetamacros.h"]:
            if header.is_file():
                match = re.search(r"^#define Q_MOC_OUTPUT_REVISION\s+(\d+)$", header.read_text(), re.MULTILINE)
                if match:
                    revisions.add(int(match[1]))
                    qt_includes.add(include)
                    helper = header.with_name("qtmochelpers.h")
                    if helper.is_file():
                        marker = re.search(r"^\s*#\s*define\s+QT_MOC_HAS_STRINGDATA\s+(\d+)\s*$", helper.read_text(), re.MULTILINE)
                        if marker:
                            qt_defines.add("QT_MOC_HAS_STRINGDATA=" + marker[1])
        # Qt module includes have the same SDK root as QtCore.
        if (directory / include / "../QtCore/qtmetamacros.h").is_file():
            qt_includes.add(include)
    # LLVM #62985: clang-tidy 18 double-counts an overloaded Qt delete when
    # its header is system code. Analyze only Qt includes as ordinary includes;
    # keep every checker enabled and retain system status for other SDKs.
    for index, argument in enumerate(arguments[:-1]):
        if argument == "-isystem" and arguments[index + 1] in qt_includes:
            arguments[index] = "-I"
    entry.pop("command", None)
    entry["arguments"] = arguments
    tidy_entries.append(entry)
if not entries:
    sys.exit("No project sources in compilation database")
if len(revisions) > 1:
    sys.exit("Conflicting Qt MOC revisions in compilation database")
(destination / "compile_commands.json").write_text(json.dumps(entries))
(destination / "tidy").mkdir(exist_ok=True)
(destination / "tidy/compile_commands.json").write_text(json.dumps(tidy_entries))
(destination / "sources.txt").write_text("\n".join(sorted({str((Path(item["directory"]) / item["file"]).resolve()) for item in entries})) + "\n")
(destination / "moc-revision.txt").write_text(str(next(iter(revisions))) if revisions else "")

(destination / "cppcheck-qt-defines.txt").write_text("".join(value + "\n" for value in sorted(qt_defines)))

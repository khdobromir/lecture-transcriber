"""Bind partial FFmpeg dependency sources/notices to its verified input and recipes."""
import argparse
import ast
import base64
import configparser
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import tarfile
from package_inputs import ffmpeg_source_records
from package_notices import collect_qt_notices, safe_path
from package_source import digest


def verified_input(record, cache):
    name = record["filename"]
    if Path(name).name != name or "\\" in name or ":" in name:
        raise ValueError("Unsafe FFmpeg input filename")
    path = cache / name
    if path.is_symlink() or digest(path) != record["sha256"]:
        raise ValueError("FFmpeg input checksum mismatch: " + name)
    return path


def verify_submodule(record, parent, cache):
    """Verify the parent commit/tree chain without fetching or executing Git code."""
    module = record["git_submodule"]

    def object_bytes(kind, encoded, expected):
        if not isinstance(encoded, str) or len(encoded) > 4 * 1024 * 1024:
            raise ValueError("Invalid FFmpeg Git object proof")
        body = base64.b64decode(encoded, validate=True)
        header = kind.encode() + b" " + str(len(body)).encode() + b"\0"
        if hashlib.sha1(header + body).hexdigest() != expected:
            raise ValueError("FFmpeg Git object proof checksum mismatch")
        return body

    commit = object_bytes("commit", module["commit_base64"], parent["revision"])
    first = commit.split(b"\n", 1)[0]
    if not re.fullmatch(rb"tree [a-f0-9]{40}", first):
        raise ValueError("Invalid FFmpeg parent commit tree")
    root = first[5:].decode("ascii")
    trees = module["trees_base64"]
    parts = module["path"].split("/")
    if not 1 <= len(trees) <= len(parts):
        raise ValueError("Invalid FFmpeg Git tree proof count")
    used = set()

    def entries(tree):
        if tree not in trees:
            raise ValueError("Missing FFmpeg Git tree proof")
        body = object_bytes("tree", trees[tree], tree)
        used.add(tree)
        result = {}; offset = 0
        while offset < len(body):
            end = body.find(b"\0", offset)
            if end < 0 or end + 21 > len(body):
                raise ValueError("Invalid FFmpeg Git tree entry")
            fields = body[offset:end].split(b" ", 1)
            if len(fields) != 2:
                raise ValueError("Invalid FFmpeg Git tree entry")
            mode, name = fields[0].decode("ascii"), fields[1].decode("utf-8")
            if (mode not in {"40000", "100644", "100755", "120000", "160000"}
                    or name in {"", ".", ".."} or "/" in name or name in result):
                raise ValueError("Invalid FFmpeg Git tree entry")
            result[name] = (mode, body[end + 1:end + 21].hex())
            offset = end + 21
        return result

    current = root
    root_entries = entries(root)
    for index, part in enumerate(parts):
        children = root_entries if index == 0 else entries(current)
        mode, current = children.get(part, (None, None))
        if mode != ("160000" if index == len(parts) - 1 else "40000"):
            raise ValueError("FFmpeg submodule path is not a Git link")
    if current != record["revision"] or used != set(trees):
        raise ValueError("FFmpeg submodule revision does not match parent Git link")
    path = verified_input(parent, cache)
    with tarfile.open(path) as archive:
        matches = [m for m in archive.getmembers() if m.isfile()
                   and PurePosixPath(m.name).parts[1:] == (".gitmodules",)]
        if len(matches) != 1 or matches[0].size > 1024 * 1024:
            raise ValueError("Missing/ambiguous FFmpeg submodule declaration")
        safe_path(matches[0].name)
        declarations = archive.extractfile(matches[0]).read()
    blob = hashlib.sha1(b"blob " + str(len(declarations)).encode() + b"\0" + declarations).hexdigest()
    if root_entries.get(".gitmodules") != ("100644", blob):
        raise ValueError("FFmpeg submodule declaration does not match parent Git tree")
    config = configparser.ConfigParser(interpolation=None)
    config.read_string(declarations.decode("utf-8"))
    urls = [config.get(section, "url", fallback=None) for section in config.sections()
            if config.get(section, "path", fallback=None) == module["path"]]
    if urls != [record["repository"]]:
        raise ValueError("FFmpeg submodule repository does not match declaration")
    verified_input(parent, cache)
    return dict(source=record["name"], parent=parent["name"], parent_sha256=parent["sha256"],
                path=module["path"], revision=current,
                declaration_sha256=hashlib.sha256(declarations).hexdigest())


def verify_deps_input(record, parent, cache):
    reference = record["deps_input"]
    path = verified_input(parent, cache)
    with tarfile.open(path) as archive:
        matches = [m for m in archive.getmembers() if m.isfile()
                   and PurePosixPath(m.name).parts[1:] == ("DEPS",)]
        if len(matches) != 1 or matches[0].size > 1024 * 1024:
            raise ValueError("Missing/ambiguous FFmpeg DEPS declaration")
        safe_path(matches[0].name)
        body = archive.extractfile(matches[0]).read()
    # Read only literal variables and string concatenation/Var references. No
    # eval, exec, imports or upstream dependency-sync scripts are executed.
    try:
        tree = ast.parse(body.decode("utf-8"))
    except SyntaxError as error:
        raise ValueError("Invalid FFmpeg DEPS declaration") from error
    if any(not isinstance(node, ast.Assign) or len(node.targets) != 1
           or not isinstance(node.targets[0], ast.Name)
           or node.targets[0].id not in {"vars", "deps", "use_relative_paths"} for node in tree.body):
        raise ValueError("Unsupported FFmpeg DEPS statement")
    assignments = {}
    for name in ["vars", "deps", "use_relative_paths"]:
        values = [node.value for node in tree.body if isinstance(node, ast.Assign)
                  and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
                  and node.targets[0].id == name]
        if len(values) != 1:
            raise ValueError("Missing/ambiguous FFmpeg DEPS mapping")
        assignments[name] = values[0]
    if ast.literal_eval(assignments["use_relative_paths"]) is not True:
        raise ValueError("FFmpeg DEPS paths must be relative to the source root")
    variables = ast.literal_eval(assignments["vars"])
    if not isinstance(variables, dict) or any(not isinstance(k, str) or not isinstance(v, str)
                                              for k, v in variables.items()):
        raise ValueError("Invalid FFmpeg DEPS variables")

    def value(node):
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            return node.value
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
            return value(node.left) + value(node.right)
        if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "Var"
                and not node.keywords and len(node.args) == 1 and isinstance(node.args[0], ast.Constant)
                and isinstance(node.args[0].value, str) and node.args[0].value in variables):
            return variables[node.args[0].value]
        raise ValueError("Unsupported FFmpeg DEPS expression")

    deps = assignments["deps"]
    if not isinstance(deps, ast.Dict) or any(not isinstance(key, ast.Constant) or not isinstance(key.value, str)
                                           for key in deps.keys):
        raise ValueError("Invalid FFmpeg DEPS paths")
    selected = [node for key, node in zip(deps.keys, deps.values) if key.value == reference["path"]]
    if len(selected) != 1 or value(selected[0]) != record["repository"] + "@" + record["revision"]:
        raise ValueError("FFmpeg dependency does not match parent DEPS")
    verified_input(parent, cache)
    return dict(source=record["name"], parent=parent["name"], parent_sha256=parent["sha256"],
                path=reference["path"], revision=record["revision"], deps_sha256=hashlib.sha256(body).hexdigest())


def collect_ffmpeg_notices(lock, binary, cache, licenses, configuration):
    sources = ffmpeg_source_records(lock, [binary])
    verified_input(binary, cache)
    recipes = [r for r in lock["downloads"] if r["name"] == "ffmpeg-build-recipes"
               and binary["sha256"] in r["for_binary_sha256"]]
    if len(recipes) != 1:
        raise ValueError("Missing/ambiguous FFmpeg build recipes")
    recipe_archive = verified_input(recipes[0], cache)
    flags = set(shlex.split(configuration))
    deps_inputs = []
    for record in sources:
        if "deps_input" in record:
            parent = next(source for source in sources if source["name"] == record["deps_input"]["parent"])
            deps_inputs.append(verify_deps_input(record, parent, cache))
    submodules = []
    for record in sources:
        if "git_submodule" in record:
            parent = next(source for source in sources if source["name"] == record["git_submodule"]["parent"])
            submodules.append(verify_submodule(record, parent, cache))
    generated_inputs = []
    for record in sources:
        if "generated_input" not in record:
            continue
        parent = next(source for source in sources if source["name"] == record["generated_input"]["parent"])
        path = verified_input(parent, cache)
        with tarfile.open(path) as archive:
            scripts = {}
            for relative in ["autogen.sh", "dnn/download_model.sh"]:
                matches = [m for m in archive.getmembers() if m.isfile()
                           and PurePosixPath(m.name).parts[1:] == PurePosixPath(relative).parts]
                if len(matches) != 1 or matches[0].size > 1024 * 1024:
                    raise ValueError("Missing/ambiguous Opus build input reference")
                safe_path(matches[0].name)
                scripts[relative] = archive.extractfile(matches[0]).read()
        checksum = record["upstream_sha256"]
        url = "https://media.xiph.org/opus/models/opus_data-" + checksum + ".tar.gz"
        if (record["url"] != url
                or re.findall(rb'^dnn/download_model.sh "([a-f0-9]{64})"$', scripts["autogen.sh"], re.MULTILINE) != [checksum.encode()]
                or b'model=opus_data-$1.tar.gz\n' not in scripts["dnn/download_model.sh"]
                or b'https://media.xiph.org/opus/models/$model' not in scripts["dnn/download_model.sh"]):
            raise ValueError("Opus generated input does not match pinned source")
        verified_input(parent, cache)
        generated_inputs.append(dict(source=record["name"], parent=parent["name"], parent_sha256=parent["sha256"],
                                     upstream_sha256=checksum, retained_sha256=record["sha256"],
                                     reference_sha256={name: hashlib.sha256(body).hexdigest() for name, body in scripts.items()}))
    with tarfile.open(recipe_archive) as archive:
        members = archive.getmembers()
        if len({m.name for m in members}) != len(members):
            raise ValueError("Duplicate FFmpeg recipe entry")
        for record in sources:
            safe_path("source/" + record["recipe"])
            slot = record["recipe_slot"]
            if not re.fullmatch(r"[0-9]*", slot):
                raise ValueError("Unsafe FFmpeg recipe slot")
            matches = [m for m in members if m.isfile()
                       and PurePosixPath(m.name).parts[1:] == PurePosixPath(record["recipe"]).parts]
            if len(matches) != 1:
                raise ValueError("Missing/ambiguous FFmpeg dependency recipe")
            safe_path(matches[0].name)
            text = archive.extractfile(matches[0]).read().decode("utf-8")
            # A recipe may name a tag; the retained archive still uses its resolved full commit.
            recipe_source = record
            if "git_submodule" in record:
                recipe_source = next(source for source in sources if source["name"] == record["git_submodule"]["parent"])
            if "deps_input" in record:
                recipe_source = next(source for source in sources if source["name"] == record["deps_input"]["parent"])
            recipe_revision = recipe_source.get("recipe_revision", recipe_source["revision"])
            revision_variable = "SCRIPT_REV" if record.get("svn_snapshot") is True else "SCRIPT_COMMIT"
            if record.get("svn_snapshot") is True:
                recipe_revision = record["revision"]
            for variable, expected in [("SCRIPT_REPO", recipe_source["repository"]), (revision_variable, recipe_revision)]:
                if re.findall(r'^' + variable + slot + r'="([^"\n]+)"$', text, re.MULTILINE) != [expected]:
                    raise ValueError("FFmpeg recipe does not match pinned dependency: " + record["name"])
            if any(flag not in flags or flag.replace("--enable-", "--disable-", 1) in flags
                   for flag in record["configure_flags"]):
                raise ValueError("FFmpeg configuration does not enable dependency: " + record["name"])
    verified_input(recipes[0], cache)
    collect_qt_notices(sources, cache, licenses / "FFmpeg-dependency-source-notices")
    evidence = dict(schema=1, ffmpeg_input_sha256=binary["sha256"], recipe_input=recipes[0], sources=sources,
                    configuration=configuration, generated_inputs=generated_inputs, git_submodules=submodules,
                    deps_inputs=deps_inputs,
                    corresponding_sources_complete=False)
    (licenses / "ffmpeg-source-provenance.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_lock", type=Path)
    parser.add_argument("binary_lock", type=Path)
    parser.add_argument("cache", type=Path)
    parser.add_argument("licenses", type=Path)
    parser.add_argument("ffmpeg", type=Path)
    args = parser.parse_args()
    lock = json.loads(args.source_lock.read_text(encoding="utf-8-sig"))
    dependencies = json.loads(args.binary_lock.read_text(encoding="utf-8-sig"))["downloads"]
    binary = dependencies["ffmpeg"] if isinstance(dependencies, dict) else next(r for r in dependencies if r["name"] == "ffmpeg")
    verified_input(binary, args.cache)
    configuration = subprocess.check_output([str(args.ffmpeg), "-buildconf"], stderr=subprocess.STDOUT, text=True)
    collect_ffmpeg_notices(lock, binary, args.cache, args.licenses, configuration)


if __name__ == "__main__":
    main()

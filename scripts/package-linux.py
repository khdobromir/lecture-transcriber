"""Build a relocatable AppImage without modifying the user's installation."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
from package_source import validate_identity
from package_inputs import archive_inputs, source_records
from package_notices import collect_qt_notices
from package_linux_notices import collect_system_notices, collect_data_notices, write_provenance
from package_linux_sources import collect_sources
from package_sdk_sources import sdk_source_records, collect_sdk_notices
from package_runtime_sources import runtime_source_records, collect_runtime_notices
from package_ffmpeg_sources import collect_ffmpeg_notices
from package_canonical_sources import canonicalize_verified_tar
from package_git_sources import fetch_git_source
from package_svn_sources import fetch_svn_source
from package_standalone_notices import collect_notices as collect_standalone_notices

PROJECT = Path(__file__).resolve().parents[1]


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def publish_file(source, target, expected):
    """Expose a complete verified copy atomically, without replacing a candidate."""
    with tempfile.NamedTemporaryFile(prefix=".candidate-", dir=target.parent, delete=False) as output:
        partial = Path(output.name)
        try:
            with source.open("rb") as stream:
                shutil.copyfileobj(stream, output)
            output.close()
            if digest(partial) != expected:
                raise ValueError("Candidate bytes changed after verification")
            partial.chmod(source.stat().st_mode & 0o777)
            os.link(partial, target)
        finally:
            partial.unlink(missing_ok=True)


def fetch(record, cache):
    target = cache / record["filename"]
    if target.is_symlink():
        raise ValueError("Dependency cache must not contain symlinks")
    if target.is_file():
        if digest(target) != record["sha256"]:
            raise ValueError("Dependency cache checksum mismatch: " + target.name)
        return target
    if record.get("svn_snapshot") is True:
        return fetch_svn_source(record, target)
    if record.get("git_snapshot") is True:
        return fetch_git_source(record, target)
    # Unique partials also allow two builds to use the same cache safely.
    with tempfile.NamedTemporaryFile(dir=cache, prefix="download-", delete=False) as stream:
        partial = Path(stream.name)
        try:
            request = urllib.request.Request(record["url"], headers={"User-Agent": "Transcribe-package/1"})
            for attempt in range(3):
                stream.seek(0)
                stream.truncate()
                try:
                    with urllib.request.urlopen(request, timeout=120) as response:
                        if response.geturl().split(":", 1)[0] != "https":
                            raise ValueError("Dependency redirect must use HTTPS")
                        shutil.copyfileobj(response, stream, 1024 * 1024)
                    break
                except urllib.error.HTTPError as error:
                    status = error.code
                    error.close()
                    if status not in {502, 503, 504} or attempt == 2:
                        raise RuntimeError(f"Download failed for {target.name}: HTTP {status} (attempt {attempt + 1}/3)") from error
                    print(f"Retry download {target.name}: HTTP {status} (attempt {attempt + 2}/3)", flush=True)
                    time.sleep(attempt + 1)
            stream.close()
            if record.get("canonical_tar") is True:
                canonicalize_verified_tar(partial, target, record["sha256"])
            else:
                if digest(partial) != record["sha256"]:
                    raise ValueError("Downloaded checksum mismatch: " + target.name)
                partial.replace(target)
        finally:
            partial.unlink(missing_ok=True)
    return target


def extract_sources(archive, destination):
    """Only regular files/directories; never execute or follow archive links."""
    with tarfile.open(archive) as package:
        names = set()
        for entry in package.getmembers():
            path = PurePosixPath(entry.name)
            if (path.is_absolute() or not path.parts or ".." in path.parts or "\\" in entry.name
                    or path.as_posix() in names or not (entry.isfile() or entry.isdir())):
                raise ValueError("Unsafe dependency archive member: " + entry.name)
            names.add(path.as_posix())
        package.extractall(destination, filter="data")


def inventory(root):
    entries = []
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root).as_posix()
        if relative == "package-manifest.json":
            continue
        if path.is_symlink():
            target = os.readlink(path)
            if Path(target).is_absolute() or not path.resolve().is_relative_to(root.resolve()) or not path.exists():
                raise ValueError("Unsafe or dangling package symlink: " + relative)
            entries.append({"path": relative, "link": target})
        elif path.is_file():
            entries.append({"path": relative, "sha256": digest(path), "mode": path.stat().st_mode & 0o777})
        elif not path.is_dir():
            raise ValueError("Unsupported package entry: " + relative)
    return entries


def version_tuple(value):
    return tuple(int(part) for part in value.split("."))


def check_glibc(root, maximum):
    required = (0, 0)
    for path in root.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        info = subprocess.check_output(["readelf", "--version-info", str(path)], text=True)
        versions = [version_tuple(value) for value in re.findall(r"GLIBC_(\d+\.\d+(?:\.\d+)?)", info)]
        if versions:
            current = max(versions)
            if current > version_tuple(maximum):
                raise ValueError(f"{path.name} needs glibc {'.'.join(map(str, current))}; build on Ubuntu 24.04")
            required = max(required, current)
    return ".".join(map(str, required))


def complete_libraries(root, environment):
    """Include linuxdeploy's otherwise excluded desktop libraries, not glibc.

    ldd is only run on our own binaries and SHA-256 checked dependency binaries.
    GPU vendor drivers remain provided by the target desktop's graphics stack.
    """
    excluded = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
                "ld-linux-x86-64.so.2", "libresolv.so.2", "libutil.so.1"}
    libraries = root / "usr/lib"; libraries.mkdir(exist_ok=True)
    env = dict(environment, LD_LIBRARY_PATH=str(libraries) + os.pathsep + environment["LD_LIBRARY_PATH"])
    for path in list(root.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        result = subprocess.run(["ldd", str(path)], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if "not found" in result.stdout:
            raise ValueError("Missing runtime dependency: " + str(path) + "\n" + result.stdout)
        if result.returncode and "not a dynamic executable" not in result.stdout and "statically linked" not in result.stdout:
            raise ValueError("Could not inspect runtime dependencies: " + str(path))
        for name, filename in re.findall(r"^\s*(\S+) => (/\S+) \(", result.stdout, re.MULTILINE):
            if name in excluded or name.startswith("libnss_"):
                continue
            target = libraries / name
            if not target.exists():
                shutil.copy2(Path(filename).resolve(), target)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-root", type=Path, help="Pinned Qt SDK's gcc_64 directory")
    parser.add_argument("--work-directory", type=Path, default=PROJECT / ".cache/linux-package")
    parser.add_argument("--destination", type=Path, default=PROJECT / "dist")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--build-directory", type=Path, help="Reuse a development CMake build; configure/build/tests still run")
    parser.add_argument("--whisper-build-directory", type=Path, help="Reuse a trusted development backend build after checking its source against the pinned archive")
    parser.add_argument("--release", action="store_true", help="Require clean sources and full verification")
    parser.add_argument("--skip-tests", action="store_true", help="Diagnostic build; explicitly recorded in manifest")
    parser.add_argument("--real-smoke", action="store_true", help="Also download pinned public speech/model and verify installed GUI")
    parser.add_argument("--result-file", type=Path, help="Write the exact candidate filename, hash and stage after verification")
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("Build on Linux x86_64")
    if not 1 <= args.jobs <= 256:
        parser.error("--jobs must be 1–256")
    if args.real_smoke and args.skip_tests:
        parser.error("--real-smoke requires the GUI test harness; omit --skip-tests")
    if os.geteuid() == 0:
        parser.error("Run packaging as a regular user (the container wrapper sets the caller's UID)")
    lock = json.loads((PROJECT / "packaging/linux/dependencies.json").read_text())
    source_lock = json.loads((PROJECT / "packaging/source-inputs.json").read_text(encoding="utf-8"))
    sources = source_records(source_lock, lock["downloads"].values(), lock["qt"])
    sdk_sources = sdk_source_records(source_lock, lock["qt"])
    sources += sdk_sources
    sources += runtime_source_records(source_lock, lock["downloads"]["runtime"])
    for name in ["cmake", "c++", "git", "readelf", "patchelf", "desktop-file-validate", "dpkg-query", "ldconfig", "apt-cache", "apt-get"]:
        if not shutil.which(name):
            parser.error("Missing build tool " + name + "; use bash scripts/package-linux.sh --container")
    qmake = args.qt_root / "bin/qmake" if args.qt_root else Path(shutil.which("qmake6") or shutil.which("qmake") or "missing")
    if not qmake.is_file():
        parser.error("Specify --qt-root or use --container")
    qt_version = subprocess.check_output([str(qmake), "-query", "QT_VERSION"], text=True).strip()
    if qt_version != lock["qt"]:
        parser.error(f"Expected Qt {lock['qt']}, found {qt_version}; specify --qt-root or use --container")
    work = args.work_directory.resolve(); work.mkdir(parents=True, exist_ok=True)
    cache = work / "downloads"; cache.mkdir(exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="run-", dir=work))
    print("Build logs and staging: " + str(stage), flush=True)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORMTHEME="generic", QT_QUICK_BACKEND="software")
    qt_libs = subprocess.check_output([str(qmake), "-query", "QT_INSTALL_LIBS"], text=True).strip()
    qt_plugins = subprocess.check_output([str(qmake), "-query", "QT_INSTALL_PLUGINS"], text=True).strip()
    qt_qml = subprocess.check_output([str(qmake), "-query", "QT_INSTALL_QML"], text=True).strip()
    env.update(LD_LIBRARY_PATH=qt_libs, QT_PLUGIN_PATH=qt_plugins, QML_IMPORT_PATH=qt_qml)

    def run(name, command, *, cwd=PROJECT, environment=env):
        print(name, flush=True)
        log = stage / (name + ".log")
        with log.open("wb") as output:
            result = subprocess.run(list(map(str, command)), cwd=cwd, env=environment, stdout=output, stderr=subprocess.STDOUT, check=False)
        if result.returncode:
            raise RuntimeError(f"{name} failed ({result.returncode}); see {log}")

    identity = validate_identity(PROJECT, release=args.release, skip_tests=args.skip_tests)
    dependencies = {name: fetch(record, cache) for name, record in lock["downloads"].items()}
    for record in sources:
        fetch(record, cache)
    build = args.build_directory.resolve() if args.build_directory else stage / "build"
    appdir = stage / "Transcribe.AppDir"
    configure = ["cmake", "-S", PROJECT, "-B", build, "-DCMAKE_BUILD_TYPE=Release", "-DTRANSCRIBE_BUILD_GUI=ON",
                 "-DTRANSCRIBE_WARNINGS_AS_ERRORS=ON", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
                 "-DBUILD_TESTING=" + ("OFF" if args.skip_tests else "ON")]
    if args.qt_root:
        configure.append("-DCMAKE_PREFIX_PATH=" + str(args.qt_root.resolve()))
    run("configure", configure)
    run("build", ["cmake", "--build", build, "--parallel", args.jobs])
    if not args.skip_tests:
        run("ctest", ["ctest", "--test-dir", build, "--output-on-failure"])
        run("qmllint", ["cmake", "--build", build, "--target", "all_qmllint"])
    run("install", ["cmake", "--install", build, "--prefix", appdir / "usr"])
    tools = appdir / "usr/bin/tools"; tools.mkdir()
    licenses = appdir / "usr/share/transcribe/licenses"; licenses.mkdir(parents=True)
    collect_qt_notices(source_lock["qt"]["downloads"], cache, licenses / "Qt-source-notices")
    collect_runtime_notices(source_lock, lock["downloads"]["runtime"], dependencies["runtime"], cache, licenses)
    collect_standalone_notices(dependencies["yt-dlp"], lock["downloads"]["yt-dlp"]["sha256"],
                              licenses / "yt-dlp-embedded-notices")
    source = stage / "whisper-source"; extract_sources(dependencies["whisper"], source)
    whisper = source / ("whisper.cpp-" + lock["downloads"]["whisper"]["revision"])
    whisper_build = stage / "whisper-build"
    if args.whisper_build_directory:
        whisper_build = args.whisper_build_directory.resolve()
        cache_text = (whisper_build / "CMakeCache.txt").read_text()
        configured_source = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$", cache_text, re.MULTILINE)
        if not configured_source:
            raise ValueError("Backend cache has no source directory")
        cached_source = Path(configured_source[1]).resolve()
        # Upstream CMake generates this JS metadata in the source directory.
        # Reproduce its exact pinned content before comparing all source files.
        cmake_text = (whisper / "CMakeLists.txt").read_text()
        version = ".".join(re.search(r"set\(WHISPER_VERSION_" + part + r" (\d+)\)", cmake_text)[1]
                           for part in ["MAJOR", "MINOR", "PATCH"]) + "-dev"
        template = (whisper / "bindings/javascript/package-tmpl.json").read_text()
        (whisper / "bindings/javascript/package.json").write_text(template.replace("@WHISPER_VERSION@", version))
        if inventory(cached_source) != inventory(whisper):
            raise ValueError("Cached backend source differs from the pinned archive")
        whisper = cached_source
    run("whisper-configure", ["cmake", "-S", whisper, "-B", whisper_build, "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_SHARED_LIBS=ON", "-DGGML_NATIVE=OFF", "-DGGML_BACKEND_DL=ON", "-DGGML_CPU_ALL_VARIANTS=ON", "-DGGML_OPENMP=OFF",
        "-DGGML_CUDA=OFF", "-DGGML_VULKAN=OFF", "-DGGML_BLAS=OFF", "-DWHISPER_BUILD_TESTS=OFF", "-DWHISPER_BUILD_IS_DEV=ON"])
    # Baseline, AVX and AVX2 modules are runtime-selected. Building the CLI alone
    # does not build these MODULE targets. Never require the build machine's ISA.
    run("whisper-build", ["cmake", "--build", whisper_build, "--target", "whisper-cli",
        "ggml-cpu-x64", "ggml-cpu-sandybridge", "ggml-cpu-haswell", "--parallel", args.jobs])
    shutil.copy2(whisper_build / "bin/whisper-cli", tools / "whisper-cli")
    for path in (whisper_build / "bin").glob("*.so*"):
        if path.name.startswith("libggml-cpu-") and path.name not in {
            "libggml-cpu-x64.so", "libggml-cpu-sandybridge.so", "libggml-cpu-haswell.so"}:
            continue
        shutil.copy2(path, tools / path.name)
    shutil.copy2(whisper / "LICENSE", licenses / "whisper-MIT.txt")
    ffmpeg = stage / "ffmpeg"; extract_sources(dependencies["ffmpeg"], ffmpeg)
    for name in ["ffmpeg", "ffprobe"]:
        matches = list(ffmpeg.rglob("bin/" + name))
        if len(matches) != 1:
            raise ValueError("Expected one FFmpeg tool: " + name)
        shutil.copy2(matches[0], tools / name)
    for path in ffmpeg.rglob("*"):
        if path.is_file() and path.name.startswith(("LICENSE", "COPYING")):
            shutil.copy2(path, licenses / ("FFmpeg-" + path.name))
    collect_ffmpeg_notices(source_lock, lock["downloads"]["ffmpeg"], cache, licenses,
                          subprocess.check_output([str(tools / "ffmpeg"), "-buildconf"], stderr=subprocess.STDOUT, text=True))
    shutil.copy2(dependencies["yt-dlp"], tools / "yt-dlp")
    for path in tools.iterdir():
        path.chmod(0o755)
    (tools / ".transcribe-bundle").write_text("1\n")
    for name in ["linuxdeploy", "qt-plugin", "appimagetool"]:
        dependencies[name].chmod(0o755)
    deploy_env = dict(env, APPIMAGE_EXTRACT_AND_RUN="1", QMAKE=str(qmake.resolve()),
        QML_SOURCES_PATHS=str(PROJECT / "gui"), EXTRA_PLATFORM_PLUGINS="libqoffscreen.so;libqwayland-egl.so;libqwayland-generic.so",
        EXTRA_QT_MODULES="waylandclient", PATH=str(cache) + os.pathsep + env["PATH"])
    command = [dependencies["linuxdeploy"], "--appdir", appdir, "--executable", appdir / "usr/bin/transcribe-gui",
        "--executable", appdir / "usr/bin/transcribe", "--desktop-file", PROJECT / "packaging/linux/transcribe-gui.desktop",
        "--icon-file", PROJECT / "packaging/linux/transcribe-gui.svg", "--plugin", "qt"]
    # Qt's platform plugins also load these integration plugins dynamically.
    # Deploy their dependencies explicitly; they aren't ELF DT_NEEDED entries
    # of the GUI and the pinned Qt plugin does not deploy them for waylandclient.
    for category in ["wayland-graphics-integration-client", "wayland-shell-integration",
                     "wayland-decoration-client", "xcbglintegrations"]:
        target_directory = appdir / "usr/plugins" / category
        target_directory.mkdir(parents=True, exist_ok=True)
        plugins = sorted((Path(qt_plugins) / category).glob("*.so"))
        if not plugins:
            raise ValueError("Missing Qt integration plugins: " + category)
        for plugin in plugins:
            target = target_directory / plugin.name
            shutil.copy2(plugin, target)
            command += ["--deploy-deps-only", target]
    for name in ["whisper-cli", "ffmpeg", "ffprobe", "yt-dlp"]:
        command += ["--executable", tools / name]
    # TLS libraries are loaded by Qt's TLS plugin; fontconfig is also excluded
    # from linuxdeploy's default library set. Explicitly include these runtimes.
    libraries = subprocess.check_output(["ldconfig", "-p"], text=True)
    for name in ["libssl.so.3", "libcrypto.so.3", "libfontconfig.so.1"]:
        matches = [line.split(" => ", 1)[1].strip() for line in libraries.splitlines()
                   if line.strip().startswith(name + " ") and "x86-64" in line and " => " in line]
        if not matches:
            raise ValueError("Missing runtime library in build environment: " + name)
        command += ["--library", matches[0]]
    run("deploy", command, environment=deploy_env)
    complete_libraries(appdir, env)
    # linuxdeploy's executable deployment can also copy tools to usr/bin. Keep
    # the private tools directory as the single authoritative tool location.
    for name in ["whisper-cli", "ffmpeg", "ffprobe", "yt-dlp"]:
        (appdir / "usr/bin" / name).unlink(missing_ok=True)
    (tools / "whisper-cli").rename(tools / "whisper-cli.bin")
    shutil.copy2(PROJECT / "packaging/linux/whisper-cli", tools / "whisper-cli")
    (tools / "whisper-cli").chmod(0o755)
    (appdir / "AppRun").unlink(missing_ok=True)
    shutil.copy2(PROJECT / "packaging/linux/AppRun", appdir / "AppRun"); (appdir / "AppRun").chmod(0o755)
    # appimagetool materializes the desktop symlink and creates .DirIcon.
    # Do this before hashing so the final payload has exactly this inventory.
    (appdir / "transcribe-gui.desktop").unlink()
    shutil.copy2(appdir / "usr/share/applications/transcribe-gui.desktop", appdir / "transcribe-gui.desktop")
    with (appdir / "transcribe-gui.desktop").open("a") as desktop:
        desktop.write("X-AppImage-Version=" + identity["source"][:12] + "\n")
    (appdir / ".DirIcon").unlink(missing_ok=True)
    (appdir / ".DirIcon").symlink_to("transcribe-gui.svg")
    for name, source_path in [("Transcribe-MIT.txt", PROJECT / "LICENSE"),
                             ("nlohmann-json-MIT.txt", PROJECT / "third_party/nlohmann/LICENSE.MIT")]:
        shutil.copy2(source_path, licenses / name)
    for path in (PROJECT / "packaging/windows/licenses").glob("*"):
        shutil.copy2(path, licenses / path.name)
    fonts = appdir / "usr/share/fonts"; fonts.mkdir(parents=True)
    font = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    notice = Path("/usr/share/doc/fonts-dejavu-core/copyright")
    if not font.is_file() or not notice.is_file():
        raise ValueError("Install fonts-dejavu-core in the builder (or use --container)")
    shutil.copy2(font, fonts / font.name); shutil.copy2(notice, licenses / "DejaVu-copyright.txt")
    shutil.copy2(PROJECT / "packaging/linux/fonts.conf", appdir / "usr/share/transcribe/fonts.conf")
    shutil.copy2("/etc/ssl/certs/ca-certificates.crt", appdir / "usr/share/transcribe/ca-certificates.crt")
    shutil.copy2("/usr/share/doc/ca-certificates/copyright", licenses / "ca-certificates-copyright.txt")
    library_provenance = appdir / "usr/share/transcribe/linux-library-provenance.json"
    system_libraries = collect_system_notices(appdir, licenses, Path(qt_libs))
    system_libraries = collect_sdk_notices(system_libraries, sdk_sources, cache, licenses)
    system_libraries = collect_data_notices(system_libraries, appdir, licenses,
        [dict(payload="usr/share/fonts/DejaVuSans.ttf", original=font, package="fonts-dejavu-core"),
         dict(payload="usr/share/transcribe/ca-certificates.crt", original=Path("/etc/ssl/certs/ca-certificates.crt"),
              package="ca-certificates", generator=Path("/usr/sbin/update-ca-certificates"))])
    write_provenance(system_libraries, library_provenance)
    system_sources = collect_sources(system_libraries, cache)
    source_provenance = appdir / "usr/share/transcribe/linux-source-provenance.json"
    write_provenance(system_sources, source_provenance)
    shutil.copy2(PROJECT / "packaging/linux/THIRD-PARTY.md", appdir / "usr/share/transcribe/THIRD-PARTY.md")
    shutil.copy2(PROJECT / "packaging/linux/dependencies.json", appdir / "usr/share/transcribe/dependencies.json")
    maximum = check_glibc(appdir, lock["minimum_glibc"])
    validate_identity(PROJECT, identity, args.release, args.skip_tests)
    compiler = re.search(r"^CMAKE_CXX_COMPILER:FILEPATH=(.+)$", (build / "CMakeCache.txt").read_text(), re.MULTILINE)
    if not compiler:
        raise ValueError("Cannot determine the configured C++ compiler")
    manifest = dict(identity, schema=1, platform="Linux x86_64", qt=qt_version,
        release=args.release,
        toolchain={name: subprocess.check_output(command, text=True).splitlines()[0] for name, command in
                   [("compiler", [compiler[1], "--version"]), ("cmake", ["cmake", "--version"]), ("python", ["python3", "--version"])]},
        glibc_required=maximum, glibc_baseline=lock["minimum_glibc"], tested=not args.skip_tests,
        real_smoke=args.real_smoke, dependencies=lock, source_inputs=source_lock, files=inventory(appdir))
    (appdir / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    image = stage / "Transcribe-x86_64.AppImage"
    run("appimage", [dependencies["appimagetool"], "--no-appstream", "--runtime-file", dependencies["runtime"],
        appdir, image], environment=dict(deploy_env, ARCH="x86_64", VERSION=identity["source"][:12]))
    image.chmod(0o755)
    image_hash = digest(image)
    smoke = ["python3", PROJECT / "tests/smoke_linux_package.py", image, "--source", identity["source"],
             "--artifacts", stage / "smoke-evidence"]
    if args.real_smoke:
        prerequisites = stage / "real-prerequisites"
        run("real-prerequisites", ["python3", PROJECT / "tests/prepare_real_smoke.py", prerequisites,
            "--ffmpeg", tools / "ffmpeg"])
        smoke += ["--real-gui", build / "gui/test_gui_real", "--prerequisites", prerequisites]
    run("package-smoke", smoke, environment=env)
    inputs_archive = stage / "build-inputs.tar.gz"
    archive_inputs(list(lock["downloads"].values()) + sources + system_sources["downloads"], cache, inputs_archive,
                   [PROJECT / "packaging/linux/dependencies.json", PROJECT / "packaging/source-inputs.json",
                    appdir / "package-manifest.json", library_provenance, source_provenance,
                    PROJECT / "scripts/package_git_sources.py", PROJECT / "scripts/package_canonical_sources.py",
                    PROJECT / "scripts/package_source.py", PROJECT / "scripts/package_svn_sources.py"])
    validate_identity(PROJECT, identity, args.release, args.skip_tests)
    destination = args.destination.resolve(); destination.mkdir(parents=True, exist_ok=True)
    # Publish only a completely checked candidate, never overwrite a previous build.
    target = destination / ("Transcribe-" + identity["application_version"] + "-linux-x86_64-" + ("release-" if args.release else "diagnostic-") + identity["source"][:12] + "-" + stage.name + ".AppImage")
    publish_file(image, target, image_hash)
    target.with_suffix(target.suffix + ".sha256").write_text(digest(target) + "  " + target.name + "\n")
    target.with_suffix(target.suffix + ".json").write_text(json.dumps(manifest, indent=2) + "\n")
    inputs_target = target.with_suffix(target.suffix + ".build-inputs.tar.gz")
    publish_file(inputs_archive, inputs_target, digest(inputs_archive))
    inputs_target.with_suffix(inputs_target.suffix + ".sha256").write_text(digest(inputs_target) + "  " + inputs_target.name + "\n")
    if args.result_file:
        result = dict(filename=target.name, sha256=digest(target), source=identity["source"],
                      stage=stage.name, real_smoke=args.real_smoke)
        args.result_file.write_text(json.dumps(result, indent=2) + "\n")
    print("Verified package: " + str(target), flush=True)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Интеграционные тесты: настоящий FFmpeg, подмены сети и распознавания."""
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import wave

PROJECT = Path(__file__).resolve().parents[1]
MOCK = r'''#!/usr/bin/env python3
import json, os, shutil, sys, wave
from pathlib import Path
args = sys.argv[1:]
kind = Path(sys.argv[0]).name
with open(os.environ["MOCK_LOG"], "a") as stream:
    stream.write(json.dumps([kind, args], ensure_ascii=False) + "\n")
def get(flag):
    return args[args.index(flag) + 1]
if kind == "yt-dlp":
    if os.environ.get("FAIL_DOWNLOAD"):
        sys.exit(23)
    assert "--no-playlist" in args and "--ignore-config" in args
    assert args[-2] == "--" and args[-1].startswith("https://")
    assert get("-f") == "bestaudio/best"
    destination = Path(get("-o").replace("%(ext)s", "wav"))
    shutil.copyfile(os.environ["MOCK_INPUT"], destination)
    index = args.index("--print-to-file")
    assert args[index + 1] == "after_move:%(filepath)s"
    Path(args[index + 2]).write_text(str(destination) + "\n")
else:
    if os.environ.get("FAIL_ENGINE"):
        sys.exit(17)
    assert get("--language") == "ru" and "--no-gpu" in args
    with wave.open(get("--file"), "rb") as audio:
        assert audio.getframerate() == 16000
        assert audio.getnchannels() == 1
        assert audio.getsampwidth() == 2
    prefix = get("--output-file")
    for extension in ("txt", "srt", "vtt"):
        Path(prefix + "." + extension).write_text("Тестовая расшифровка.\n", encoding="utf-8")
'''


class Pipeline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which("g++") or not shutil.which("ffmpeg"):
            raise unittest.SkipTest("Нужны g++ и ffmpeg")
        cls.shared = tempfile.TemporaryDirectory(prefix="vklecture-tests-")
        cls.binary = Path(cls.shared.name) / "vklecture"
        subprocess.run(["g++", "-std=c++23", "-O2", "-Wall", "-Wextra",
                        "-Wpedantic", "-Werror", str(PROJECT / "src/main.cpp"),
                        "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.shared.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="vklecture-case-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = self.root / "app data"
        engine = self.data / "whisper.cpp/build/bin/whisper-cli"
        engine.parent.mkdir(parents=True)
        engine.write_text(MOCK)
        engine.chmod(0o755)
        models = self.data / "models"
        models.mkdir()
        (models / "ggml-medium-q5_0.bin").write_bytes(b"test-model")
        (models / "ggml-silero-v6.2.0.bin").write_bytes(b"test-vad")
        (self.data / "default-model").write_text("medium\n")
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        downloader = bin_dir / "yt-dlp"
        downloader.write_text(MOCK)
        downloader.chmod(0o755)
        self.audio = self.root / "Лекция '$(touch PWNED)' & 1.wav"
        with wave.open(str(self.audio), "wb") as stream:
            stream.setparams((2, 2, 44100, 0, "NONE", "not compressed"))
            samples = b"".join(struct.pack("<hh", n, n) for i in range(22050)
                               for n in [int(1000 * math.sin(i * 2 * math.pi * 440 / 44100))])
            stream.writeframes(samples)
        self.log = self.root / "calls.jsonl"
        self.env = dict(os.environ, VKLECTURE_HOME=str(self.data),
                        PATH=str(bin_dir) + os.pathsep + os.environ["PATH"],
                        MOCK_LOG=str(self.log), MOCK_INPUT=str(self.audio))
        self.out = self.root / "Результаты с пробелами"

    def invoke(self, *args, env=None):
        return subprocess.run([str(self.binary), "--out", str(self.out), *args],
                              env=env or self.env, cwd=self.root,
                              capture_output=True, text=True)

    def calls(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def result(self):
        return next(self.out.iterdir())

    def test_local_file_conversion_exports_cleanup(self):
        original = self.audio.read_bytes()
        completed = self.invoke(str(self.audio))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        for name in ("transcript.txt", "transcript.srt", "transcript.vtt", "source.txt"):
            self.assertTrue((self.result() / name).is_file())
        self.assertFalse((self.result() / "audio").exists())
        self.assertEqual(self.audio.read_bytes(), original)
        self.assertFalse((self.root / "PWNED").exists())
        self.assertEqual([call[0] for call in self.calls()], ["whisper-cli"])

    def test_url_arguments_cookies_prompt_and_keep_audio(self):
        url = "https://vkvideo.ru/video-1_2?x=$(touch PWNED)&y='lecture'"
        browser = "chromium:/profile path/Default"
        prompt = "Тензор, базис; $(touch PWNED)"
        completed = self.invoke("--cookies-from-browser", browser, "--prompt", prompt,
                                "--threads", "2", "--keep-audio", url)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        downloader, engine = self.calls()
        self.assertEqual(downloader[1][-1], url)
        self.assertEqual(downloader[1][downloader[1].index("--cookies-from-browser") + 1], browser)
        self.assertEqual(engine[1][engine[1].index("--prompt") + 1], prompt)
        self.assertEqual(engine[1][engine[1].index("--threads") + 1], "2")
        self.assertTrue((self.result() / "audio/lecture.wav").is_file())
        self.assertFalse((self.root / "PWNED").exists())

    def test_no_vad_and_custom_model(self):
        (self.data / "models/ggml-silero-v6.2.0.bin").unlink()
        model = self.root / "Моя модель.bin"
        model.write_bytes(b"custom-test-model")
        completed = self.invoke("--no-vad", "--model", str(model), str(self.audio))
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertNotIn("--vad", self.calls()[0][1])
        self.assertIn(str(model), self.calls()[0][1])

    def test_engine_error_preserves_audio(self):
        completed = self.invoke(str(self.audio), env=dict(self.env, FAIL_ENGINE="1"))
        self.assertEqual(completed.returncode, 17)
        self.assertTrue((self.result() / "audio/lecture.wav").is_file())
        self.assertNotIn("Готово.", completed.stdout)

    def test_download_error_stops_pipeline(self):
        completed = self.invoke("https://vkvideo.ru/video-1_2", env=dict(self.env, FAIL_DOWNLOAD="1"))
        self.assertEqual(completed.returncode, 23)
        self.assertEqual([call[0] for call in self.calls()], ["yt-dlp"])
        self.assertNotIn("Готово.", completed.stdout)

    def test_invalid_input_and_options(self):
        for args in [("--threads", "0", str(self.audio)),
                     ("--threads", "4x", str(self.audio)),
                     ("--model",), ("missing.mp4",),
                     ("[https://vkvideo.ru/video](https://vkvideo.ru/video)",),
                     ("--unknown", str(self.audio))]:
            with self.subTest(args=args):
                self.assertNotEqual(self.invoke(*args).returncode, 0)
        self.assertFalse(self.out.exists())

    def test_ffmpeg_failure_stops_before_engine(self):
        self.audio.write_bytes(b"not audio")
        completed = self.invoke(str(self.audio))
        self.assertNotEqual(completed.returncode, 0)
        self.assertFalse(self.log.exists())
        self.assertTrue(self.audio.is_file())
        self.assertNotIn("Готово.", completed.stdout)

    def test_repeated_runs_have_distinct_directories(self):
        for _ in range(2):
            completed = self.invoke(str(self.audio))
            self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(len(list(self.out.iterdir())), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)

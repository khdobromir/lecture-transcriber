"""Independent subprocess/JSON reader for real CLI on Linux and Windows."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
import wave

CLI, MOCK = map(Path, sys.argv[1:3])
sys.argv = sys.argv[:1]


class MachineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="transcribe-machine-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        suffix = ".exe" if os.name == "nt" else ""
        self.binary = self.root / ("transcribe" + suffix)
        shutil.copy2(CLI, self.binary)
        tools = self.root / "tools"
        tools.mkdir()
        for name in ["ffmpeg", "whisper-cli", "yt-dlp"]:
            shutil.copy2(MOCK, tools / (name + suffix))
        self.data = self.root / "data"
        engine = self.data / "whisper.cpp/build/bin" / ("whisper-cli" + suffix)
        engine.parent.mkdir(parents=True)
        shutil.copy2(MOCK, engine)
        models = self.data / "models"
        models.mkdir()
        (self.data / "default-model").write_text("medium\n", encoding="utf-8")
        (models / "ggml-medium-q5_0.bin").write_bytes(b"fixture")
        (models / "ggml-silero-v6.2.0.bin").write_bytes(b"fixture")
        self.audio = self.root / "Лекция 😀 & $(touch PWNED).wav"
        with wave.open(str(self.audio), "wb") as out:
            out.setnchannels(1)
            out.setsampwidth(2)
            out.setframerate(16000)
            out.writeframes(b"\x01\x01" * 32000)
        self.output = self.root / "Результаты 😀"
        self.env = dict(os.environ, TRANSCRIBE_HOME=str(self.data),
                        PATH=str(tools) + os.pathsep + os.environ["PATH"],
                        TRANSCRIBE_MOCK_AUDIO=str(self.audio),
                        TRANSCRIBE_MOCK_DELAY="10")

    def launch(self, extra=(), source=None):
        child = subprocess.Popen([str(self.binary), "--machine", "--out", str(self.output),
                                  *extra, "--", source or str(self.audio)],
                                 cwd=self.root, env=self.env, stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.addCleanup(lambda: child.kill() if child.poll() is None else None)
        self.addCleanup(child.stdin.close)
        self.addCleanup(child.stdout.close)
        self.addCleanup(child.stderr.close)
        return child

    def collect(self, child, initial=()):
        # Keep control pipe open: communicate() closes stdin, which is cancellation.
        lines = []
        reader = threading.Thread(target=lambda: lines.extend(child.stdout.readlines()), daemon=True)
        reader.start()
        child.wait(timeout=12)
        reader.join(timeout=2)
        self.assertFalse(reader.is_alive())
        events = list(initial) + [json.loads(line.decode("utf-8")) for line in lines]
        self.assertTrue(events)
        self.assertTrue(all(event["protocol"] == 1 for event in events))
        self.assertEqual(events[0]["type"], "hello")
        return events

    def result(self):
        roots = list(self.output.iterdir())
        self.assertEqual(len(roots), 1)
        return roots[0]

    def wait_for_partial(self, child):
        events = []
        for line in iter(child.stdout.readline, b""):
            event = json.loads(line)
            events.append(event)
            if event["type"] != "progress":
                continue
            # The initial progress event can precede worker startup on Windows.
            # Cancellation preservation must be tested after text is published.
            partial = Path(event["result"]) / "transcripts/transcript.txt"
            if partial.exists() and "Текст 😀" in partial.read_text(encoding="utf-8"):
                return events
        self.fail("Backend exited before publishing partial text")

    def test_mock_download_rejects_truncated_report_arguments(self):
        suffix = ".exe" if os.name == "nt" else ""
        tool = self.root / "tools" / ("yt-dlp" + suffix)
        for report in (["--print-to-file"], ["--print-to-file", "format"]):
            with self.subTest(report=report):
                child = subprocess.run([str(tool), "-o", str(self.root / "mock.wav"), *report],
                                       env=self.env, capture_output=True, timeout=5)
                self.assertEqual(child.returncode, 1)
                self.assertIn("missing download report", child.stderr.decode("utf-8"))

    def test_success_unicode_and_multiple_parts(self):
        child = self.launch(["--chunks", "2", "--jobs", "2"])
        events = self.collect(child)
        self.assertEqual(child.returncode, 0, child.stderr.read().decode("utf-8"))
        self.assertEqual(events[-1]["type"], "completed")
        self.assertEqual(events[-1]["code"], 0)
        self.assertTrue(any(event["type"] == "progress" for event in events))
        root = self.result()
        self.assertEqual(json.loads((root / "result.json").read_text(encoding="utf-8"))["status"], "completed")
        text = (root / "transcripts/transcript.txt").read_text(encoding="utf-8")
        self.assertIn("Текст 😀 1", text)
        self.assertLess(text.index("1"), text.index("2"))
        self.assertFalse((root / "audio").exists())
        self.assertFalse((self.root / "PWNED").exists())

    def test_cancel_eof_and_malformed_control(self):
        for command, expected in [(b'{"type":"cancel"}\n', 130), (None, 141),
                                  (b'{"type":123}\n', 141), (b'x' * 9000, 141)]:
            with self.subTest(command=command and command[:30]):
                if self.output.exists():
                    shutil.rmtree(self.output)
                self.env["TRANSCRIBE_MOCK_DELAY"] = "30000"
                self.env["TRANSCRIBE_MOCK_START_DELAY"] = "250"
                child = self.launch()
                prefix = self.wait_for_partial(child)
                if command is None:
                    child.stdin.close()
                else:
                    child.stdin.write(command)
                    child.stdin.flush()
                events = self.collect(child, prefix)
                self.assertEqual(child.returncode, expected)
                self.assertEqual(events[-1]["status"], "interrupted")
                root = self.result()
                self.assertTrue((root / "audio").exists())
                self.assertIn("Текст 😀", (root / "transcripts/transcript.txt").read_text(encoding="utf-8"))

    def test_missing_input_structured_failure(self):
        child = self.launch(source=str(self.root / "missing.wav"))
        events = self.collect(child)
        self.assertNotEqual(child.returncode, 0)
        self.assertEqual(events[-1]["type"], "failed")
        self.assertFalse(self.output.exists())

    def test_lost_output_pipe_preserves_partial_result(self):
        self.env["TRANSCRIBE_MOCK_DELAY"] = "30000"
        self.env["TRANSCRIBE_MOCK_START_DELAY"] = "250"
        child = self.launch()
        self.wait_for_partial(child)
        child.stdout.close()
        child.wait(timeout=12)
        self.assertEqual(child.returncode, 141)
        root = self.result()
        self.assertTrue((root / "audio").exists())
        self.assertEqual(json.loads((root / "result.json").read_text(encoding="utf-8"))["status"], "interrupted")
        self.assertIn("Текст 😀", (root / "transcripts/transcript.txt").read_text(encoding="utf-8"))

    def test_invalid_option_structured_failure(self):
        child = self.launch(["--threads", "0"])
        events = self.collect(child)
        self.assertNotEqual(child.returncode, 0)
        self.assertEqual(events[-1]["type"], "failed")

    def test_machine_flag_as_prompt_value_keeps_human_mode(self):
        child = subprocess.run([str(self.binary), "--prompt", "--machine", "--out", str(self.output),
                                str(self.root / "missing.wav")], env=self.env, cwd=self.root,
                               capture_output=True, timeout=12)
        self.assertNotEqual(child.returncode, 0)
        self.assertEqual(child.stdout, b"")
        self.assertIn("Ошибка", child.stderr.decode("utf-8"))

    def test_url_cache_reuse(self):
        cache = self.root / "cache"
        for _ in range(2):
            child = self.launch(["--cache-dir", str(cache)], "https://example.org/video")
            events = self.collect(child)
            self.assertEqual(child.returncode, 0, child.stderr.read().decode("utf-8"))
        self.assertTrue(any(event.get("stage") == "cache" for event in events))


unittest.main()

#!/usr/bin/env python3
"""Optional real-engine smoke test; never changes the user's installation."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

PROJECT = Path(__file__).resolve().parents[1]


def check(binary, home):
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise RuntimeError(f"Нет исполняемой программы: {binary}\n"
                           "Соберите её через CMake или укажите --binary ПУТЬ.")
    if not home.is_dir():
        raise RuntimeError(f"Каталог установки не найден: {home}\n"
                           "Укажите существующую установку через --app-home ПУТЬ "
                           "или выполните bash install.sh medium.")
    engine = home / "whisper.cpp/build/bin/whisper-cli"
    sample = home / "whisper.cpp/samples/jfk.wav"
    if not engine.is_file() or not os.access(engine, os.X_OK):
        raise RuntimeError(f"Нет исполняемого whisper-cli: {engine}\n"
                           "Проверьте --app-home или выполните bash install.sh medium.")
    if not sample.is_file():
        raise RuntimeError(f"Нет технического примера: {sample}\n"
                           "Нужны исходники whisper.cpp с samples/jfk.wav.")
    if not shutil.which("ffmpeg"):
        raise RuntimeError("Нет ffmpeg в PATH. Установите зависимости из README.")
    models = {}
    for line in (PROJECT / "scripts/models.tsv").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        selection, filename, _, _, expected = line.split()
        if selection not in ("medium", "vad"):
            continue
        model = home / "models" / filename
        if not model.is_file():
            raise RuntimeError(f"Нет модели: {model}\n"
                               "Для проверки нужны medium и Silero VAD. "
                               "Проверьте --app-home или выполните bash install.sh medium.")
        digest = hashlib.sha256()
        with model.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != expected:
            raise RuntimeError(f"SHA-256 модели не совпадает: {model}")
        models[selection] = model
    with tempfile.TemporaryDirectory(prefix="transcribe-real-smoke-") as directory:
        root = Path(directory)
        data = root / "app data"
        (data / "whisper.cpp/build/bin").mkdir(parents=True)
        (data / "whisper.cpp/build/bin/whisper-cli").symlink_to(engine)
        (data / "models").mkdir()
        for model in models.values():
            (data / "models" / model.name).symlink_to(model)
        (data / "default-model").write_text("medium\n")
        audio = root / "technical sample.wav"
        subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
                        "-i", str(sample), "-t", "5", "-ar", "16000", "-ac", "1",
                        "-c:a", "pcm_s16le", str(audio)], check=True)
        original = audio.read_bytes()
        started = time.monotonic()
        subprocess.run([str(binary), "--out", str(root / "results"), str(audio)],
                       env=dict(os.environ, TRANSCRIBE_HOME=str(data)), check=True, timeout=300)
        results = list((root / "results").iterdir())
        if len(results) != 1:
            raise RuntimeError("Ожидался один каталог результата.")
        result = results[0]
        for filename in ("transcript.txt", "transcript.srt", "transcript.vtt", "source.txt"):
            if not (result / filename).is_file():
                raise RuntimeError(f"Не создан файл результата: {filename}")
        if (result / "audio").exists() or audio.read_bytes() != original:
            raise RuntimeError("Не удалены рабочие файлы или изменён локальный исходник.")
        print(f"Real smoke passed: medium-q5_0, Silero VAD 6.2.0, ru, "
              f"5 seconds of samples/jfk.wav; wall time {time.monotonic() - started:.1f}s")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=PROJECT / "build/transcribe",
                        help="собранная программа transcribe")
    parser.add_argument("--app-home", type=Path,
                        default=Path(os.environ.get("TRANSCRIBE_HOME") or
                                     Path.home() / ".local/share/transcribe"),
                        help="каталог с whisper.cpp и models; по умолчанию "
                             "TRANSCRIBE_HOME или ~/.local/share/transcribe")
    args = parser.parse_args()
    try:
        check(args.binary.resolve(), args.app_home.resolve())
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as error:
        parser.exit(1, f"Ошибка проверки: {error}\n")


if __name__ == "__main__":
    main()

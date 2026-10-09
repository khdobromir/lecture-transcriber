# Transcribe v0.4.0 — кандидат

Выпуск ещё не опубликован. Допуск бинарных пакетов отслеживается в
[отчёте приёмки](release-validation-v0.4.0.md).

## Изменения

- Qt Quick GUI для Linux и Windows: одна задача, live preview, прогресс/ETA,
  отмена/повтор, история, загрузка и импорт моделей с проверкой SHA-256.
- Самостоятельный CLI сохранён; общий pipeline и protocol v1 обслуживают GUI.
- Linux AppImage включает Qt/QML, Whisper CPU, FFmpeg/ffprobe и yt-dlp.
- Windows ZIP включает app-local CRT, Qt и инструменты, Unicode paths и
  runtime CPU dispatch. Поиск tools/backend ограничен переносимым комплектом.
- Устойчивые отмена, публикация exports, sample bounds, фоновые читатели,
  загрузка моделей и history recovery покрываются регрессионными тестами.
- Упаковщики проверяют source identity и готовый архив; CI тестирует один
  AppImage checksum в builder и clean containers.

## Переход с v0.3.0

CLI defaults и форматы TXT/SRT/VTT сохранены. Замена приложения не удаляет
модели, результаты и настройки. На Linux каталог данных остаётся
`~/.local/share/transcribe` или `TRANSCRIBE_HOME`; GUI распознаёт существующие
результаты и legacy history. Windows хранит данные в `%LOCALAPPDATA%/Transcribe`.
Recognition model и VAD импортируются отдельно; моделей внутри пакетов нет.

## Установка и ограничения

Windows: распакуйте весь ZIP, запустите `bin/transcribe-gui.exe`.
Linux: разрешите исполнение AppImage и запустите его; без FUSE используйте
`--appimage-extract-and-run`. Для CLI из исходников используйте README/install.sh.

Целевая матрица: Windows 11 x64; Linux x86_64, glibc 2.39+, Ubuntu 24.04,
Debian 13 и Arch/Omarchy. Native Windows 11 и Ubuntu desktop приёмка ещё открыты.
CPU-only; GPU, macOS/ARM, musl, diarization, resume ASR и автоматическое обновление
не входят в выпуск. Дробление остаётся опциональным (`chunks=1` по умолчанию).
Технический smoke не измеряет WER или качество длинных лекций.

Перед публикацией ZIP/AppImage должны быть готовы corresponding sources,
build materials и notices всех фактически включённых компонентов. Исходный
архив Transcribe сам по себе это требование не закрывает.

# Внешние зависимости

MIT в корне репозитория распространяется на собственный код проекта.
В релиз исходников не включаются внешние программы, библиотеки, модели и
тестовое аудио. Установщик получает whisper.cpp и модели из источников ниже;
системные инструменты устанавливаются отдельно пакетным менеджером.

| Компонент | Назначение | Лицензия и источник |
|---|---|---|
| whisper.cpp / ggml | Локальный C++-движок, CPU backend | [MIT, v1.9.4](https://github.com/ggml-org/whisper.cpp/blob/v1.9.4/LICENSE) |
| Модели Whisper GGML | small, medium, turbo | [MIT, карточка поставщика](https://huggingface.co/ggerganov/whisper.cpp); [исходный Whisper](https://github.com/openai/whisper/blob/main/LICENSE) |
| Silero VAD GGML | Определение речи | [MIT, карточка поставщика](https://huggingface.co/ggml-org/whisper-vad); [Silero VAD](https://github.com/snakers4/silero-vad/blob/master/LICENSE) |
| yt-dlp | Загрузка медиа, импорт cookies | [Unlicense для собственного исходного кода](https://github.com/yt-dlp/yt-dlp/blob/master/LICENSE); зависимости и готовые сборки могут иметь дополнительные лицензии |
| FFmpeg | Подготовка WAV | [LGPL-2.1-or-later либо GPL в зависимости от сборки](https://ffmpeg.org/legal.html) |

Программа запускает FFmpeg, yt-dlp и whisper-cli отдельными процессами.
При самостоятельном распространении внешних исходников, бинарников или моделей
сохраняйте относящиеся к ним лицензии и уведомления об авторстве.

## Закреплённые модели

Ревизии репозиториев, имена файлов и SHA-256 находятся в `scripts/models.tsv`.
Хеши получены 1 октября 2026 года из `siblings[].lfs.sha256` официального
[API Whisper](https://huggingface.co/api/models/ggerganov/whisper.cpp?blobs=true)
и [API VAD](https://huggingface.co/api/models/ggml-org/whisper-vad?blobs=true).
Загрузчики используют URL с полной ревизией, а не веткой `main`.
Для обновления моделей заново получите метаданные, проверьте лицензию и
совместимость с закреплённым whisper-cli, затем обновите манифест и проверки.

GCC, CMake, Git, curl, Bash, coreutils, util-linux и инструменты CI
предоставляются окружением разработки/установки и не входят в релиз проекта.

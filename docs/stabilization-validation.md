# Проверка кандидатов стабилизации

Статусы: «пройдено» только с проверкой указанного SHA; «не проверено» при
отсутствии результата. Этот документ не объявляет готовность к выпуску.
Перечень сценариев: [stabilization-scenarios.md](stabilization-scenarios.md).

## Исходный снимок

SHA `ec4f2d0755f9cd873bb74531fc7e311b87afb0b8`, проверка CI получена 2026-10-05.
[Run 37235121107](https://github.com/khdobromir/lecture-transcriber/actions/runs/37235121107): success.

| Job | Результат |
|---|---|
| [Windows MSVC CLI](https://github.com/khdobromir/lecture-transcriber/actions/runs/37235121107/job/111532579308) | Пройдено |
| [Arch](https://github.com/khdobromir/lecture-transcriber/actions/runs/37235121107/job/111532579524) | Пройдено |
| [Ubuntu GCC](https://github.com/khdobromir/lecture-transcriber/actions/runs/37235121107/job/111532579527) | Пройдено |
| [Static analysis](https://github.com/khdobromir/lecture-transcriber/actions/runs/37235121107/job/111532579534) | Пройдено |
| [Windows Qt GUI/ZIP](https://github.com/khdobromir/lecture-transcriber/actions/runs/37235121107/job/111532579549) | Пройдено; smoke staging, проверка распакованного итогового ZIP отсутствует |
| [Linux Qt 6.8.3](https://github.com/khdobromir/lecture-transcriber/actions/runs/37235121107/job/111532579557) | Пройдено |
| [Debian](https://github.com/khdobromir/lecture-transcriber/actions/runs/37235121107/job/111532579574) | Пройдено |
| [Ubuntu Clang 18](https://github.com/khdobromir/lecture-transcriber/actions/runs/37235121107/job/111532579645) | Пройдено |

Логи доступны в каждом job; лог Windows GUI/ZIP получен отдельно через GitHub
API. Артефакт `Transcribe-windows-x64`, ID `11315457314`, размер 137725713 bytes,
digest upload archive `46716f6db4713e09e0475024e521c2d0f325dcb90c49e14df13452905318d82d`.
Это digest контейнера GitHub artifact, **не** отдельного пользовательского ZIP.
Закреплённые зависимости: [dependencies.json](../packaging/windows/dependencies.json)
и [models.tsv](../scripts/models.tsv). Qt 6.8.3, MSVC x64, whisper.cpp
`927cfce34f31707e17f2bff35c349632fb9e2c3a`; FFmpeg/yt-dlp сверяются builder по SHA-256.

Новые FILE/SPLIT/MODEL/HISTORY/PROTOCOL/GUI регрессии плана на исходном SHA не
проверены. Исходный CI не доказывает их исправление. Native readers, 256 частей,
416 recovery и насыщение истории имеют воспроизводящие сценарии в перечне.

## Последовательные кандидаты

- PR [#5](https://github.com/khdobromir/lecture-transcriber/pull/5), source head
  `d0339adbd598ec41376405d2992917c0718d3661`:
  [CI 37281520322](https://github.com/khdobromir/lecture-transcriber/actions/runs/37281520322),
  8/8 jobs success. FILE-1/FILE-2 подтверждены native Windows Server 2022 x64
  (MSVC и Qt 6.8.3), включая воспроизведение старого QFile reader conflict,
  публикацию при открытом SharedReader, release barrier, bounded lock/cancel.
- PR [#6](https://github.com/khdobromir/lecture-transcriber/pull/6), source head
  `1fe9d515fb4bf341c87c8adcf66b32dc203b05ac`:
  [CI 37282320950](https://github.com/khdobromir/lecture-transcriber/actions/runs/37282320950),
  8/8 jobs success. Настоящий pinned Windows FFmpeg прошёл 1/2/17/256 частей,
  длинные Unicode paths, точные sample bounds, отмену и ошибку второй группы.
  Linux FFmpeg 6.1 и 9 проверены теми же сценариями. Это проверка PR tree;
  окончательный кандидат/ZIP потребует отдельного CI на точном source SHA.

- PR [#7](https://github.com/khdobromir/lecture-transcriber/pull/7), source head
  `31ec954e89516e7360bef7414ab775e568b98475`:
  [CI 37285883531](https://github.com/khdobromir/lecture-transcriber/actions/runs/37285883531),
  8/8 jobs success. MODEL-1/MODEL-2/HISTORY-1 прошли Linux и native Windows
  Qt 6.8.3: 416/reset, strict ranges, разрыв/отмена/retry, lock release,
  импорт из установленного файла, 2000 known + новый, missing/restart и tie order.

## Ручные границы допуска кандидата

Локальная проверка protocol/process tree выполнена Clang ASan/UBSan с
`detect_leaks=0`: LeakSanitizer с `detect_leaks=1` завершается собственной fatal
диагностикой ограничения ptrace в данном окружении. Настройка ядра не менялась;
проверка утечек этим прогоном не подтверждается. Проверка памяти и UB активна.

Чистая Windows 11 без Qt/VS/Python/Git: не проверено. Настоящая русская запись с
фиксированным SHA на обеих ОС: не проверено для нового кандидата. Установленный
GUI на Ubuntu и Omarchy/Hyprland (portal dialogs, clipboard, exports, DPI,
палитры, клавиатура): не проверено для нового кандидата. X11, Windows multi-group
и ограничения Linux cgroup требуют отдельного описания испытанного окружения.

Кандидат не допускается к выпуску, пока обязательные ручные сценарии остаются
не проверенными. Release/tag в рамках реализации плана автоматически не создаются.

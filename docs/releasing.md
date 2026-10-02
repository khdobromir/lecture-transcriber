# Подготовка и публикация выпуска

Разработка и подготовка выпуска выполняются в `dev`; тег ставится на итоговый
коммит `master` после слияния PR и успешного CI. Репозиторий распространяет
исходники: GitHub предоставляет ZIP и tar.gz, модели и бинарники не прикладываются.

## Подготовка в dev

1. Подтяните `origin/master` и устраните конфликты до подготовки версии.
2. Обновите единственный номер версии в `project(... VERSION ...)` в CMake,
   README, CHANGELOG и `docs/release-notes-vX.Y.Z.md`.
3. Выполните проверки CONTRIBUTING для GCC и Clang из чистой копии исходников
   с `-Werror`. Проверьте `transcribe --version`, сборку без тестов и установку
   CLI во временный prefix.
4. Запишите результаты и границы проверки в `docs/release-validation-vX.Y.Z.md`.
5. Создайте коммит Conventional Commits, например
   `chore(release): prepare v0.2.1`, отправьте `dev` и создайте PR в `master`.
6. Дождитесь успешного CI для актуального коммита и PR: Ubuntu GCC, Ubuntu Clang,
   Debian GCC, Arch Linux и статический анализ (clang-tidy, cppcheck, ShellCheck).
   Заметки GitHub Release можно заранее сохранить как черновик.

Черновик выпуска не заменяет проверку итоговой `master`. Если после подготовки
изменились код, зависимости или конфликтное слияние, повторите относящиеся
к изменениям проверки.

## Слияние и публикация v0.2.1

После слияния PR дождитесь успешного CI на итоговом коммите `master`.
Не удаляйте `dev`: дальнейшая разработка продолжается в этой ветке.
Для коммита слияния используйте Conventional Commits, например
`chore(release): merge v0.2.1 into master`.
Синхронизируйте локальную `master` и убедитесь, что она совпадает с `origin/master`:

```bash
git switch master
git pull --ff-only origin master
git status --short
git rev-parse HEAD origin/master
```

Проверьте, что в этом коммите версия CMake равна `0.2.1`, и что тег `v0.2.1`
ещё не существует локально и на GitHub. Создайте аннотированный тег именно на
проверенном коммите:

```bash
git tag -a v0.2.1 -m "Transcribe CLI v0.2.1"
git push origin v0.2.1
```

Если черновик GitHub Release уже подготовлен, опубликуйте его после отправки
тега, сохранив заметки из `docs/release-notes-v0.2.1.md`:

```bash
gh release edit v0.2.1 --draft=false --latest \
  --title "Transcribe CLI v0.2.1" \
  --notes-file docs/release-notes-v0.2.1.md
```

Если черновика нет, создайте выпуск по существующему тегу:

```bash
gh release create v0.2.1 --verify-tag --latest \
  --title "Transcribe CLI v0.2.1" \
  --notes-file docs/release-notes-v0.2.1.md
```

Проверьте страницу выпуска, скачивание исходников и соответствие тега
проверенному коммиту. AUR-пакет и переносимые бинарники этим процессом не создаются.

Официальная справка CLI:
[создание выпуска](https://cli.github.com/manual/gh_release_create),
[редактирование и публикация черновика](https://cli.github.com/manual/gh_release_edit).

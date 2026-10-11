#include "cli.hpp"
#include "pipeline.hpp"
#include "platform.hpp"
#include "process.hpp"
#include "progress.hpp"
#include "protocol.hpp"
#include "windows.hpp"
#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace fs = std::filesystem;
void help() {
    std::cout << R"(Transcribe CLI — транскрипция русской речи на CPU

Использование:
  transcribe [параметры] "URL или путь к файлу"

Параметры:
  --model small|medium|turbo|ПУТЬ.bin  Модель (по умолчанию выбранная при установке)
  --threads N                        Потоки каждого whisper-cli (1–256)
                                     По умолчанию физические ядра / число работников
  --chunks N                         Число частей (1–256, по умолчанию 1)
  --jobs N                           Одновременные части (не больше --chunks)
                                     По умолчанию до двух при дроблении
  --out КАТАЛОГ                      Родительский каталог результатов
                                     (по умолчанию $HOME/Transcriptions)
  --cache-dir КАТАЛОГ                Кэш скачанного аудио
  --cache-limit-gib N                 Лимит кэша в ГиБ (по умолчанию 10)
  --no-cache                         Не читать и не сохранять кэш
  --refresh-cache                    Скачать заново и обновить кэш
  --machine                          JSON Lines события; stdin принимает cancel
  --no-progress                      Не выводить промежуточный прогресс
  --cookies-from-browser СПЕЦ         Например firefox или chromium:ПРОФИЛЬ
  --cookies ФАЙЛ                     Файл cookies в формате Netscape
  --prompt ТЕКСТ                     Краткий список терминов лекции
  --no-vad                           Отключить определение участков речи
  --keep-audio                       Оставить рабочие аудио/видеофайлы
  --help                             Эта справка
  --version                          Версия программы
  --                                 Конец параметров

Результаты: transcripts/transcript.txt, .srt, .vtt; logs/*.log; source.txt.
Каждый запуск создаёт отдельный каталог; исходный локальный файл не удаляется.
TXT дополняется во время распознавания в порядке записи; субтитры — после успеха.
Linux: Ctrl+C, SIGTERM и SIGHUP останавливают работников и сохраняют частичный текст.
Windows: Ctrl+C/Ctrl+Break или отмена GUI завершают работников через Job Object.
Закрытие консольного окна Windows ограничено системным deadline.
)";
}


int entry(const std::vector<std::string>& arguments) {
    std::unique_ptr<transcribe::MachineControl> control;
    try {
        transcribe::install_signal_handlers();
        std::vector<std::string_view> args(arguments.begin(), arguments.end());
        const auto action = transcribe::cli_action(args);
        if (action == transcribe::CliAction::help || action == transcribe::CliAction::usage) { help(); return action == transcribe::CliAction::usage ? 2 : 0; }
        if (action == transcribe::CliAction::version) { std::cout << "transcribe " << TRANSCRIBE_VERSION << '\n'; return 0; }
        if (transcribe::machine_requested(args)) control = std::make_unique<transcribe::MachineControl>();
        const auto options = transcribe::parse_arguments(args, transcribe::physical_cpus());
        if (transcribe::oversubscribed(options, transcribe::logical_cpus()))
            std::cerr << "Число работников × потоки превышает доступные CPU; скорость может снизиться.\n";
        transcribe::Progress progress(options.progress && !options.machine);
        transcribe::Pipeline pipeline([&](const transcribe::Event& event) {
            using transcribe::EventType;
            if (options.machine) { transcribe::write_machine_event(event); return; }
            if (event.type != EventType::progress) progress.finish();
            if (event.type == EventType::result) std::cout << (event.stage == "temporary" ? "Временный каталог результата: " : "Каталог результата: ") << transcribe::path_utf8(event.result) << '\n';
            else if (event.type == EventType::stage) {
                if (event.stage == "probe") std::cout << "[1/3] Получение названия и идентификатора видео...\n";
                else if (event.stage == "cache") std::cout << "[1/3] Используется скачанное аудио из кэша\n";
                else if (event.stage == "download") std::cout << "[1/3] Скачивание медиа по URL...\n";
                else if (event.stage == "local") std::cout << "[1/3] Используется локальный файл\n";
                else if (event.stage == "prepare") std::cout << "[2/3] WAV: моно, 16 кГц, PCM 16 бит...\n";
                else if (event.stage == "recognize") std::cout << "[3/3] Русская речь, CPU, " << event.message << "...\n";
                else if (event.stage == "split" && !event.message.empty()) std::cout << event.message << '\n';
                else if (event.stage == "merge") std::cout << "Объединение и проверка расшифровок...\n";
            } else if (event.type == EventType::progress) progress.update(event.fraction, event.finished, event.total);
            else if (event.type == EventType::finalizing) std::cout << "Готово. Распознавание заняло " << std::fixed << std::setprecision(1) << std::stod(event.message) << " мин.\n" << transcribe::path_utf8(event.result) << '\n';
            else if (event.type == EventType::warning) std::cerr << "Предупреждение: " << event.message << '\n';
            else if (event.type == EventType::failed) {
                std::cerr << "Ошибка: " << event.message << '\n';
                if (!event.result.empty()) std::cerr << "Промежуточные файлы оставлены в " << transcribe::path_utf8(event.result) << '\n';
            }
            std::cout.flush();
            transcribe::check_cancelled();
            if (!std::cout) throw std::runtime_error("Не удалось вывести итог обработки");
        });
        return pipeline.run(options, {transcribe::app_home(), fs::current_path()}).code;
    } catch (const std::exception& error) {
        const int code = transcribe::cancellation_signal() ? 128 + transcribe::cancellation_signal() : 1;
        if (control) try { transcribe::write_machine_event(transcribe::Event{.type = transcribe::EventType::failed, .message = error.what(), .status = "failed", .code = code}); } catch (...) { // NOLINT(bugprone-empty-catch): the original error survives a lost event channel.
        }
        std::cerr << "Ошибка: " << error.what() << '\n'; return code;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(transcribe::narrow_utf8(argv[i]));
    return entry(args);
}
#else
int main(int argc, char** argv) { return entry({argv + 1, argv + argc}); }
#endif

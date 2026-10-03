#include "support.hpp"
#include "media.hpp"
#include "progress.hpp"
#include "result.hpp"
#include <array>
#include <limits>

using namespace test;
using namespace transcribe;
namespace {
struct CacheFixture {
    Temp temp;
    fs::path root = temp.path / "cache", media = temp.path / "input";
    SourceInfo source{"Название\nс кавычками \" и %", "Mock", "id", false};
    CacheFixture() { write(media, "1234"); }
};
}
int main() {
    Suite suite;
    suite.add("title_unicode_paths_controls_and_utf8_limit", [] {
        CHECK(safe_title("Лекция 1") == "Лекция 1");
        CHECK(safe_title("../../file\\name\n\t") == "_.._file_name__");
        CHECK(safe_title(" ... ") == "video" && safe_title("") == "video");
        std::string title;
        for (int i = 0; i < 100; ++i) title += "Ж";
        CHECK(safe_title(title).size() == 180 && safe_title(title) == title.substr(0, 180));
        CHECK(safe_title("hi\xff") == "hi_");
    });
    suite.add("result_names_collisions_layout_and_no_replace_rename", [] {
        Temp temp;
        const auto now = std::chrono::system_clock::from_time_t(0);
        auto first = ResultPaths::create(temp.path, "Лекция", now);
        auto second = ResultPaths::create(temp.path, "Лекция", now);
        CHECK(first.root.filename().string().starts_with("Лекция_") && second.root.string() == first.root.string() + "_2");
        CHECK(fs::is_directory(first.logs()) && fs::is_directory(first.transcripts()) && fs::is_directory(first.work()));
        CHECK((fs::status(first.root).permissions() & fs::perms::others_all) == fs::perms::none);
        auto provisional = ResultPaths::create(temp.path, "video", now);
        write(provisional.logs() / "metadata.log", "diagnostics");
        write(first.root / "keep", "first");
        provisional.name("Лекция");
        CHECK(provisional.root.string() == first.root.string() + "_3");
        CHECK(read(first.root / "keep") == "first" && read(provisional.logs() / "metadata.log") == "diagnostics");
    });
    suite.add("weighted_progress_unequal_parts_parallel_queue", [] {
        CHECK(weighted_progress({{1, 100}, {3, 0}}) == 0.25);
        CHECK(weighted_progress({{1, 100}, {3, 50}}) == 0.625);
        CHECK(weighted_progress({{1, 100}, {3, 100}}) == 1);
        CHECK(weighted_progress({}) == 0 && weighted_progress({{-1, 100}}) == 0);
    });
    suite.add("eta_warmup_stall_regression_and_completion", [] {
        const auto start = Eta::Clock::time_point{};
        Eta eta(start);
        CHECK(!eta.update(0, start) && !eta.update(0.1, start + std::chrono::seconds(2)));
        CHECK(!eta.update(0.1, start + std::chrono::seconds(6)));
        const auto initial = eta.update(0.2, start + std::chrono::seconds(10));
        CHECK(initial && std::abs(*initial - 40) < 0.001);
        const auto stall = eta.update(0.2, start + std::chrono::seconds(20));
        CHECK(stall && *stall > *initial);
        const auto regressed = eta.update(0.1, start + std::chrono::seconds(20));
        CHECK(regressed && std::isfinite(*regressed));
        CHECK(eta.update(1, start + std::chrono::seconds(30)) == 0);
        CHECK(!eta.update(std::numeric_limits<double>::quiet_NaN(), start));
        CHECK(eta_text({}) == "ETA: расчёт…" && eta_text(65) == "ETA ≈ 01:05");
    });
    suite.add("cache_roundtrip_offline_alias_authentication_and_copy_isolation", [] {
        CacheFixture f;
        MediaCache cache(f.root, 8);
        CHECK(cache.publish(f.source, "anonymous", "url1", f.media));
        auto entry = cache.lookup_url("url1", "anonymous"); CHECK(entry);
        CHECK(entry->source.title == f.source.title && !cache.lookup_url("url1", "browser:firefox"));
        cache.copy(*entry, f.temp.path / "copy", "url2");
        CHECK(cache.lookup_url("url2", "anonymous"));
        write(f.temp.path / "copy", "different");
        CHECK(read(entry->directory / "media") == "1234");
        CHECK(cache.lookup_source(f.source, "anonymous"));
        auto generic = f.source; generic.extractor = "Generic";
        CHECK(cache.publish(generic, "anonymous", "generic", f.media));
        CHECK(cache.lookup_url("generic", "anonymous") && !cache.lookup_source(generic, "anonymous"));
    });
    suite.add("cache_lru_capacity_oversize_and_refresh", [] {
        CacheFixture f;
        MediaCache cache(f.root, 8);
        CHECK(cache.publish(f.source, "anonymous", "first", f.media));
        auto second = f.source; second.id = "2";
        CHECK(cache.publish(second, "anonymous", "second", f.media));
        auto first = cache.lookup_url("first", "anonymous"); CHECK(first);
        fs::last_write_time(first->directory / "meta", fs::file_time_type::clock::now() + std::chrono::seconds(1));
        auto third = f.source; third.id = "3";
        CHECK(cache.publish(third, "anonymous", "third", f.media));
        CHECK(cache.lookup_url("first", "anonymous") && !cache.lookup_url("second", "anonymous") && cache.lookup_url("third", "anonymous"));
        write(f.media, "5678"); CHECK(cache.publish(f.source, "anonymous", "first", f.media));
        CHECK(read(cache.lookup_url("first", "anonymous")->directory / "media") == "5678");
        write(f.media, "123456789"); CHECK(!cache.publish(f.source, "anonymous", "big", f.media));
        CHECK(!cache.lookup_url("big", "anonymous") && !cache.lookup_url("first", "anonymous"));
    });
    suite.add("cache_corrupt_and_symlink_entries_are_misses", [] {
        CacheFixture f;
        MediaCache cache(f.root, 8);
        CHECK(cache.publish(f.source, "anonymous", "url", f.media));
        const auto entry = cache.lookup_url("url", "anonymous"); CHECK(entry);
        write(entry->directory / "media", ""); CHECK(!cache.lookup_url("url", "anonymous"));
        fs::remove(entry->directory / "media"); test::link(f.media, entry->directory / "media");
        CHECK(!cache.lookup_url("url", "anonymous") && read(f.media) == "1234");
    });
    suite.add("cache_rejects_symlink_root_and_lock", [] {
        CacheFixture f;
        fs::create_directory(f.temp.path / "outside"); test::link(f.temp.path / "outside", f.root);
        bool rejected = false;
        try { MediaCache cache(f.root, 8); } catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected && entries(f.temp.path / "outside") == 0);
        fs::remove(f.root); fs::create_directory(f.root); fs::permissions(f.root, fs::perms::owner_all);
        test::link(f.media, f.root / "lock"); rejected = false;
        try { MediaCache cache(f.root, 8); } catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected && read(f.media) == "1234");
    });
    suite.add("cache_lower_limit_counts_corrupt_payloads_and_removes_abandoned_staging", [] {
        CacheFixture f;
        {
            MediaCache cache(f.root, 8);
            CHECK(cache.publish(f.source, "anonymous", "url", f.media));
            const auto entry = cache.lookup_url("url", "anonymous"); CHECK(entry);
            write(entry->directory / "meta", "invalid");
            write(f.root / "entries/.pending-abandoned/media", "1234");
        }
        MediaCache limited(f.root, 3);
        CHECK(entries(f.root / "entries") == 0);
    });
    return suite.run();
}

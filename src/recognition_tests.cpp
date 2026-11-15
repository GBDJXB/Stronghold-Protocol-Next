#include "stronghold/recognition.hpp"
#include "stronghold/analysis.hpp"
#include <fstream>
#include <iostream>
#include <Windows.h>

namespace stronghold {
int run_recognition_tests() {
    const auto root = fs::temp_directory_path() / wide("recognition-" + unique_id());
    struct Cleanup { fs::path p; ~Cleanup(){ std::error_code ec; fs::remove_all(p, ec); } } cleanup{root};
    try {
        Capture c;
        c.window.hwnd = 123; c.window.pid = 456; c.window.process_created = 133000000000000001;
        c.window.width = 2; c.window.height = 1; c.window.title = "合成截图";
        c.window.visible = true; c.method = "wgc";
        c.requested_at = "2026-11-15T00:00:00.000Z"; c.completed_at = c.requested_at;
        c.frame = {2, 1, {255,0,0,0,128,255}};
        c.frame.source_time_100ns = 1;
        const auto saved = save_capture(c, root);
        const auto loaded = load_capture_archive(saved.directory);
        if (loaded.id != saved.id || loaded.capture.frame.rgb != c.frame.rgb ||
            loaded.capture.window.process_created != c.window.process_created || loaded.capture.window.title != c.window.title ||
            loaded.capture.frame.width != 2 || loaded.capture.frame.height != 1 || loaded.capture.frame.source_time_100ns != 1)
            throw Error("test_failed", "Archive did not preserve RGB pixels and source association");
        std::cout << "recognition: RGB archive and provenance passed\n";
        auto observation = analyze_capture_archive(saved.directory, {});
        if(observation.category != "unknown" || observation.source.id != saved.id ||
           observation.source.capture.frame.rgb != c.frame.rgb || observation.reason != "unsupported_dimensions")
            throw Error("test_failed", "Unsupported image analysis lost unknown or source association");
        std::cout << "recognition: unsupported screen is unknown with provenance passed\n";
        // Optional real-development fixture: paths supplied explicitly, never a heldout manifest.
        wchar_t sample[32768]{}, maa[32768]{};
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_DIFFICULTY",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed", "Development fixture requires STRONGHOLD_MAA_ROOT");
            auto classified = analyze_capture_archive(fs::path(sample), fs::path(maa));
            if(classified.category != "out_of_game" || classified.model_sha256.empty() ||
               classified.source.id.empty())
                throw Error("test_failed", "Development difficulty screen was not locally recognized with source/model provenance");
            std::cout << "recognition: development difficulty classification with neural provenance passed\n";
        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_REST",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            const auto classified=analyze_capture_archive(sample,maa);
            if(classified.category!="ordinary_in_game") throw Error("test_failed","Stable development rest was not recognized");
            std::cout << "recognition: stable development rest passed\n";
        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_COMBAT",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            const auto classified=analyze_capture_archive(sample,maa);
            if(classified.category!="ordinary_in_game") throw Error("test_failed","Stable development combat was not recognized");
            std::cout << "recognition: stable development combat passed\n";
        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_UNSUPPORTED",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            const auto classified=analyze_capture_archive(sample,maa);
            if(classified.category!="unknown") throw Error("test_failed","Unsupported development overlay/transition was confirmed as ordinary");
            std::cout << "recognition: development unsupported view rejected passed\n";
        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_MENU",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            const auto classified=analyze_capture_archive(sample,maa);
            if(classified.category!="out_of_game") throw Error("test_failed","Development main menu was not recognized");
            std::cout << "recognition: development main menu passed\n";
        }
        auto expect_error = [&](const char* code, auto action) {
            try { action(); } catch(const Error& e) {
                if(e.code == code) return;
                throw Error("test_failed", std::string("Expected ") + code + ", received " + e.code);
            }
            throw Error("test_failed", std::string("Expected error: ") + code);
        };
        auto write = [&](std::string value) { std::ofstream f(saved.directory / L"metadata.json",std::ios::binary|std::ios::trunc); f << value; };
        auto changed = [&](const std::string& from, const std::string& to) {
            auto value=loaded.metadata_json; const auto pos=value.find(from);
            if(pos==std::string::npos) throw Error("test_failed","Fixture replacement not found");
            value.replace(pos,from.size(),to); write(value);
        };
        changed("2026-11-15T00:00:00.000Z", "2026-02-30T00:00:00.000Z");
        expect_error("archive_metadata_invalid", [&]{ load_capture_archive(saved.directory); });
        std::cout << "recognition: invalid UTC timestamp rejected\n";
        changed("\"time\":\"1\"", "\"time\":\"\"");
        expect_error("archive_metadata_invalid", [&]{ load_capture_archive(saved.directory); });
        std::cout << "recognition: empty source timestamp rejected\n";
        auto timing = [&](const std::string& time, const std::string& freshness, const std::string& age) {
            auto value=loaded.metadata_json;
            const auto start=value.find("\"source_frame\":");
            const auto end=value.find(",\"window\":",start);
            if(start==std::string::npos || end==std::string::npos) throw Error("test_failed","Timing fixture not found");
            value.replace(start,end-start,"\"source_frame\":{\"clock\":\"qpc\",\"unit\":\"100ns\",\"time\":"+time+"},\"frame_freshness\":\""+freshness+"\",\"frame_age_ms_at_archive_check\":"+age);
            write(value);
        };
        timing("null","fresh","2000");
        expect_error("archive_metadata_invalid", [&]{ load_capture_archive(saved.directory); });
        std::cout << "recognition: contradictory source timing rejected\n";
        for(const auto& age : {"0", "1000"}) {
            timing("\"1\"","fresh",age);
            if(load_capture_archive(saved.directory).capture.frame.source_time_100ns!=1)
                throw Error("test_failed","Valid fresh timing lost source association");
        }
        timing("\"1\"","stale","1000.001");
        load_capture_archive(saved.directory);
        timing("null","unknown","null");
        if(load_capture_archive(saved.directory).capture.frame.source_time_100ns!=0)
            throw Error("test_failed","Null source timing did not remain unavailable");
        struct InvalidTiming { const char* time; const char* freshness; const char* age; };
        for(const auto& value : {
                InvalidTiming{"null","unknown","0"}, {"null","fresh","null"}, {"null","stale","null"},
                {"\"0\"","unknown","null"}, {"\"0\"","fresh","0"},
                {"\"1\"","unknown","null"}, {"\"1\"","unknown","0"}, {"\"1\"","fresh","null"},
                {"\"1\"","stale","1000"}, {"\"1\"","fresh","1000.001"}, {"\"1\"","fresh","-1"}}) {
            timing(value.time,value.freshness,value.age);
            expect_error("archive_metadata_invalid", [&]{ load_capture_archive(saved.directory); });
        }
        std::cout << "recognition: source timing null/zero and freshness boundaries passed\n";
        changed("\"schema_version\":2,", "");
        expect_error("archive_metadata_invalid", [&]{ load_capture_archive(saved.directory); });
        changed("\"schema_version\":2", "\"schema_version\":3");
        expect_error("archive_version", [&]{ load_capture_archive(saved.directory); });
        changed("\"width\":2", "\"width\":\"2\"");
        expect_error("archive_metadata_invalid", [&]{ load_capture_archive(saved.directory); });
        changed("raw.png", "../raw.png");
        expect_error("archive_path", [&]{ load_capture_archive(saved.directory); });
        changed("\"format\":\"RGB8\"", "\"format\":\"RGBA8\"");
        expect_error("archive_image_format", [&]{ load_capture_archive(saved.directory); });
        changed("\"width\":2,\"height\":1,\"format\"", "\"width\":1,\"height\":1,\"format\"");
        expect_error("archive_image_dimensions", [&]{ load_capture_archive(saved.directory); });
        auto huge=loaded.metadata_json;
        for(std::size_t pos=0;(pos=huge.find("\"width\":2",pos))!=std::string::npos;pos+=13) huge.replace(pos,9,"\"width\":20000");
        write(huge);
        expect_error("archive_resource_limit", [&]{ load_capture_archive(saved.directory); });
        changed("\"hwnd\":123", "\"hwnd\":9007199254740992");
        expect_error("archive_metadata_invalid", [&]{ load_capture_archive(saved.directory); });
        write("{broken"); expect_error("archive_metadata_invalid", [&]{ load_capture_archive(saved.directory); });
        write(std::string("{\"x\":\"\xff\"}")); expect_error("archive_metadata_invalid", [&]{ load_capture_archive(saved.directory); });
        write(std::string(1024*1024+1,' ')); expect_error("archive_resource_limit", [&]{ load_capture_archive(saved.directory); });
        fs::remove(saved.directory / L"metadata.json"); expect_error("archive_metadata_missing", [&]{ load_capture_archive(saved.directory); });
        write(loaded.metadata_json);
        fs::rename(saved.directory / L"raw.png",saved.directory / L"original.png");
        expect_error("archive_image_missing", [&]{ load_capture_archive(saved.directory); });
        fs::copy_file(saved.directory / L"original.png", saved.directory / L"raw.png");
        { std::fstream png(saved.directory / L"raw.png",std::ios::in|std::ios::out|std::ios::binary); png.write("bad!",4); }
        expect_error("archive_image_invalid", [&]{ load_capture_archive(saved.directory); });
        fs::copy_file(saved.directory / L"original.png", saved.directory / L"raw.png",fs::copy_options::overwrite_existing);
        if(load_capture_archive(saved.directory).metadata_json != loaded.metadata_json)
            throw Error("test_failed","Reader modified original metadata");
        const auto renamed=root / L"copied-archive"; fs::rename(saved.directory,renamed);
        if(load_capture_archive(renamed).id != saved.id) throw Error("test_failed","Frame association depends on directory name");
        std::cout << "recognition: version/type/path/resource/missing/corrupt/dimension failures and repeated reads passed\n";
        return 0;
    } catch (const Error& e) { std::cerr << e.code << ": " << e.what() << '\n'; return 1; }
}
}

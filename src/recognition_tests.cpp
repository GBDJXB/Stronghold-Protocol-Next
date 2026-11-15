#include "stronghold/recognition.hpp"
#include "stronghold/analysis.hpp"
#include "stronghold/state.hpp"
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
        auto synthetic = observation;
        synthetic.category="ordinary_in_game"; synthetic.reason="synthetic_fixture";
        synthetic.runtime_version="synthetic-runtime";
        synthetic.model_sha256=std::string(64,'e'); synthetic.dictionary_sha256=std::string(64,'f');
        synthetic.funds.status="recognized"; synthetic.funds.value=4;
        synthetic.funds.confidence=.99; synthetic.funds.reason="synthetic_fixture"; synthetic.funds.raw_text="4";
        synthetic.funds.model_sha256=std::string(64,'a'); synthetic.funds.dictionary_sha256=std::string(64,'b');
        synthetic.stage={"recognized","combat",.99,"synthetic_fixture","0/3",std::string(64,'a'),std::string(64,'b')};
        synthetic.remaining_time={"recognized",47,.99,"synthetic_fixture","47",std::string(64,'c'),std::string(64,'d')};
        const auto record_file=save_state_record(synthetic,root / L"states");
        const auto state=load_state_record(record_file);
        if(state.observation_status!="historical" || state.funds.value!=4 || state.funds.status!="recognized" ||
           state.frame_id!=saved.id || state.source_metadata_json!=loaded.metadata_json ||
           state.request_start!=c.requested_at || state.funds.model_sha256!=std::string(64,'a'))
            throw Error("test_failed","Saved state lost historical value/source/provenance");
        if(state.stage.value!="combat" || state.remaining_time.value!=47 || state.remaining_time.model_sha256!=std::string(64,'c'))
            throw Error("test_failed","State lost independent stage/time provenance");
        std::cout << "state: recognized value reloads as historical with source passed\n";
        const auto unknown_file=save_state_record(observation,root / L"states");
        if(unknown_file==record_file || load_state_record(unknown_file).funds.value ||
           load_state_record(unknown_file).funds.status!="unknown" || load_state_record(record_file).funds.value!=4)
            throw Error("test_failed","Unknown snapshot overwrote history or became zero/known");
        fs::rename(saved.directory,saved.directory.wstring()+L"-moved");
        if(load_state_record(record_file).source_metadata_json!=loaded.metadata_json)
            throw Error("test_failed","Reload incorrectly depends on original archive presence");
        fs::rename(saved.directory.wstring()+L"-moved",saved.directory);
        auto state_error=[&](const char* code,auto action) {
            try {action();} catch(const Error& e) {if(e.code==code)return;throw Error("test_failed",std::string("Expected ")+code+", got "+e.code);}
            throw Error("test_failed",std::string("Expected ")+code);
        };
        std::ifstream state_input(record_file,std::ios::binary);
        const std::string original_state((std::istreambuf_iterator<char>(state_input)),{});state_input.close();
        auto corrupt=root/L"corrupt-state.json";
        auto bad_state=[&](const std::string& from,const std::string& to,const char* code) {
            auto content=original_state;auto pos=content.find(from);if(pos==std::string::npos)throw Error("test_failed","State corruption fixture missing");
            content.replace(pos,from.size(),to);std::ofstream out(corrupt,std::ios::binary|std::ios::trunc);out<<content;out.close();
            state_error(code,[&]{load_state_record(corrupt);});
        };
        for(const auto& category : {"ordinary_in_game", "out_of_game", "special_in_game"}) {
            auto classified=observation;
            classified.category=category; classified.special_view=classified.category=="special_in_game"?"result":"";
            classified.model_sha256=std::string(64,'e'); classified.dictionary_sha256=std::string(64,'f');
            classified.runtime_version="synthetic-runtime";
            const auto complete_file=save_state_record(classified,root/L"classifier-provenance");
            std::ifstream input(complete_file,std::ios::binary);
            const std::string complete((std::istreambuf_iterator<char>(input)),{}); input.close();
            for(auto field : {&Analysis::model_sha256, &Analysis::dictionary_sha256, &Analysis::runtime_version}) {
                auto incomplete=classified; const auto present=incomplete.*field; (incomplete.*field).clear();
                state_error("state_invalid",[&]{save_state_record(incomplete,root/L"invalid-classifier-output");});
                auto content=complete; const auto at=content.find(present);
                if(at==std::string::npos) throw Error("test_failed","Missing classifier corruption fixture");
                content.erase(at,present.size());
                {std::ofstream out(corrupt,std::ios::binary|std::ios::trunc);out<<content;}
                state_error("state_invalid",[&]{load_state_record(corrupt);});
            }
        }
        if(fs::exists(root/L"invalid-classifier-output")) throw Error("test_failed","Incomplete classifier state was published");
        const auto unsupported=load_state_record(unknown_file);
        if(!unsupported.model_sha256.empty() || !unsupported.dictionary_sha256.empty() || !unsupported.runtime_version.empty())
            throw Error("test_failed","Unsupported-dimensions fixture unexpectedly has classifier provenance");
        std::cout << "state: classifier provenance validation passed\n";
        bad_state("\"schema_version\":2","\"schema_version\":99","state_version");
        bad_state("\"stage\":","\"missing_stage\":","state_invalid");
        bad_state("\"remaining_time\":","\"missing_remaining_time\":","state_invalid");
        bad_state("\"remaining_time_unit\":\"seconds\"","\"remaining_time_unit\":\"minutes\"","state_invalid");
        bad_state("\"value\":47","\"value\":null","state_invalid");
        bad_state("\"value\":\"combat\"","\"value\":\"unsupported\"","state_invalid");
        bad_state("\"value\":4","\"value\":null","state_invalid");
        bad_state("source_frame","missing_source_frame","state_invalid");
        bad_state("process_created","missing_process_created","state_invalid");
        bad_state("\"status\":\"recognized\"","\"status\":\"unknown\"","state_invalid");
        bad_state("\"observation_status\":\"historical\"","\"observation_status\":\"current\"","state_invalid");
        bad_state("\"confidence\":0.990000","\"confidence\":1.1","state_invalid");
        bad_state("\"frame_id\":","\"missing_frame_id\":","state_invalid");
        bad_state("2026-11-15T00:00:00.000Z","2026-02-30T00:00:00.000Z","state_invalid");
        {std::ofstream out(corrupt,std::ios::binary|std::ios::trunc);out<<"{";}
        state_error("state_invalid",[&]{load_state_record(corrupt);});
        {std::ofstream out(corrupt,std::ios::binary|std::ios::trunc);out<<std::string(2*1024*1024+1,' ');}
        state_error("state_resource_limit",[&]{load_state_record(corrupt);});
        state_error("state_missing",[&]{load_state_record(root/L"absent-state.json");});
        auto inconsistent=synthetic;inconsistent.funds.status="unknown";
        state_error("state_invalid",[&]{save_state_record(inconsistent,root/L"invalid-state-output");});
        if(fs::exists(root/L"invalid-state-output"))throw Error("test_failed","Invalid state was published");
        if(load_state_record(record_file).funds.value!=4 || load_capture_archive(saved.directory).capture.frame.rgb!=c.frame.rgb)
            throw Error("test_failed","Snapshots changed previous state or raw archive");
        std::cout << "state: unknown/null immutable history, moved archive and corrupt/missing/version/resource rejection passed\n";
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
            if(classified.category!="ordinary_in_game" || classified.funds.status!="unknown" || classified.funds.value) throw Error("test_failed","Combat cost was confused with funds or combat not recognized");
            std::cout << "recognition: stable development combat passed\n";
        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_SPECIAL",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            wchar_t previous_path[32768]{};
            fs::path previous_file;
            std::string previous_frame;
            if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_FUNDS_FOUR",previous_path,32768)) {
                const auto previous=analyze_capture_archive(previous_path,maa);
                if(previous.funds.value!=4) throw Error("test_failed","Previous development funds4 fixture unreadable");
                previous_frame=previous.source.id;
                previous_file=save_state_record(previous,root/L"special-history");
            }
            const auto special=analyze_capture_archive(sample,maa);
            if(special.category!="special_in_game" || special.funds.status!="unknown" || special.funds.value || special.stage.status!="unknown" || special.stage.value || special.remaining_time.status!="unknown" || special.remaining_time.value || special.source.id.empty())
                throw Error("test_failed","Supported development special view was not recognized with unknown ordinary fields and own source");
            if(special.special_view!="information" && special.special_view!="result")
                throw Error("test_failed","Special subtype missing");
            const auto special_file=save_state_record(special,root/L"special-history");
            const auto historical=load_state_record(special_file);
            if(historical.category!="special_in_game" || historical.special_view!=special.special_view ||
               historical.frame_id!=special.source.id || historical.funds.value || historical.stage.value || historical.stage.status!="unknown" || historical.remaining_time.value || historical.remaining_time.status!="unknown" || historical.observation_status!="historical" ||
               load_state_record(record_file).funds.value!=4 || load_state_record(record_file).frame_id!=saved.id)
                throw Error("test_failed","Special historical snapshot lost subtype/source or confirmed ordinary fields");
            if(!previous_file.empty()) {
                const auto previous=load_state_record(previous_file);
                if(previous.funds.value!=4 || previous.frame_id!=previous_frame || previous.frame_id==historical.frame_id)
                    throw Error("test_failed","Special view overwrote previous ordinary funds/source");
            }
            std::cout << "recognition: supported development special view passed\n";
        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_UNSUPPORTED",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            const auto classified=analyze_capture_archive(sample,maa);
            if(classified.category!="unknown" || classified.funds.status!="unknown" || classified.funds.value) throw Error("test_failed","Unsupported development overlay/transition confirmed a screen or funds value");
            std::cout << "recognition: development unsupported view rejected passed\n";
        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_MENU",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            const auto classified=analyze_capture_archive(sample,maa);
            if(classified.category!="out_of_game" || classified.funds.status!="unknown" || classified.funds.value) throw Error("test_failed","Main menu recognized funds or was not recognized");
            std::cout << "recognition: development main menu passed\n";
        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_FUNDS_FOUR",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            const auto observed=analyze_capture_archive(sample,maa);
            if(observed.category!="ordinary_in_game" || observed.funds.status!="recognized" ||
               observed.funds.value!=4 || observed.funds.confidence<.95 || observed.source.id.empty() ||
               observed.funds.model_sha256.empty() || observed.funds.dictionary_sha256.empty())
                throw Error("test_failed","Development funds4 was not recognized with source and confidence");
            const auto funds_snapshot=load_state_record(save_state_record(observed,root/L"real-funds-states"));
            if(funds_snapshot.funds.value!=4 || funds_snapshot.funds.model_sha256!=observed.funds.model_sha256 || funds_snapshot.observation_status!="historical") throw Error("test_failed","Real funds snapshot lost value/provenance");
            std::cout << "recognition: development funds4 passed\n";
            if(observed.stage.status!="recognized" || observed.stage.value!="rest" || observed.remaining_time.value || observed.remaining_time.status!="unknown")
                throw Error("test_failed","Rest stage/time were not independently represented");
            auto without_icon=observed.source.capture;
            for(int y=890;y<936;++y) for(int x=1730;x<1776;++x)
                for(int channel=0;channel<3;++channel) without_icon.frame.rgb[(y*1920+x)*3+channel]=0;
            const auto iconless=save_capture(without_icon,root);
            const auto rejected=analyze_capture_archive(iconless.directory,maa);
            if(rejected.category!="ordinary_in_game" || rejected.funds.status!="unknown" || rejected.funds.value)
                throw Error("test_failed","Funds numeral without yellow icon was accepted");
            std::cout << "recognition: synthetic missing funds icon rejected passed\n";
            auto unreadable=observed.source.capture;
            for(int y=890;y<936;++y) for(int x=1776;x<1836;++x)
                for(int channel=0;channel<3;++channel) unreadable.frame.rgb[(y*1920+x)*3+channel]=0;
            const auto blacked=save_capture(unreadable,root);
            const auto unknown=analyze_capture_archive(blacked.directory,maa);
            if(unknown.category!="ordinary_in_game" || unknown.funds.status!="unknown" || unknown.funds.value ||
               unknown.funds.reason!="unreadable_funds" || unknown.source.id!=blacked.id)
                throw Error("test_failed","Unreadable funds did not remain unknown with its own source");
            std::cout << "recognition: synthetic unreadable funds remains ordinary with unknown field passed\n";
            auto extended=observed.source.capture;
            for(int y=900;y<920;++y) for(int x=1825;x<1830;++x) {
                const auto offset=(y*1920+x)*3;
                extended.frame.rgb[offset]=255;extended.frame.rgb[offset+1]=220;extended.frame.rgb[offset+2]=0;
            }
            const auto larger=save_capture(extended,root);
            const auto bounded=analyze_capture_archive(larger.directory,maa);
            if(bounded.category!="ordinary_in_game" || bounded.funds.value || bounded.funds.status!="unknown")
                throw Error("test_failed","Extended funds glyph outside supported crop was accepted");
            std::cout << "recognition: synthetic extended funds layout rejected passed\n";



        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_FUNDS_ZERO",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            const auto observed=analyze_capture_archive(sample,maa);
            if(observed.category!="ordinary_in_game" || observed.funds.status!="recognized" || observed.funds.value!=0)
                throw Error("test_failed","Valid zero funds confused with unknown");
            std::cout << "recognition: development zero funds passed\n";
        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_COMBAT",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            const auto observed=analyze_capture_archive(sample,maa);
            if(observed.category!="ordinary_in_game" || observed.stage.value!="combat" || observed.remaining_time.value!=47 ||
               observed.remaining_time.status!="recognized" || observed.remaining_time.model_sha256.empty() || observed.funds.value)
                throw Error("test_failed","Combat stage/timer47/funds distinction failed");
            std::cout << "recognition: development combat timer47 and phase passed\n";
            auto unreadable=observed.source.capture;
            for(int y=50;y<160;++y) for(int x=1695;x<1835;++x)
                for(int channel=0;channel<3;++channel) unreadable.frame.rgb[(y*1920+x)*3+channel]=0;
            const auto blacked=save_capture(unreadable,root);
            const auto unknown=analyze_capture_archive(blacked.directory,maa);
            if(unknown.category!="ordinary_in_game" || unknown.stage.value!="combat" || unknown.remaining_time.value || unknown.remaining_time.status!="unknown" || unknown.funds.value || unknown.source.id!=blacked.id)
                throw Error("test_failed","Unreadable timer corrupted independent stage/funds/source");
            const auto historical=load_state_record(save_state_record(unknown,root/L"states"));
            if(historical.stage.value!="combat" || historical.remaining_time.value || historical.observation_status!="historical")
                throw Error("test_failed","Unreadable timer persistence changed field validity");
            std::cout << "recognition: synthetic black TV keeps stage independent and timer unknown passed\n";
        }
        if(GetEnvironmentVariableW(L"STRONGHOLD_DEVELOPMENT_WAITING",sample,32768)) {
            if(!GetEnvironmentVariableW(L"STRONGHOLD_MAA_ROOT",maa,32768)) throw Error("test_failed","Missing MAA root");
            const auto observed=analyze_capture_archive(sample,maa);
            if(observed.category!="ordinary_in_game" || observed.stage.value!="waiting" || observed.remaining_time.value!=28 || observed.funds.value)
                throw Error("test_failed","Finished waiting frame falsely became ongoing combat");
            std::cout << "recognition: development finished waiting phase passed\n";
            auto no_phase=observed.source.capture;
            for(int y=1109;y<1151;++y) for(int x=817;x<1195;++x)
                for(int channel=0;channel<3;++channel) no_phase.frame.rgb[(y*1920+x)*3+channel]=0;
            const auto missing=analyze_capture_archive(save_capture(no_phase,root).directory,maa);
            if(missing.category!="ordinary_in_game" || missing.stage.status!="unknown" || missing.stage.value || missing.remaining_time.value!=28)
                throw Error("test_failed","Completed progress without phase banner became combat");
            std::cout << "recognition: completed enemy HUD alone does not imply ongoing combat passed\n";
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

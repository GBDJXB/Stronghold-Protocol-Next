#include "stronghold/analysis.hpp"
#include <regex>
namespace stronghold {
Analysis analyze_capture_archive(const fs::path& directory, const fs::path& maa_root) {
    Analysis result; result.source = load_capture_archive(directory);
    const auto& frame = result.source.capture.frame;
    if(frame.width != 1920 || frame.height != 1200) {
        result.reason = "unsupported_dimensions"; return result;
    }
    NeuralTextRecognizer recognizer(maa_root);
    result.runtime_version = recognizer.runtime_version;
    result.model_sha256 = recognizer.model_sha256;
    result.dictionary_sha256 = recognizer.dictionary_sha256;
    const auto title = recognizer.read(frame,1535,34,275,46);
    const auto start = recognizer.read(frame,1620,1080,240,65);
    result.evidence.push_back("difficulty_title="+title.text+";confidence="+std::to_string(title.confidence));
    result.evidence.push_back("difficulty_start="+start.text+";confidence="+std::to_string(start.confidence));
    if(title.text == "请选择模拟难度" && title.confidence >= .95 &&
       start.text == "开始模拟" && start.confidence >= .95) {
        result.category = "out_of_game"; result.reason = "difficulty_selection_text";
    } else {
        auto read = [&](const char* name,int x,int y,int w,int h) {
            auto value=recognizer.read(frame,x,y,w,h);
            result.evidence.push_back(std::string(name)+"="+value.text+";confidence="+std::to_string(value.confidence));
            return value;
        };
        const auto terminal=read("main_terminal",1290,228,200,111);
        const auto team=read("main_team",974,515,133,79);
        if(terminal.text=="终端" && terminal.confidence>=.8 && team.text=="编队" && team.confidence>=.9) {
            result.category="out_of_game"; result.reason="main_menu_text_anchors"; return result;
        }
        const auto result_title=read("result_overlay",1025,548,244,65);
        const auto result_subtitle=read("result_subtitle",1034,616,164,43);
        if((result_title.confidence>=.8 && result_title.text.find("作战结束")!=std::string::npos) ||
           (result_subtitle.confidence>=.8 && result_subtitle.text.find("完美作战")!=std::string::npos)) {
            result.reason="unsupported_result_overlay"; return result;
        }
        const auto round=read("round_label",666,34,57,33);
        const auto rest=read("rest_phase",940,30,112,36);
        const auto ready=read("ready",1750,153,120,42);
        const auto refresh=read("refresh",1792,792,69,37);
        const auto close=read("shop_close",1735,1104,78,41);
        if(round.text=="回合" && round.confidence>=.95 && rest.text=="休息一下" && rest.confidence>=.95 &&
           ready.text=="准备就绪" && ready.confidence>=.95 && refresh.text=="刷新" && refresh.confidence>=.95 &&
           close.text=="收起" && close.confidence>=.95) {
            result.category="ordinary_in_game"; result.reason="stable_rest_text_anchors";
            NeuralTextRecognizer numeric_recognizer(maa_root,TextModel::Characters);
            result.funds.model_sha256=numeric_recognizer.model_sha256;
            result.funds.dictionary_sha256=numeric_recognizer.dictionary_sha256;
            const auto funds=numeric_recognizer.read(frame,1776,890,44,46);
            result.evidence.push_back("funds="+funds.text+";confidence="+std::to_string(funds.confidence));
            result.funds.raw_text=funds.text;
            result.funds.confidence=funds.confidence;
            result.funds.reason="unreadable_funds";
            // Layout guard for the yellow funds icon; never identify the white C counter as funds.
            int yellow_pixels=0;
            for(int y=890;y<936;++y) for(int x=1730;x<1776;++x) {
                const auto offset=(static_cast<size_t>(y)*frame.width+x)*3;
                const int r=frame.rgb[offset],g=frame.rgb[offset+1],b=frame.rgb[offset+2];
                if(r>=120 && g>=100 && b<=110 && r*2>=b*3 && g*10>=b*13) ++yellow_pixels;
            }
            result.evidence.push_back("funds_yellow_icon_pixels="+std::to_string(yellow_pixels));
            // Only a single gold numeral in this layout is currently supported.
            // Inspect the full right-hand amount space and crop border to reject clipped/extra glyphs.
            bool outside_glyph=false;
            for(int y=890;y<936;++y) for(int x=1772;x<1900;++x) {
                if(x>=1778 && x<1816 && y>=896 && y<931) continue;
                const auto offset=(static_cast<size_t>(y)*frame.width+x)*3;
                const int r=frame.rgb[offset],g=frame.rgb[offset+1],b=frame.rgb[offset+2];
                if(r>=180 && g>=160 && b<=140 && r-b>=70) outside_glyph=true;
            }
            if(yellow_pixels<200) result.funds.reason="funds_icon_not_supported";
            else if(outside_glyph) result.funds.reason="unsupported_funds_extent";
            else if(std::regex_match(funds.text,std::regex("[0-9]")) && funds.confidence>=.95) {
                result.funds.status="recognized";
                result.funds.value=std::stoi(funds.text);
                result.funds.reason="stable_rest_funds_text";
            }

        } else {
            const auto progress=read("enemy_progress",970,27,71,40);
            const auto covenant=read("covenant_label",811,173,55,28);
            if(round.text=="回合" && round.confidence>=.95 &&
               std::regex_match(progress.text,std::regex("[0-9]{1,3}/[0-9]{1,3}")) && progress.confidence>=.95 &&
               covenant.text=="坚守" && covenant.confidence>=.95) {
                result.category="ordinary_in_game"; result.reason="combat_text_anchors";
            } else result.reason="insufficient_supported_text";
        }
    }
    return result;
}
}

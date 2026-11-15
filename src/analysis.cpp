#include "stronghold/analysis.hpp"
#include <regex>
#include <algorithm>
#include <cmath>
namespace stronghold {
namespace {
// Fixed 1920x1200 TV face geometry: TL, BL, BR, TR. Bilinear quad
// rectification is deterministic pixel preprocessing, never digit inference.
Frame timer_face(const Frame& source) {
    Frame target{100,54,std::vector<std::uint8_t>(100*54*3)};
    for(int y=0;y<54;++y) for(int x=0;x<100;++x) {
        const double u=(x+.5)/100, v=(y+.5)/54;
        const double sx=(1-u)*(1-v)*1720+(1-u)*v*1705+u*v*1805+u*(1-v)*1821-.5;
        const double sy=(1-u)*(1-v)*63+(1-u)*v*117+u*v*145+u*(1-v)*88-.5;
        const int x0=static_cast<int>(sx),y0=static_cast<int>(sy);
        const double fx=sx-x0,fy=sy-y0;
        for(int c=0;c<3;++c) {
            auto pixel=[&](int px,int py){return source.rgb[(static_cast<size_t>(py)*source.width+px)*3+c];};
            const double top=pixel(x0,y0)*(1-fx)+pixel(x0+1,y0)*fx;
            const double bottom=pixel(x0,y0+1)*(1-fx)+pixel(x0+1,y0+1)*fx;
            target.rgb[(y*100+x)*3+c]=static_cast<std::uint8_t>(std::clamp(std::lround(top*(1-fy)+bottom*fy),0L,255L));
        }
    }
    return target;
}
}

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
    // Loading feedback can retain perfectly readable menu anchors underneath.
    const auto feedback=recognizer.read(frame,1086,1095,280,45);
    result.evidence.push_back("loading_feedback="+feedback.text+";confidence="+std::to_string(feedback.confidence));
    if(feedback.confidence>=.9 && feedback.text.find("正在提交反馈")!=std::string::npos) {
        result.reason="unsupported_loading_feedback"; return result;
    }
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
        const auto database=read("entry_database",178,297,170,42);
        const auto objectives=read("entry_objectives",225,892,142,44);
        if(database.text=="协议数据库" && database.confidence>=.95 &&
           objectives.text=="关键目标" && objectives.confidence>=.95) {
            result.category="out_of_game"; result.reason="protocol_entry_text_anchors"; return result;
        }
        const auto terminal=read("main_terminal",1290,228,200,111);
        const auto team=read("main_team",974,515,133,79);
        if(terminal.text=="终端" && terminal.confidence>=.8 && team.text=="编队" && team.confidence>=.9) {
            result.category="out_of_game"; result.reason="main_menu_text_anchors"; return result;
        }
        const auto info_core=read("information_core",108,192,165,49);
        const auto info_additional=read("information_additional",108,507,165,48);
        const auto info_leader=read("information_leader",1594,485,160,42);
        if(info_core.text=="核心盟约" && info_core.confidence>=.95 &&
           info_additional.text=="附加盟约" && info_additional.confidence>=.95 &&
           info_leader.text=="敌方领袖" && info_leader.confidence>=.95) {
            result.category="special_in_game"; result.special_view="information";
            result.reason="information_text_anchors"; return result;
        }
        const auto info_element=read("information_element",1560,671,211,39);
        const auto info_hidden=read("information_hidden",1560,747,211,39);
        if(info_leader.text=="敌方领袖" && info_leader.confidence>=.95 &&
           info_element.text=="特训敌人·元素" && info_element.confidence>=.95 &&
           info_hidden.text=="特训敌人·隐匿" && info_hidden.confidence>=.95) {
            result.category="special_in_game"; result.special_view="information";
            result.reason="scrolled_information_text_anchors"; return result;
        }
        const auto result_title=read("result_overlay",1025,548,244,65);
        const auto result_subtitle=read("result_subtitle",1034,616,164,43);
        if(result_title.confidence>=.8 && result_title.text=="作战结束" &&
           result_subtitle.confidence>=.8 && result_subtitle.text.find("完美作战")!=std::string::npos) {
            result.category="special_in_game"; result.special_view="result";
            result.reason="result_text_anchors"; return result;
        }
        if((result_title.confidence>=.8 && result_title.text.find("作战结束")!=std::string::npos) ||
           (result_subtitle.confidence>=.8 && result_subtitle.text.find("完美作战")!=std::string::npos)) {
            result.reason="unsupported_result_overlay"; return result;
        }
        const auto attack_range=read("selected_attack_range",190,503,96,31);
        if(attack_range.text=="攻击范围" && attack_range.confidence>=.95) {
            result.reason="unsupported_selected_operator"; return result;
        }
        const auto resource=read("tooltip_resource",1162,248,99,41);
        if(resource.text=="天空盒" && resource.confidence>=.95) {
            result.reason="unsupported_resource_tooltip"; return result;
        }
        const auto protection=read("tooltip_protection",1478,200,119,36);
        if(protection.text=="保护目标" && protection.confidence>=.95) {
            result.reason="unsupported_goal_tooltip"; return result;
        }
        const auto layers=read("tooltip_layers",1419,260,91,37);
        if(layers.confidence>=.95 && layers.text.find("层数")!=std::string::npos) {
            result.reason="unsupported_covenant_tooltip"; return result;
        }
        const auto round=read("round_label",666,34,57,33);
        const auto rest=read("rest_phase",940,30,112,36);
        const auto ready=read("ready",1750,153,120,42);
        const auto refresh=read("refresh",1792,792,69,37);
        const auto close=read("shop_close",1735,1104,78,41);
        if(round.text=="回合" && round.confidence>=.95 && rest.text=="休息一下" && rest.confidence>=.95 &&
           ready.text=="准备就绪" && ready.confidence>=.95 && close.confidence>=.95 &&
           ((refresh.text=="刷新" && refresh.confidence>=.95 && close.text=="收起") || close.text=="展开")) {
            result.category="ordinary_in_game"; result.reason="stable_rest_text_anchors";
            result.stage={"recognized", "rest", rest.confidence, "stable_rest_phase_text", rest.text, result.model_sha256, result.dictionary_sha256};
            result.remaining_time.reason="no_supported_visible_timer";
            if(close.text=="展开") { result.funds.reason="shop_collapsed"; return result; }
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
            const auto precision=read("covenant_precision",874,171,56,30);
            if(round.text=="回合" && round.confidence>=.95 &&
               std::regex_match(progress.text,std::regex("[0-9]{1,3}/[0-9]{1,3}")) && progress.confidence>=.95 &&
               ((covenant.text=="坚守" && covenant.confidence>=.95) ||
                (precision.text=="精准" && precision.confidence>=.95))) {
                result.category="ordinary_in_game"; result.reason="combat_text_anchors";
                const auto waiting=read("waiting_phase",817,1109,378,42);
                const auto slash=progress.text.find('/');
                if(std::stoi(progress.text.substr(0,slash))<std::stoi(progress.text.substr(slash+1)))
                    result.stage={"recognized","combat",progress.confidence,"unfinished_enemy_progress_anchors",progress.text,result.model_sha256,result.dictionary_sha256};
                else result.stage.reason="completed_enemy_progress_without_phase_anchor";
                if(waiting.text=="作战结束,等待队友完成作战" && waiting.confidence>=.95)
                    result.stage={"recognized","waiting",waiting.confidence,"finished_waiting_text",waiting.text,result.model_sha256,result.dictionary_sha256};
                NeuralTextRecognizer numeric_recognizer(maa_root,TextModel::Characters);
                result.remaining_time.model_sha256=numeric_recognizer.model_sha256;
                result.remaining_time.dictionary_sha256=numeric_recognizer.dictionary_sha256;
                const auto face=timer_face(frame);
                const auto timer=numeric_recognizer.read(face,0,0,face.width,face.height);
                result.evidence.push_back("tv_timer_quad="+timer.text+";confidence="+std::to_string(timer.confidence));
                result.remaining_time.raw_text=timer.text; result.remaining_time.confidence=timer.confidence;
                result.remaining_time.reason="unreadable_tv_timer";
                if(std::regex_match(timer.text,std::regex("[0-9]{1,3}")) && timer.confidence>=.95) {
                    result.remaining_time.status="recognized";result.remaining_time.value=std::stoi(timer.text);
                    result.remaining_time.reason="rectified_tv_seconds_text";
                }

            } else result.reason="insufficient_supported_text";
        }
    }
    return result;
}
}

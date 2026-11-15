#include "stronghold/analysis.hpp"
#include "../third_party/onnxruntime_c_api.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#pragma comment(lib, "bcrypt.lib")
namespace stronghold {
namespace {
std::string hash_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw Error("recognition_model","Cannot read OCR resource for hashing");
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE value = nullptr;
        ~Hash() { if(value) BCryptDestroyHash(value); if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); }
    } h;
    if(BCryptOpenAlgorithmProvider(&h.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 ||
       BCryptCreateHash(h.algorithm,&h.value,nullptr,0,nullptr,0,0)<0)
        throw Error("recognition_model","Cannot initialize resource SHA256");
    std::array<char,65536> buffer{};
    while(file) {
        file.read(buffer.data(),buffer.size());
        auto count=file.gcount();
        if(count && BCryptHashData(h.value,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(count),0)<0)
            throw Error("recognition_model","Cannot hash OCR resource");
    }
    if(!file.eof()) throw Error("recognition_model","Failed reading OCR resource");
    std::array<unsigned char,32> digest{};
    if(BCryptFinishHash(h.value,digest.data(),static_cast<ULONG>(digest.size()),0)<0)
        throw Error("recognition_model","Cannot finalize resource SHA256");
    std::ostringstream out; out<<std::hex<<std::setfill('0');
    for(auto b:digest) out<<std::setw(2)<<static_cast<unsigned>(b);
    return out.str();
}
}
struct NeuralTextRecognizer::Impl {
    HMODULE library=nullptr;
    const OrtApi* api=nullptr;
    OrtEnv* environment=nullptr;
    OrtSessionOptions* options=nullptr;
    OrtSession* session=nullptr;
    OrtMemoryInfo* memory=nullptr;
    std::vector<std::string> dictionary;
    ~Impl() {
        if(api) { if(memory) api->ReleaseMemoryInfo(memory); if(session) api->ReleaseSession(session);
            if(options) api->ReleaseSessionOptions(options); if(environment) api->ReleaseEnv(environment); }
        if(library) FreeLibrary(library);
    }
    void check(OrtStatus* status) {
        if(!status) return;
        std::string message=api->GetErrorMessage(status); api->ReleaseStatus(status);
        throw Error("recognition_runtime","Local neural OCR: "+message);
    }
};
NeuralTextRecognizer::NeuralTextRecognizer(const fs::path& maa_root, TextModel selection):impl(std::make_unique<Impl>()) {
    if(!maa_root.is_absolute()) throw Error("recognition_model","MAA installation root must be absolute");
    std::error_code path_error;
    const auto root=fs::canonical(maa_root,path_error);
    if(path_error) throw Error("recognition_model","MAA installation root is missing or inaccessible");
    const auto family=selection==TextModel::Characters ? L"PaddleCharOCR" : L"PaddleOCR";
    const auto model=root/L"resource"/family/L"rec"/L"inference.onnx";
    const auto keys=root/L"resource"/family/L"rec"/L"keys.txt";
    model_sha256=hash_file(model); dictionary_sha256=hash_file(keys);
    std::ifstream dictionary(keys,std::ios::binary);
    impl->dictionary.emplace_back(); // CTC blank index zero.
    std::string line;
    while(std::getline(dictionary,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line.empty()) throw Error("recognition_model","OCR dictionary contains empty entry");
        impl->dictionary.push_back(line);
    }
    if(!dictionary.eof() || impl->dictionary.size()<2) throw Error("recognition_model","Invalid OCR dictionary");
    impl->dictionary.emplace_back(" "); // PaddleOCR use_space_char.
    const auto dll=root/L"onnxruntime_maa.dll";
    impl->library=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!impl->library) throw Error("recognition_runtime","Cannot load configured MAA ONNX Runtime library");
    const auto entry=reinterpret_cast<const OrtApiBase* (ORT_API_CALL*)()>(GetProcAddress(impl->library,"OrtGetApiBase"));
    if(!entry) throw Error("recognition_runtime","Configured library lacks OrtGetApiBase");
    const auto base=entry(); impl->api=base->GetApi(ORT_API_VERSION);
    if(!impl->api) throw Error("recognition_runtime","Configured ONNX Runtime does not support API24");
    runtime_version=base->GetVersionString();
    auto& p=*impl;
    p.check(p.api->CreateEnv(ORT_LOGGING_LEVEL_WARNING,"stronghold-offline-ocr",&p.environment));
    p.check(p.api->CreateSessionOptions(&p.options));
    p.check(p.api->SetIntraOpNumThreads(p.options,1));
    p.check(p.api->CreateSession(p.environment,model.c_str(),p.options,&p.session));
    p.check(p.api->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&p.memory));
}
NeuralTextRecognizer::~NeuralTextRecognizer()=default;
TextObservation NeuralTextRecognizer::read(const Frame& frame,int x,int y,int width,int height) {
    if(frame.width<=0 || frame.height<=0 || width<=0 || height<=0 || x<0 || y<0 ||
       width>frame.width || height>frame.height || x>frame.width-width || y>frame.height-height ||
       frame.rgb.size()!=static_cast<size_t>(frame.width)*static_cast<size_t>(frame.height)*3)
        throw Error("recognition_input","Invalid neural OCR frame or crop bounds");
    const int resized_width=static_cast<int>(std::ceil(48.0*width/height));
    if(resized_width<1 || resized_width>4096) throw Error("recognition_input","Neural OCR crop aspect ratio exceeds supported bound");
    const size_t plane=static_cast<size_t>(48)*resized_width;
    std::vector<float> input(plane*3);
    // Bilinear sampling in original RGB bytes; model expects normalized BGR NCHW.
    for(int row=0;row<48;++row) for(int col=0;col<resized_width;++col) {
        const double sy=std::clamp((row+0.5)*height/48.0-0.5,0.0,static_cast<double>(height-1));
        const double sx=std::clamp((col+0.5)*width/resized_width-0.5,0.0,static_cast<double>(width-1));
        const int y0=static_cast<int>(sy), x0=static_cast<int>(sx);
        const int y1=std::min(y0+1,height-1), x1=std::min(x0+1,width-1);
        const double fy=sy-y0, fx=sx-x0;
        for(int c=0;c<3;++c) {
            const auto pixel=[&](int px,int py){return frame.rgb[(static_cast<size_t>(y+py)*frame.width+x+px)*3+(2-c)];};
            const double a=pixel(x0,y0)*(1-fx)+pixel(x1,y0)*fx;
            const double b=pixel(x0,y1)*(1-fx)+pixel(x1,y1)*fx;
            input[static_cast<size_t>(c)*plane+static_cast<size_t>(row)*resized_width+col]=static_cast<float>(((a*(1-fy)+b*fy)/255.0-0.5)/0.5);
        }
    }
    auto& p=*impl;
    struct Values {
        const OrtApi* api; OrtValue* input=nullptr; OrtValue* output=nullptr; OrtTensorTypeAndShapeInfo* shape=nullptr;
        ~Values(){if(shape)api->ReleaseTensorTypeAndShapeInfo(shape);if(output)api->ReleaseValue(output);if(input)api->ReleaseValue(input);}
    } values{p.api};
    const int64_t shape[]{1,3,48,resized_width};
    p.check(p.api->CreateTensorWithDataAsOrtValue(p.memory,input.data(),input.size()*sizeof(float),shape,4,ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&values.input));
    const char* input_name="x";const char* output_name="fetch_name_0";
    const OrtValue* input_value=values.input;
    p.check(p.api->Run(p.session,nullptr,&input_name,&input_value,1,&output_name,1,&values.output));
    p.check(p.api->GetTensorTypeAndShape(values.output,&values.shape));
    size_t rank=0; p.check(p.api->GetDimensionsCount(values.shape,&rank));
    ONNXTensorElementDataType type; p.check(p.api->GetTensorElementType(values.shape,&type));
    if(rank!=3 || type!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) throw Error("recognition_model","Unexpected OCR tensor type or rank");
    int64_t dims[3];p.check(p.api->GetDimensions(values.shape,dims,3));
    if(dims[0]!=1 || dims[1]<=0 || dims[1]>4096 || dims[2]!=static_cast<int64_t>(p.dictionary.size()))
        throw Error("recognition_model","Unexpected OCR output shape or dictionary mismatch");
    float* data=nullptr;p.check(p.api->GetTensorMutableData(values.output,reinterpret_cast<void**>(&data)));
    TextObservation result;size_t count=0;int64_t previous=-1;
    for(int64_t t=0;t<dims[1];++t) {
        const float* step=data+t*dims[2];int64_t best=0;
        for(int64_t c=0;c<dims[2];++c) {
            if(!std::isfinite(step[c]) || step[c]<0 || step[c]>1.0001f) throw Error("recognition_model","Invalid OCR probabilities");
            if(step[c]>step[best]) best=c;
        }
        if(best!=0 && best!=previous) {result.text+=p.dictionary[static_cast<size_t>(best)];result.confidence+=step[best];++count;}
        previous=best;
    }
    if(count) result.confidence/=count;
    return result;
}
}

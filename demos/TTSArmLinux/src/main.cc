#include <cstdlib>
#include <iostream>
#include <sstream>
#include <iterator>
#include <memory>
#include <string>
#include <map>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <paddle_api.h>
#include <front/front_interface.h>
#include "Predictor.hpp"

using namespace paddle::lite_api;

DEFINE_string(sentence, "你好，欢迎使用语音合成服务", "Text to be synthesized (Chinese only. English will crash the program.)");
DEFINE_string(front_conf, "./front.conf", "Front configuration file");
DEFINE_string(acoustic_model, "./models/cpu/fastspeech2_mix_static_0.2.0.nb", "Acoustic model .nb file");
DEFINE_string(phone_id_data_type, "int64", "Input 0 data type of acoustic model: int32, int64 or float");
DEFINE_string(speaker_id, "1", "Speaker id (A number: 0, 1, 2... Leave blank if the model does not have this parameter or the program may crash.)");
DEFINE_string(speaker_id_data_type, "int64", "Input 1 data type of acoustic model: int32, int64 or float");
DEFINE_string(vocoder, "./models/cpu/mb_melgan_csmsc_static_0.1.1.nb", "vocoder .nb file");
DEFINE_string(output_wav, "./output/tts.wav", "Output WAV file");
DEFINE_string(wav_bit_depth, "16", "WAV bit depth, 16 (16-bit PCM) or 32 (32-bit IEEE float)");
DEFINE_string(wav_sample_rate, "24000", "WAV sample rate, should match the output of the vocoder");
DEFINE_string(cpu_thread, "1", "CPU thread numbers");

template <typename T>
std::vector<T> vectorT(const std::vector<int32_t> input) {
    std::vector<T> output(input.size());
    std::transform(input.begin(), input.end(), output.begin(), [](int x) {
        return static_cast<T>(x);
    });
    return output;
}

std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                   [](unsigned char c){ return std::tolower(c); });
    return result;
}

int main(int argc, char *argv[]) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    PredictorInterface *predictor;

    if (FLAGS_wav_bit_depth == "16") {
        predictor = new Predictor<int16_t>();
    } else if (FLAGS_wav_bit_depth == "32") {
        predictor = new Predictor<float>();
    } else {
        LOG(ERROR) << "Unsupported WAV bit depth: " << FLAGS_wav_bit_depth;
        return -1;
    }


    /////////////////////////// 前端：文本转音素 ///////////////////////////

    // 实例化文本前端引擎
    ppspeech::FrontEngineInterface *front_inst = nullptr;
    front_inst = new ppspeech::FrontEngineInterface(FLAGS_front_conf);
    if ((!front_inst) || (front_inst->init())) {
        LOG(ERROR) << "Creater tts engine failed!";
        if (front_inst != nullptr) {
            delete front_inst;
        }
        front_inst = nullptr;
        return -1;
    }

    // 英文大写转小写
    std::string sentence = toLower(FLAGS_sentence);

    // 转UTF-32
    std::wstring ws_sentence = ppspeech::utf8string2wstring(sentence);

    // 繁体转简体
    std::wstring sentence_simp;
    front_inst->Trand2Simp(ws_sentence, sentence_simp); 
    ws_sentence = sentence_simp;

    std::string s_sentence;
    std::vector<std::wstring> sentence_part;
    std::vector<int> phoneIds = {};
    std::vector<int> toneIds = {};

    // 根据标点进行分句
    LOG(INFO) << "Start to segment sentences by punctuation";
    front_inst->SplitByPunc(ws_sentence, sentence_part); 
    LOG(INFO) << "Segment sentences through punctuation successfully";

    // 分句后获取音素id
    LOG(INFO) << "Start to get the phoneme and tone id sequence of each sentence";
    for(int i = 0; i < sentence_part.size(); i++) {

        LOG(INFO) << "Raw sentence is: " << ppspeech::wstring2utf8string(sentence_part[i]);
        front_inst->SentenceNormalize(sentence_part[i]);
        s_sentence = ppspeech::wstring2utf8string(sentence_part[i]);
        LOG(INFO) << "After normalization sentence is: " << s_sentence;
        
        if (0 != front_inst->GetSentenceIds(s_sentence, phoneIds, toneIds)) {
            LOG(ERROR) << "TTS inst get sentence phoneIds and toneIds failed";
            return -1;
        }
            
    }
    LOG(INFO) << "The phoneIds of the sentence is: " << limonp::Join(phoneIds.begin(), phoneIds.end(), " ");
    LOG(INFO) << "The toneIds of the sentence is: " << limonp::Join(toneIds.begin(), toneIds.end(), " ");
    LOG(INFO) << "Get the phoneme id sequence of each sentence successfully";
 

    /////////////////////////// 后端：音素转音频 ///////////////////////////

    // WAV采样率（必须与模型输出匹配）
    // 如果播放速度和音调异常，请修改采样率
    // 常见采样率：16000, 24000, 32000, 44100, 48000, 96000
    const uint32_t wavSampleRate = std::stoul(FLAGS_wav_sample_rate);

    // CPU线程数
    const int cpuThreadNum = std::stol(FLAGS_cpu_thread);

    // CPU电源模式
    const PowerMode cpuPowerMode = PowerMode::LITE_POWER_HIGH;

    if (!predictor->Init(FLAGS_acoustic_model, FLAGS_vocoder, cpuPowerMode, cpuThreadNum, wavSampleRate)) {
        LOG(ERROR) << "predictor init failed" << std::endl;
        return -1;
    }

    // 模型可能要求使用不同类型的传入参数
    if (FLAGS_phone_id_data_type == "int32") {
        predictor->SetAcousticModelInput(0, phoneIds);
    } else if (FLAGS_phone_id_data_type == "int64") {
        predictor->SetAcousticModelInput(0, vectorT<int64_t>(phoneIds));
    } else if (FLAGS_phone_id_data_type == "float") {
        predictor->SetAcousticModelInput(0, vectorT<float>(phoneIds));
    } else {
        LOG(ERROR) << "Unsupported phone id data type: " << FLAGS_phone_id_data_type;
        return -1;
    }

    // 说话者id，如果为空则不设置（有的模型可能没有该参数）
    if (!FLAGS_speaker_id.empty()) {
        std::vector<int32_t> speakerIds = {std::stoi(FLAGS_speaker_id)};

        // 模型可能要求使用不同类型的传入参数
        if (FLAGS_speaker_id_data_type == "int32") {
            predictor->SetAcousticModelInput(1, speakerIds);
        } else if (FLAGS_speaker_id_data_type == "int64") {
            predictor->SetAcousticModelInput(1, vectorT<int64_t>(speakerIds));
        } else if (FLAGS_speaker_id_data_type == "float") {
            predictor->SetAcousticModelInput(1, vectorT<float>(speakerIds));
        } else {
            LOG(ERROR) << "Unsupported speaker id data type: " << FLAGS_speaker_id_data_type;
            return -1;
        }
    }

    if (!predictor->RunModel()) {
        LOG(ERROR) << "predictor run model failed" << std::endl;
        return -1;
    }

    LOG(INFO) << "Inference time: " << predictor->GetInferenceTime() << " ms, "
              << "WAV size (without header): " << predictor->GetWavSize() << " bytes, "
              << "WAV duration: " << predictor->GetWavDuration() << " ms, "
              << "RTF: " << predictor->GetRTF() << std::endl;

    if (!predictor->WriteWavToFile(FLAGS_output_wav)) {
        LOG(ERROR) << "write wav file failed" << std::endl;
        return -1;
    }

    delete predictor;

    return 0;
}

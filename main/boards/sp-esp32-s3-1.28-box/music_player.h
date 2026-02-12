#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include "audio_codec.h"
#include "simple_dec/esp_audio_simple_dec.h"
#include "esp_ae_rate_cvt.h"

class MusicPlayer {
public:
    enum class State { kStopped, kPlaying, kPaused };

    MusicPlayer(AudioCodec* codec);
    ~MusicPlayer();

    bool InitSdCard();
    bool Play(const std::string& filename = "");
    void Pause();
    void Resume();
    void Stop();
    void Next();
    void Previous();

    std::vector<std::string> ListTracks() const;
    std::string GetCurrentTrack() const;
    State GetState() const { return state_.load(); }
    int GetTrackCount() const;

private:
    static void PlayerTaskEntry(void* arg);
    void PlayerTask();

    bool ScanMusicFiles();
    bool OpenDecoder(const std::string& path);
    void CloseDecoder();
    bool DecodeAndPlayChunk();
    esp_audio_simple_dec_type_t GetDecoderType(const std::string& filepath) const;

    AudioCodec* codec_;
    std::atomic<State> state_{State::kStopped};
    bool sd_mounted_ = false;

    std::vector<std::string> track_list_;
    int current_index_ = -1;
    std::string pending_filename_;
    mutable std::mutex mutex_;

    // Decoder state
    esp_audio_simple_dec_handle_t decoder_ = nullptr;
    esp_ae_rate_cvt_handle_t resampler_ = nullptr;
    FILE* file_ = nullptr;
    int source_sample_rate_ = 0;
    int source_channels_ = 0;
    bool info_obtained_ = false;

    // FreeRTOS
    TaskHandle_t task_ = nullptr;
    EventGroupHandle_t events_ = nullptr;

    static constexpr int EVT_PLAY    = BIT0;
    static constexpr int EVT_STOP    = BIT1;
    static constexpr int EVT_PAUSE   = BIT2;
    static constexpr int EVT_RESUME  = BIT3;
    static constexpr int EVT_NEXT    = BIT4;
    static constexpr int EVT_PREV    = BIT5;
    static constexpr int EVT_EXIT    = BIT6;
    static constexpr int EVT_ALL     = 0x7F;

    static constexpr size_t READ_BUF_SIZE = 2048;
    static constexpr size_t PCM_BUF_SIZE  = 4096;
};

#endif // MUSIC_PLAYER_H

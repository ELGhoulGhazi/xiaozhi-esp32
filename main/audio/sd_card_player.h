#ifndef SD_CARD_PLAYER_H
#define SD_CARD_PLAYER_H

#include <string>
#include <vector>
#include <tuple>
#include <mutex>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

class AudioService;
class AudioCodec;

enum class PlayerCommand {
    kNone,
    kPlay,
    kStop,
    kNext,
    kPrevious,
    kVolumeUp,
    kVolumeDown
};

class SdCardPlayer {
public:
    SdCardPlayer();
    ~SdCardPlayer();

    void Initialize(AudioService* audio_service, AudioCodec* codec,
                    const std::string& music_dir = "/sdcard/music");

    // Handle a voice command action string (e.g. "play_music", "stop_music")
    void HandleCommand(const std::string& action);

    bool IsPlaying() const { return playing_.load(); }

    // Returns {command_pinyin, display_text, action} tuples for MultiNet registration
    // lang_code: "en", "cn", "fr"
    static std::vector<std::tuple<std::string, std::string, std::string>>
        GetCommandsForLanguage(const std::string& lang_code);

private:
    AudioService* audio_service_ = nullptr;
    AudioCodec* codec_ = nullptr;
    std::string music_dir_;

    // Playlist
    std::vector<std::string> playlist_;
    int current_track_ = 0;
    std::mutex playlist_mutex_;

    // Playback state
    TaskHandle_t playback_task_ = nullptr;
    std::atomic<bool> playing_{false};
    std::atomic<PlayerCommand> pending_command_{PlayerCommand::kNone};
    EventGroupHandle_t event_group_ = nullptr;
    static constexpr uint32_t EVT_COMMAND = (1 << 0);

    void ScanMusicFiles();
    static void PlaybackTaskEntry(void* arg);
    void PlaybackLoop();
    void PlayOggFile(const std::string& filepath);
    void PlayWavFile(const std::string& filepath);
    void PlayMp3File(const std::string& filepath);

    static bool IsOggFile(const std::string& filename);
    static bool IsWavFile(const std::string& filename);
    static bool IsMp3File(const std::string& filename);
};

#endif // SD_CARD_PLAYER_H

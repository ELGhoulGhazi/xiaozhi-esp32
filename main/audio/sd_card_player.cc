#include "sd_card_player.h"
#include "audio_service.h"
#include "audio_codec.h"
#include "demuxer/ogg_demuxer.h"
#include "protocol.h"
#include "simple_dec/esp_audio_simple_dec.h"

#include <esp_log.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>
#include <algorithm>

#define TAG "SdCardPlayer"

static constexpr size_t FILE_READ_BUF_SIZE = 4096;
static constexpr size_t PLAYBACK_TASK_STACK = 12288;

SdCardPlayer::SdCardPlayer() {
}

SdCardPlayer::~SdCardPlayer() {
    if (playing_.load()) {
        pending_command_.store(PlayerCommand::kStop);
        if (event_group_) {
            xEventGroupSetBits(event_group_, EVT_COMMAND);
        }
        // Wait for task to finish
        while (playback_task_ != nullptr) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (event_group_) {
        vEventGroupDelete(event_group_);
    }
}

void SdCardPlayer::Initialize(AudioService* audio_service, AudioCodec* codec,
                               const std::string& music_dir) {
    audio_service_ = audio_service;
    codec_ = codec;
    music_dir_ = music_dir;
    event_group_ = xEventGroupCreate();
}

void SdCardPlayer::HandleCommand(const std::string& action) {
    ESP_LOGI(TAG, "HandleCommand: %s", action.c_str());

    if (action == "play_music") {
        if (playing_.load()) {
            ESP_LOGI(TAG, "Already playing");
            return;
        }
        ScanMusicFiles();
        if (playlist_.empty()) {
            ESP_LOGW(TAG, "No music files found in %s", music_dir_.c_str());
            return;
        }
        pending_command_.store(PlayerCommand::kPlay);
        xEventGroupSetBits(event_group_, EVT_COMMAND);

        if (playback_task_ == nullptr) {
            xTaskCreatePinnedToCore(PlaybackTaskEntry, "sd_player", PLAYBACK_TASK_STACK,
                                    this, 4, &playback_task_, 0);
        }
    } else if (action == "stop_music") {
        pending_command_.store(PlayerCommand::kStop);
        xEventGroupSetBits(event_group_, EVT_COMMAND);
    } else if (action == "next_track") {
        pending_command_.store(PlayerCommand::kNext);
        xEventGroupSetBits(event_group_, EVT_COMMAND);
    } else if (action == "previous_track") {
        pending_command_.store(PlayerCommand::kPrevious);
        xEventGroupSetBits(event_group_, EVT_COMMAND);
    } else if (action == "volume_up") {
        int vol = codec_->output_volume();
        int new_vol = std::min(100, vol + 10);
        ESP_LOGI(TAG, "Volume up: %d -> %d", vol, new_vol);
        codec_->SetOutputVolume(new_vol);
    } else if (action == "volume_down") {
        int vol = codec_->output_volume();
        int new_vol = std::max(0, vol - 10);
        ESP_LOGI(TAG, "Volume down: %d -> %d", vol, new_vol);
        codec_->SetOutputVolume(new_vol);
    } else {
        ESP_LOGW(TAG, "Unknown action: %s", action.c_str());
    }
}

void SdCardPlayer::ScanMusicFiles() {
    std::lock_guard<std::mutex> lock(playlist_mutex_);
    playlist_.clear();
    current_track_ = 0;

    DIR* dir = opendir(music_dir_.c_str());
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open music directory: %s", music_dir_.c_str());
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name(entry->d_name);
        if (IsOggFile(name) || IsWavFile(name) || IsMp3File(name)) {
            playlist_.push_back(music_dir_ + "/" + name);
        }
    }
    closedir(dir);

    std::sort(playlist_.begin(), playlist_.end());
    ESP_LOGI(TAG, "Found %d music files in %s", (int)playlist_.size(), music_dir_.c_str());
    for (auto& f : playlist_) {
        ESP_LOGI(TAG, "  %s", f.c_str());
    }
}

void SdCardPlayer::PlaybackTaskEntry(void* arg) {
    auto* self = static_cast<SdCardPlayer*>(arg);
    self->PlaybackLoop();
    self->playback_task_ = nullptr;
    vTaskDelete(nullptr);
}

void SdCardPlayer::PlaybackLoop() {
    playing_.store(true);

    while (true) {
        // Get current track
        std::string filepath;
        {
            std::lock_guard<std::mutex> lock(playlist_mutex_);
            if (playlist_.empty()) {
                ESP_LOGW(TAG, "Playlist is empty");
                break;
            }
            if (current_track_ < 0) current_track_ = playlist_.size() - 1;
            if (current_track_ >= (int)playlist_.size()) current_track_ = 0;
            filepath = playlist_[current_track_];
        }

        ESP_LOGI(TAG, "Playing: %s", filepath.c_str());

        // Clear any stale command before playing
        auto cmd = pending_command_.exchange(PlayerCommand::kNone);
        if (cmd == PlayerCommand::kStop) {
            break;
        }

        // Play the file
        if (IsOggFile(filepath)) {
            PlayOggFile(filepath);
        } else if (IsWavFile(filepath)) {
            PlayWavFile(filepath);
        } else if (IsMp3File(filepath)) {
            PlayMp3File(filepath);
        }

        // Check what command caused us to stop/finish
        cmd = pending_command_.exchange(PlayerCommand::kNone);
        if (cmd == PlayerCommand::kStop) {
            break;
        } else if (cmd == PlayerCommand::kNext) {
            std::lock_guard<std::mutex> lock(playlist_mutex_);
            current_track_++;
            continue;
        } else if (cmd == PlayerCommand::kPrevious) {
            std::lock_guard<std::mutex> lock(playlist_mutex_);
            current_track_--;
            continue;
        } else {
            // Track finished naturally, advance to next
            std::lock_guard<std::mutex> lock(playlist_mutex_);
            current_track_++;
            if (current_track_ >= (int)playlist_.size()) {
                // End of playlist
                ESP_LOGI(TAG, "Playlist finished");
                break;
            }
        }
    }

    playing_.store(false);
    ESP_LOGI(TAG, "Playback stopped");
}

void SdCardPlayer::PlayOggFile(const std::string& filepath) {
    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open file: %s", filepath.c_str());
        return;
    }

    // Ensure output is enabled
    if (!codec_->output_enabled()) {
        codec_->EnableOutput(true);
    }

    OggDemuxer demuxer;
    demuxer.OnDemuxerFinished([this](const uint8_t* data, int sample_rate, size_t len) {
        auto packet = std::make_unique<AudioStreamPacket>();
        packet->sample_rate = sample_rate;
        packet->frame_duration = 60;
        packet->payload.resize(len);
        std::memcpy(packet->payload.data(), data, len);
        audio_service_->PushPacketToDecodeQueue(std::move(packet), true);
    });

    uint8_t buf[FILE_READ_BUF_SIZE];
    while (true) {
        // Check for commands
        PlayerCommand cmd = pending_command_.load();
        if (cmd == PlayerCommand::kStop || cmd == PlayerCommand::kNext || cmd == PlayerCommand::kPrevious) {
            audio_service_->ResetDecoder();
            break;
        }

        size_t bytes_read = fread(buf, 1, sizeof(buf), f);
        if (bytes_read == 0) {
            // EOF - wait for playback queue to drain
            audio_service_->WaitForPlaybackQueueEmpty();
            break;
        }

        demuxer.Process(buf, bytes_read);
    }

    fclose(f);
}

void SdCardPlayer::PlayWavFile(const std::string& filepath) {
    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open file: %s", filepath.c_str());
        return;
    }

    // Parse WAV header
    uint8_t header[44];
    if (fread(header, 1, 44, f) != 44) {
        ESP_LOGE(TAG, "Failed to read WAV header");
        fclose(f);
        return;
    }

    // Verify RIFF/WAVE
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Not a valid WAV file");
        fclose(f);
        return;
    }

    uint16_t audio_format = header[20] | (header[21] << 8);
    uint16_t num_channels = header[22] | (header[23] << 8);
    uint32_t sample_rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    uint16_t bits_per_sample = header[34] | (header[35] << 8);

    ESP_LOGI(TAG, "WAV: format=%d, channels=%d, sample_rate=%lu, bits=%d",
             audio_format, num_channels, sample_rate, bits_per_sample);

    if (audio_format != 1) {  // PCM only
        ESP_LOGE(TAG, "Unsupported WAV format (only PCM supported): %d", audio_format);
        fclose(f);
        return;
    }

    if (bits_per_sample != 16) {
        ESP_LOGE(TAG, "Unsupported bits per sample (only 16-bit supported): %d", bits_per_sample);
        fclose(f);
        return;
    }

    // Ensure output is enabled
    if (!codec_->output_enabled()) {
        codec_->EnableOutput(true);
    }

    // Read and play PCM data in chunks
    // Output chunk size: match codec output sample rate timing (~20ms of audio)
    int output_sr = codec_->output_sample_rate();
    int chunk_samples = output_sr / 50;  // 20ms worth of samples
    size_t bytes_per_sample = (bits_per_sample / 8) * num_channels;
    size_t chunk_bytes = chunk_samples * bytes_per_sample;

    std::vector<uint8_t> read_buf(chunk_bytes);
    std::vector<int16_t> pcm_out;

    while (true) {
        // Check for commands
        PlayerCommand cmd = pending_command_.load();
        if (cmd == PlayerCommand::kStop || cmd == PlayerCommand::kNext || cmd == PlayerCommand::kPrevious) {
            break;
        }

        size_t bytes_read = fread(read_buf.data(), 1, chunk_bytes, f);
        if (bytes_read == 0) {
            break;  // EOF
        }

        size_t samples_read = bytes_read / (bits_per_sample / 8);

        if (num_channels == 2) {
            // Convert stereo to mono
            size_t mono_samples = samples_read / 2;
            pcm_out.resize(mono_samples);
            auto* src = reinterpret_cast<int16_t*>(read_buf.data());
            for (size_t i = 0; i < mono_samples; i++) {
                pcm_out[i] = (src[i * 2] / 2) + (src[i * 2 + 1] / 2);
            }
        } else {
            pcm_out.resize(samples_read);
            memcpy(pcm_out.data(), read_buf.data(), bytes_read);
        }

        codec_->OutputData(pcm_out);
    }

    fclose(f);
}

bool SdCardPlayer::IsOggFile(const std::string& filename) {
    if (filename.size() < 4) return false;
    std::string ext4 = filename.substr(filename.size() - 4);
    for (auto& c : ext4) c = tolower(c);
    if (ext4 == ".ogg" || ext4 == ".oga") return true;
    if (filename.size() >= 5) {
        std::string ext5 = filename.substr(filename.size() - 5);
        for (auto& c : ext5) c = tolower(c);
        if (ext5 == ".opus") return true;
    }
    return false;
}

bool SdCardPlayer::IsWavFile(const std::string& filename) {
    if (filename.size() < 4) return false;
    std::string ext = filename.substr(filename.size() - 4);
    for (auto& c : ext) c = tolower(c);
    return ext == ".wav";
}

bool SdCardPlayer::IsMp3File(const std::string& filename) {
    if (filename.size() < 4) return false;
    std::string ext = filename.substr(filename.size() - 4);
    for (auto& c : ext) c = tolower(c);
    return ext == ".mp3";
}

void SdCardPlayer::PlayMp3File(const std::string& filepath) {
    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open file: %s", filepath.c_str());
        return;
    }

    // Ensure output is enabled
    if (!codec_->output_enabled()) {
        codec_->EnableOutput(true);
    }

    // Open the simple decoder for MP3
    esp_audio_simple_dec_cfg_t dec_cfg = {};
    dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    dec_cfg.dec_cfg = nullptr;
    dec_cfg.cfg_size = 0;

    esp_audio_simple_dec_handle_t dec_handle = nullptr;
    auto ret = esp_audio_simple_dec_open(&dec_cfg, &dec_handle);
    if (ret != ESP_AUDIO_ERR_OK || !dec_handle) {
        ESP_LOGE(TAG, "Failed to open MP3 decoder: %d", ret);
        fclose(f);
        return;
    }

    // Buffers
    uint8_t read_buf[FILE_READ_BUF_SIZE];
    // PCM output buffer: enough for one MP3 frame (1152 samples * 2 channels * 2 bytes)
    size_t pcm_buf_size = 1152 * 2 * sizeof(int16_t);
    std::vector<uint8_t> pcm_buf(pcm_buf_size);
    std::vector<int16_t> pcm_out;

    size_t leftover = 0;  // unprocessed bytes carried over
    bool got_info = false;
    int num_channels = 2;
    uint32_t sample_rate = 44100;

    while (true) {
        // Check for commands
        PlayerCommand cmd = pending_command_.load();
        if (cmd == PlayerCommand::kStop || cmd == PlayerCommand::kNext || cmd == PlayerCommand::kPrevious) {
            break;
        }

        // Read more data from file
        size_t bytes_read = fread(read_buf + leftover, 1, sizeof(read_buf) - leftover, f);
        size_t total = leftover + bytes_read;
        if (total == 0) {
            break;  // EOF and no leftover
        }

        bool eos = (bytes_read == 0);

        // Decode
        esp_audio_simple_dec_raw_t raw = {};
        raw.buffer = read_buf;
        raw.len = total;
        raw.eos = eos;
        raw.consumed = 0;

        while (raw.consumed < raw.len) {
            // Check for commands inside decode loop
            cmd = pending_command_.load();
            if (cmd == PlayerCommand::kStop || cmd == PlayerCommand::kNext || cmd == PlayerCommand::kPrevious) {
                goto done;
            }

            esp_audio_simple_dec_out_t out = {};
            out.buffer = pcm_buf.data();
            out.len = pcm_buf.size();
            out.decoded_size = 0;

            // Adjust input to remaining unconsumed data
            esp_audio_simple_dec_raw_t chunk = {};
            chunk.buffer = raw.buffer + raw.consumed;
            chunk.len = raw.len - raw.consumed;
            chunk.eos = eos;
            chunk.consumed = 0;

            ret = esp_audio_simple_dec_process(dec_handle, &chunk, &out);
            raw.consumed += chunk.consumed;

            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH && out.needed_size > 0) {
                pcm_buf.resize(out.needed_size);
                pcm_buf_size = out.needed_size;
                continue;  // retry with bigger buffer
            }

            if (out.decoded_size > 0) {
                // Get info on first successful decode
                if (!got_info) {
                    esp_audio_simple_dec_info_t info = {};
                    esp_audio_simple_dec_get_info(dec_handle, &info);
                    num_channels = info.channel;
                    sample_rate = info.sample_rate;
                    ESP_LOGI(TAG, "MP3: channels=%d, sample_rate=%lu, bitrate=%lu",
                             num_channels, sample_rate, info.bitrate);
                    got_info = true;
                }

                size_t samples = out.decoded_size / sizeof(int16_t);
                auto* src = reinterpret_cast<int16_t*>(pcm_buf.data());

                if (num_channels == 2) {
                    // Convert stereo to mono
                    size_t mono_samples = samples / 2;
                    pcm_out.resize(mono_samples);
                    for (size_t i = 0; i < mono_samples; i++) {
                        pcm_out[i] = (src[i * 2] / 2) + (src[i * 2 + 1] / 2);
                    }
                } else {
                    pcm_out.resize(samples);
                    memcpy(pcm_out.data(), src, out.decoded_size);
                }

                codec_->OutputData(pcm_out);
            }

            if (ret == ESP_AUDIO_ERR_DATA_LACK) {
                break;  // need more input data
            }
            if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                break;  // other error, try next chunk from file
            }
        }

        // Move unconsumed data to front of buffer
        leftover = total - raw.consumed;
        if (leftover > 0 && raw.consumed > 0) {
            memmove(read_buf, read_buf + raw.consumed, leftover);
        }

        if (eos) break;
    }

done:
    esp_audio_simple_dec_close(dec_handle);
    fclose(f);
}

std::vector<std::tuple<std::string, std::string, std::string>>
SdCardPlayer::GetCommandsForLanguage(const std::string& lang_code) {
    // Each tuple: {command_pinyin_or_text, display_text, action}
    // The first element is what MultiNet matches against (pinyin for Chinese, text for English/French)

    if (lang_code == "cn") {
        return {
            {"bo fang yin yue",   "播放音乐",   "play_music"},
            {"ting zhi bo fang",  "停止播放",   "stop_music"},
            {"xia yi shou",      "下一首",     "next_track"},
            {"shang yi shou",    "上一首",     "previous_track"},
            {"da sheng yi dian", "大声一点",   "volume_up"},
            {"xiao sheng yi dian", "小声一点", "volume_down"},
        };
    } else if (lang_code == "fr") {
        return {
            {"jouer musique",      "Jouer musique",      "play_music"},
            {"arreter musique",    "Arrêter musique",    "stop_music"},
            {"chanson suivante",   "Chanson suivante",   "next_track"},
            {"chanson precedente", "Chanson précédente", "previous_track"},
            {"plus fort",          "Plus fort",          "volume_up"},
            {"moins fort",         "Moins fort",         "volume_down"},
        };
    }

    // Default: English
    return {
        {"play music",    "Play music",    "play_music"},
        {"stop playing",  "Stop playing",  "stop_music"},
        {"next song",     "Next song",     "next_track"},
        {"previous song", "Previous song", "previous_track"},
        {"volume up",     "Volume up",     "volume_up"},
        {"volume down",   "Volume down",   "volume_down"},
    };
}

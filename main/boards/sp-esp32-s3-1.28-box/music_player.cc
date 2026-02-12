#include "music_player.h"
#include "config.h"
#include "application.h"

#include <algorithm>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <driver/sdspi_host.h>
#include "simple_dec/esp_audio_simple_dec_default.h"

#define TAG "MusicPlayer"

static std::string ToLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

static std::string GetExtension(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    return ToLower(path.substr(pos));
}

static std::string GetFilename(const std::string& path) {
    auto pos = path.rfind('/');
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

MusicPlayer::MusicPlayer(AudioCodec* codec) : codec_(codec) {
    events_ = xEventGroupCreate();
}

MusicPlayer::~MusicPlayer() {
    if (task_) {
        xEventGroupSetBits(events_, EVT_EXIT);
        // Wait for task to finish
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    CloseDecoder();
    if (events_) {
        vEventGroupDelete(events_);
    }
}

bool MusicPlayer::InitSdCard() {
    ESP_LOGI(TAG, "Initializing SD card (SDSPI)");

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = SDCARD_SPI_MOSI;
    bus_cfg.miso_io_num = SDCARD_SPI_MISO;
    bus_cfg.sclk_io_num = SDCARD_SPI_SCLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;

    esp_err_t ret = spi_bus_initialize((spi_host_device_t)SDCARD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SDCARD_SPI_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SDCARD_SPI_CS;
    slot_config.host_id = (spi_host_device_t)SDCARD_SPI_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 0,
        .disk_status_check_enable = true,
    };

    sdmmc_card_t* card;
    ret = esp_vfs_fat_sdspi_mount(SDCARD_MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        spi_bus_free((spi_host_device_t)SDCARD_SPI_HOST);
        return false;
    }

    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD card mounted at %s", SDCARD_MOUNT_POINT);
    sd_mounted_ = true;

    // Register default audio decoders (MP3, WAV, etc.)
    esp_audio_simple_dec_register_default();

    ScanMusicFiles();

    // Start the player task
    xTaskCreate(PlayerTaskEntry, "music_player", 6144, this, 3, &task_);

    return true;
}

bool MusicPlayer::ScanMusicFiles() {
    std::lock_guard<std::mutex> lock(mutex_);
    track_list_.clear();

    // Try /sdcard/music/ first, then /sdcard/
    const char* dirs[] = { SDCARD_MOUNT_POINT "/music", SDCARD_MOUNT_POINT };

    for (const char* dir_path : dirs) {
        DIR* dir = opendir(dir_path);
        if (!dir) continue;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type != DT_REG) continue;

            std::string ext = GetExtension(entry->d_name);
            if (ext == ".mp3" || ext == ".wav" || ext == ".ogg") {
                std::string full_path = std::string(dir_path) + "/" + entry->d_name;
                track_list_.push_back(full_path);
            }
        }
        closedir(dir);

        // If we found files in /music, don't scan root
        if (!track_list_.empty()) break;
    }

    std::sort(track_list_.begin(), track_list_.end());
    ESP_LOGI(TAG, "Found %d music files", (int)track_list_.size());
    for (const auto& t : track_list_) {
        ESP_LOGI(TAG, "  %s", GetFilename(t).c_str());
    }

    return !track_list_.empty();
}

bool MusicPlayer::Play(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (filename.empty()) {
        // Resume if paused, or start from current/first track
        if (state_.load() == State::kPaused) {
            xEventGroupSetBits(events_, EVT_RESUME);
            return true;
        }
        pending_filename_.clear();
    } else {
        pending_filename_ = filename;
    }
    xEventGroupSetBits(events_, EVT_PLAY);
    return true;
}

void MusicPlayer::Pause() {
    xEventGroupSetBits(events_, EVT_PAUSE);
}

void MusicPlayer::Resume() {
    xEventGroupSetBits(events_, EVT_RESUME);
}

void MusicPlayer::Stop() {
    xEventGroupSetBits(events_, EVT_STOP);
}

void MusicPlayer::Next() {
    xEventGroupSetBits(events_, EVT_NEXT);
}

void MusicPlayer::Previous() {
    xEventGroupSetBits(events_, EVT_PREV);
}

std::vector<std::string> MusicPlayer::ListTracks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& path : track_list_) {
        names.push_back(GetFilename(path));
    }
    return names;
}

std::string MusicPlayer::GetCurrentTrack() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_index_ >= 0 && current_index_ < (int)track_list_.size()) {
        return GetFilename(track_list_[current_index_]);
    }
    return "";
}

int MusicPlayer::GetTrackCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (int)track_list_.size();
}

esp_audio_simple_dec_type_t MusicPlayer::GetDecoderType(const std::string& filepath) const {
    std::string ext = GetExtension(filepath);
    if (ext == ".mp3") return ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    if (ext == ".wav") return ESP_AUDIO_SIMPLE_DEC_TYPE_WAV;
    if (ext == ".m4a") return ESP_AUDIO_SIMPLE_DEC_TYPE_M4A;
    if (ext == ".flac") return ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC;
    if (ext == ".aac") return ESP_AUDIO_SIMPLE_DEC_TYPE_AAC;
    return ESP_AUDIO_SIMPLE_DEC_TYPE_NONE;
}

bool MusicPlayer::OpenDecoder(const std::string& path) {
    CloseDecoder();

    auto dec_type = GetDecoderType(path);
    if (dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
        ESP_LOGE(TAG, "Unsupported format: %s", path.c_str());
        return false;
    }

    file_ = fopen(path.c_str(), "rb");
    if (!file_) {
        ESP_LOGE(TAG, "Failed to open file: %s", path.c_str());
        return false;
    }

    esp_audio_simple_dec_cfg_t cfg = {};
    cfg.dec_type = dec_type;
    cfg.dec_cfg = nullptr;
    cfg.cfg_size = 0;
    cfg.use_frame_dec = false;

    auto err = esp_audio_simple_dec_open(&cfg, &decoder_);
    if (err != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open decoder: %d", err);
        fclose(file_);
        file_ = nullptr;
        return false;
    }

    info_obtained_ = false;
    source_sample_rate_ = 0;
    source_channels_ = 0;

    ESP_LOGI(TAG, "Playing: %s", GetFilename(path).c_str());
    return true;
}

void MusicPlayer::CloseDecoder() {
    if (decoder_) {
        esp_audio_simple_dec_close(decoder_);
        decoder_ = nullptr;
    }
    if (resampler_) {
        esp_ae_rate_cvt_close(resampler_);
        resampler_ = nullptr;
    }
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
    info_obtained_ = false;
}

bool MusicPlayer::DecodeAndPlayChunk() {
    if (!file_ || !decoder_) return false;

    uint8_t read_buf[READ_BUF_SIZE];
    size_t bytes_read = fread(read_buf, 1, READ_BUF_SIZE, file_);
    if (bytes_read == 0) {
        return false;  // EOF
    }

    esp_audio_simple_dec_raw_t raw = {};
    raw.buffer = read_buf;
    raw.len = (uint32_t)bytes_read;
    raw.eos = feof(file_) != 0;

    uint8_t pcm_buf[PCM_BUF_SIZE];

    while (raw.len > 0) {
        esp_audio_simple_dec_out_t out = {};
        out.buffer = pcm_buf;
        out.len = PCM_BUF_SIZE;

        auto err = esp_audio_simple_dec_process(decoder_, &raw, &out);

        if (err == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            // Output buffer too small, skip this chunk
            ESP_LOGW(TAG, "PCM buffer too small, needed %lu", (unsigned long)out.needed_size);
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
            continue;
        }

        if (err != ESP_AUDIO_ERR_OK) {
            raw.buffer += raw.consumed;
            raw.len -= raw.consumed;
            continue;
        }

        raw.buffer += raw.consumed;
        raw.len -= raw.consumed;

        if (out.decoded_size == 0) continue;

        // Get audio info on first successful decode
        if (!info_obtained_) {
            esp_audio_simple_dec_info_t info = {};
            if (esp_audio_simple_dec_get_info(decoder_, &info) == ESP_AUDIO_ERR_OK) {
                source_sample_rate_ = info.sample_rate;
                source_channels_ = info.channel;
                info_obtained_ = true;
                ESP_LOGI(TAG, "Audio: %luHz, %d ch, %d bps",
                         (unsigned long)info.sample_rate, info.channel, info.bits_per_sample);

                // Create resampler if needed
                if ((int)info.sample_rate != codec_->output_sample_rate() && info.sample_rate > 0) {
                    esp_ae_rate_cvt_cfg_t rcfg = {};
                    rcfg.src_rate = info.sample_rate;
                    rcfg.dest_rate = codec_->output_sample_rate();
                    rcfg.channel = 1;  // We'll downmix to mono first
                    rcfg.bits_per_sample = 16;
                    rcfg.complexity = 2;
                    rcfg.perf_type = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
                    esp_ae_rate_cvt_open(&rcfg, &resampler_);
                }
            }
        }

        // Convert decoded PCM to int16_t samples
        int16_t* pcm_samples = (int16_t*)pcm_buf;
        int total_samples = out.decoded_size / sizeof(int16_t);

        // Downmix stereo to mono if needed
        std::vector<int16_t> mono_buf;
        if (source_channels_ >= 2) {
            int mono_count = total_samples / source_channels_;
            mono_buf.resize(mono_count);
            for (int i = 0; i < mono_count; i++) {
                int32_t sum = 0;
                for (int ch = 0; ch < source_channels_; ch++) {
                    sum += pcm_samples[i * source_channels_ + ch];
                }
                mono_buf[i] = (int16_t)(sum / source_channels_);
            }
            pcm_samples = mono_buf.data();
            total_samples = mono_count;
        }

        // Resample if needed
        std::vector<int16_t> output_samples;
        if (resampler_ && total_samples > 0) {
            uint32_t max_out = 0;
            esp_ae_rate_cvt_get_max_out_sample_num(resampler_, total_samples, &max_out);
            output_samples.resize(max_out);

            uint32_t out_num = max_out;
            esp_ae_rate_cvt_process(resampler_, (void*)pcm_samples, total_samples,
                                    (void*)output_samples.data(), &out_num);
            output_samples.resize(out_num);
        } else if (total_samples > 0) {
            output_samples.assign(pcm_samples, pcm_samples + total_samples);
        }

        // Output to codec
        if (!output_samples.empty()) {
            if (!codec_->output_enabled()) {
                codec_->EnableOutput(true);
            }
            codec_->OutputData(output_samples);
        }
    }

    return true;
}

void MusicPlayer::PlayerTaskEntry(void* arg) {
    auto* player = static_cast<MusicPlayer*>(arg);
    player->PlayerTask();
    player->task_ = nullptr;
    vTaskDelete(nullptr);
}

void MusicPlayer::PlayerTask() {
    ESP_LOGI(TAG, "Player task started");

    while (true) {
        // When stopped or paused, wait indefinitely for events
        // When playing, poll quickly (non-blocking) so we can keep decoding
        TickType_t wait_time = (state_.load() == State::kPlaying)
                             ? pdMS_TO_TICKS(0)
                             : portMAX_DELAY;

        EventBits_t bits = xEventGroupWaitBits(events_, EVT_ALL, pdTRUE, pdFALSE, wait_time);

        if (bits & EVT_EXIT) {
            CloseDecoder();
            state_.store(State::kStopped);
            break;
        }

        if (bits & EVT_STOP) {
            CloseDecoder();
            state_.store(State::kStopped);
            ESP_LOGI(TAG, "Stopped");
            continue;
        }

        if (bits & EVT_PAUSE) {
            if (state_.load() == State::kPlaying) {
                state_.store(State::kPaused);
                ESP_LOGI(TAG, "Paused");
            }
            continue;
        }

        if (bits & EVT_PLAY) {
            std::lock_guard<std::mutex> lock(mutex_);

            if (track_list_.empty()) {
                ESP_LOGW(TAG, "No tracks available");
                continue;
            }

            // Find track by filename if specified
            if (!pending_filename_.empty()) {
                bool found = false;
                for (int i = 0; i < (int)track_list_.size(); i++) {
                    if (GetFilename(track_list_[i]) == pending_filename_) {
                        current_index_ = i;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ESP_LOGW(TAG, "Track not found: %s", pending_filename_.c_str());
                    pending_filename_.clear();
                    continue;
                }
                pending_filename_.clear();
            } else if (current_index_ < 0) {
                current_index_ = 0;
            }

            if (OpenDecoder(track_list_[current_index_])) {
                state_.store(State::kPlaying);
            }
            continue;
        }

        if (bits & EVT_RESUME) {
            if (state_.load() == State::kPaused) {
                state_.store(State::kPlaying);
                ESP_LOGI(TAG, "Resumed");
            }
            continue;
        }

        if (bits & EVT_NEXT) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (track_list_.empty()) continue;
            current_index_ = (current_index_ + 1) % (int)track_list_.size();
            if (OpenDecoder(track_list_[current_index_])) {
                state_.store(State::kPlaying);
            }
            continue;
        }

        if (bits & EVT_PREV) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (track_list_.empty()) continue;
            current_index_ = (current_index_ - 1 + (int)track_list_.size()) % (int)track_list_.size();
            if (OpenDecoder(track_list_[current_index_])) {
                state_.store(State::kPlaying);
            }
            continue;
        }

        // Playback loop
        if (state_.load() == State::kPlaying) {
            // Auto-pause during voice interaction
            auto dev_state = Application::GetInstance().GetDeviceState();
            if (dev_state == kDeviceStateListening || dev_state == kDeviceStateSpeaking) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            if (!DecodeAndPlayChunk()) {
                // EOF - advance to next track
                ESP_LOGI(TAG, "Track finished, playing next");
                std::lock_guard<std::mutex> lock(mutex_);
                if (!track_list_.empty()) {
                    current_index_ = (current_index_ + 1) % (int)track_list_.size();
                    if (!OpenDecoder(track_list_[current_index_])) {
                        state_.store(State::kStopped);
                    }
                } else {
                    state_.store(State::kStopped);
                }
            }
        }
    }

    ESP_LOGI(TAG, "Player task exiting");
}

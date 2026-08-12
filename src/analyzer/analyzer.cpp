#include <algorithm>
        #include <array>
        #include <cmath>
        #include <cstdint>
        #include <filesystem>
        #include <fstream>
        #include <iostream>
        #include <string>
        #include <vector>

        #define DR_MP3_IMPLEMENTATION
        #include "dr_mp3.h"

        #include "fft.h"
        #include "features.h"
        #include "veffects/score.h"

        namespace {

        constexpr int kWindowSize = 2048;
        constexpr int kBandCount = 16;
        constexpr float kPi = 3.14159265358979323846f;

        float clamp01(float value) {
            return std::max(0.0f, std::min(1.0f, value));
        }

        std::vector<float> decode_to_mono(const std::string& path, uint32_t& sample_rate) {
            drmp3 decoder{};
            if (!drmp3_init_file(&decoder, path.c_str(), nullptr)) {
                throw std::runtime_error("failed to open input mp3");
            }

            sample_rate = decoder.sampleRate ? decoder.sampleRate : 44100u;
            const uint32_t channels = decoder.channels ? decoder.channels : 2u;
            std::vector<float> interleaved;
            std::array<float, 4096 * 2> chunk{};
            while (true) {
                const drmp3_uint64 frames = drmp3_read_pcm_frames_f32(&decoder, 4096, chunk.data());
                if (frames == 0) {
                    break;
                }
                interleaved.insert(interleaved.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(frames * channels));
            }
            drmp3_uninit(&decoder);

            std::vector<float> mono;
            mono.reserve(interleaved.size() / channels + 1);
            for (std::size_t i = 0; i < interleaved.size(); i += channels) {
                float sample = 0.0f;
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    sample += interleaved[i + ch];
                }
                mono.push_back(sample / static_cast<float>(channels));
            }
            return mono;
        }

        void compute_energy_bands(const std::vector<float>& mag, uint32_t sample_rate, VE_FrameScore& frame) {
            float bass = 0.0f;
            float mid = 0.0f;
            float high = 0.0f;
            float total = 0.0f;
            for (std::size_t i = 0; i < mag.size(); ++i) {
                const float freq = (static_cast<float>(i) * static_cast<float>(sample_rate)) / static_cast<float>(kWindowSize);
                total += mag[i];
                if (freq >= 20.0f && freq < 250.0f) {
                    bass += mag[i];
                } else if (freq >= 250.0f && freq < 2000.0f) {
                    mid += mag[i];
                } else if (freq >= 2000.0f && freq < 8000.0f) {
                    high += mag[i];
                }
            }
            if (total > 1e-6f) {
                frame.bass = clamp01(bass / total);
                frame.mid = clamp01(mid / total);
                frame.high = clamp01(high / total);
            } else {
                frame.bass = frame.mid = frame.high = 0.0f;
            }
        }

        std::vector<VE_FrameScore> analyze_samples(const std::vector<float>& samples, uint32_t sample_rate, uint32_t hop_samples) {
            std::vector<VE_FrameScore> frames;
            if (sample_rate == 0 || hop_samples == 0) {
                return frames;
            }

            const std::size_t total_frames = samples.empty() ? 0 : ((samples.size() + hop_samples - 1) / hop_samples);
            frames.reserve(total_frames);
            float prev_rms = 0.0f;
            std::vector<uint32_t> onset_indices;

            for (std::size_t frame_index = 0, offset = 0; offset < samples.size(); ++frame_index, offset += hop_samples) {
                std::vector<float> window(kWindowSize, 0.0f);
                for (int i = 0; i < kWindowSize; ++i) {
                    const std::size_t sample_index = offset + static_cast<std::size_t>(i);
                    if (sample_index >= samples.size()) {
                        break;
                    }
                    const float hann = 0.5f - 0.5f * std::cos((2.0f * kPi * i) / static_cast<float>(kWindowSize - 1));
                    window[i] = samples[sample_index] * hann;
                }

                std::vector<float> real(window.begin(), window.end());
                std::vector<float> imag(kWindowSize, 0.0f);
                ve_fft(real.data(), imag.data(), kWindowSize);

                std::vector<float> mag(kWindowSize / 2, 0.0f);
                for (int i = 0; i < kWindowSize / 2; ++i) {
                    mag[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
                }

                VE_FrameScore score{};
                score.frame_index = static_cast<uint32_t>(frame_index);
                score.rms = clamp01(ve_rms(window.data(), kWindowSize) * 2.0f);
                score.onset = ve_onset(prev_rms, score.rms, 0.05f);
                score.spectral_centroid = clamp01(ve_spectral_centroid(mag.data(), static_cast<int>(mag.size()), static_cast<float>(sample_rate)));
                ve_compute_bands(mag.data(), static_cast<int>(mag.size()), score.band, kBandCount);
                compute_energy_bands(mag, sample_rate, score);
                prev_rms = score.rms;

                if (score.onset > 0.5f) {
                    onset_indices.push_back(score.frame_index);
                }
                if (onset_indices.size() >= 2) {
                    float sum_intervals = 0.0f;
                    for (std::size_t i = 1; i < onset_indices.size(); ++i) {
                        sum_intervals += static_cast<float>(onset_indices[i] - onset_indices[i - 1]);
                    }
                    const float average_interval = sum_intervals / static_cast<float>(onset_indices.size() - 1);
                    score.tempo_hz = average_interval > 0.0f ? 60.0f / average_interval : 0.0f;
                } else {
                    score.tempo_hz = 0.0f;
                }
                frames.push_back(score);
            }
            return frames;
        }

        std::filesystem::path default_output_path(const std::filesystem::path& input) {
            auto output = input;
            output.replace_extension(".vsc");
            return output;
        }

        void write_score_file(const std::filesystem::path& output,
                              const std::vector<VE_FrameScore>& frames,
                              uint32_t sample_rate,
                              uint32_t hop_samples) {
            VE_ScoreHeader header{};
            header.magic = VE_SCORE_MAGIC;
            header.version = VE_SCORE_VERSION;
            header.frame_count = static_cast<uint32_t>(frames.size());
            header.sample_rate = sample_rate;
            header.hop_samples = hop_samples;

            std::ofstream stream(output, std::ios::binary);
            if (!stream) {
                throw std::runtime_error("failed to create output score file");
            }
            stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
            if (!frames.empty()) {
                stream.write(reinterpret_cast<const char*>(frames.data()), static_cast<std::streamsize>(frames.size() * sizeof(VE_FrameScore)));
            }
        }

        }  // namespace

        int main(int argc, char** argv) {
            if (argc < 2 || argc > 3) {
                std::cerr << "usage: veffects-analyze <input.mp3> [output.vsc]\n";
                return 1;
            }

            try {
                const std::filesystem::path input_path = argv[1];
                const std::filesystem::path output_path = argc == 3 ? std::filesystem::path(argv[2]) : default_output_path(input_path);
                uint32_t sample_rate = 44100u;
                const std::vector<float> samples = decode_to_mono(input_path.string(), sample_rate);
                const uint32_t hop_samples = std::max<uint32_t>(1u, sample_rate / 60u);
                const std::vector<VE_FrameScore> frames = analyze_samples(samples, sample_rate, hop_samples);
                write_score_file(output_path, frames, sample_rate, hop_samples);
                std::cout << "wrote " << frames.size() << " frames to " << output_path.string() << '\n';
                return 0;
            } catch (const std::exception& ex) {
                std::cerr << "veffects-analyze: " << ex.what() << '\n';
                return 1;
            }
        }

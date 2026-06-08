#pragma once
#include "ring.hpp"
#include <cstring>
#include <array>
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>

// ─── Scratch pool ─────────────────────────────────────────────────────────────
// Pré-alloué à l'init, O(1) acquire/release, aucune allocation dans le hot path

static constexpr int POOL_SIZE    = 16;
static constexpr int FRAMES_MAX   = 1024;

struct alignas(64) PoolBloc {
    float    data[FRAMES_MAX];
    std::atomic<bool> free{true};
};

class Pool {
public:
    PoolBloc blocs[POOL_SIZE];

    float* acquire() {
        for (auto& b : blocs) {
            bool expected = true;
            if (b.free.compare_exchange_strong(expected, false))
                return b.data;
        }
        return nullptr;   // overrun — pool épuisé
    }

    void release(float* ptr) {
        for (auto& b : blocs) {
            if (b.data == ptr) {
                b.free.store(true);
                return;
            }
        }
    }
};

// ─── AudioInput base ──────────────────────────────────────────────────────────
// onBlock() est appelé depuis le callback plateforme (ALSA ou JUCE)
// Hard-RT : pas d'allocation, pas de lock, pas de syscall sauf write(fd)

class AudioInput {
public:
    AudioInput(Ring& ring, Pool& pool, Head& capture_head)
        : ring_(ring), pool_(pool), head_(capture_head)
    {}

    virtual ~AudioInput() = default;

    virtual void start() = 0;
    virtual void stop()  = 0;

protected:
    // Appelé depuis le callback — copie dans le pool, pousse dans le ring
    void onBlock(const float* data, int frames) {
        float* buf = pool_.acquire();
        if (!buf) return;   // overrun — drop silencieux ou flag

        std::memcpy(buf, data, frames * sizeof(float));

        Slot s;
        s.data   = buf;
        s.frames = frames;
        head_.push(s);     // atomic write + write(fd) — signal kernel
    }

    Ring& ring_;
    Pool& pool_;
    Head& head_;
};

// ─── ALSA ─────────────────────────────────────────────────────────────────────
// Gère son propre thread de capture

class ALSAInput : public AudioInput {
public:
    ALSAInput(Ring& ring, Pool& pool, Head& capture_head,
              const char* device, int sample_rate, int frames_per_period)
        : AudioInput(ring, pool, capture_head)
        , device_(device)
        , sample_rate_(sample_rate)
        , frames_(frames_per_period)
        , running_(false)
    {}

    void start() override {
        // TODO : snd_pcm_open, snd_pcm_hw_params, snd_pcm_prepare
        running_ = true;
        thread_  = std::thread(&ALSAInput::capture_loop, this);
    }

    void stop() override {
        running_ = false;
        if (thread_.joinable()) thread_.join();
        // TODO : snd_pcm_close
    }

private:
    void capture_loop() {
        std::vector<float> tmp(frames_);
        while (running_) {
            // TODO : snd_pcm_readi(handle_, tmp.data(), frames_)
            // bloque kernel-side jusqu'à période disponible
            onBlock(tmp.data(), frames_);
        }
    }

    const char*       device_;
    int               sample_rate_;
    int               frames_;
    std::atomic<bool> running_;
    std::thread       thread_;
};

// ─── JUCE ─────────────────────────────────────────────────────────────────────
// Le thread audio est géré par JUCE — on expose getNextAudioBlock()

class JUCEInput : public AudioInput
               /* , public juce::AudioSource */ {
public:
    JUCEInput(Ring& ring, Pool& pool, Head& capture_head)
        : AudioInput(ring, pool, capture_head)
    {}

    void start() override {
        // TODO : AudioDeviceManager::addAudioCallback(this)
    }

    void stop() override {
        // TODO : AudioDeviceManager::removeAudioCallback(this)
    }

    // Appelé par le thread audio JUCE — hard-RT
    // void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
    //     const float* data = info.buffer->getReadPointer(0, info.startSample);
    //     onBlock(data, info.numSamples);
    // }
};

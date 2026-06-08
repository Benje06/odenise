#pragma once
#include "ring.hpp"
#include "audio_input.hpp"
#include "modules.hpp"
#include <memory>

// ─── Pipeline ─────────────────────────────────────────────────────────────────
// Câblage statique à l'init — les Head* sont fixés une fois pour toutes
//
// capture.out ──┬──→ VAD.in    VAD.out ──→ NR.in_vad
//               └──→ FFT.in    FFT.out ──→ NR.in_fft
//                                          NR.out ──→ Filtre.in
//                                                     Filtre.out ──→ AGC.in

class Pipeline {
public:
    explicit Pipeline(Ring& ring, Pool& pool)
        : ring_(ring)
        , pool_(pool)
        , capture_head_(ring)          // write_head de la capture
        , vad_ (ring, capture_head_)
        , fft_ (ring, capture_head_, /*window=*/1024, /*hop=*/512)
        , nr_  (ring, vad_.output(), fft_.output())
        , filtre_(ring, nr_.output())
        , agc_   (ring, filtre_.output())
    {}

    // Démarre tous les modules dans l'ordre aval → amont
    // (les consommateurs bloquent en attendant leurs producteurs)
    void start() {
        agc_.start();
        filtre_.start();
        nr_.start();
        fft_.start();
        vad_.start();
    }

    void stop() {
        vad_.stop();
        fft_.stop();
        nr_.stop();
        filtre_.stop();
        agc_.stop();
    }

    Head& capture_head() { return capture_head_; }

private:
    Ring& ring_;
    Pool& pool_;

    Head    capture_head_;   // écrit par AudioInput, lu par VAD + FFT

    VAD     vad_;
    FFT     fft_;
    NR      nr_;
    Filtre  filtre_;
    AGC     agc_;
};

// ─── Exemple d'instanciation ──────────────────────────────────────────────────

/*
int main() {
    Ring     ring;
    Pool     pool;
    Pipeline pipeline(ring, pool);

    // ALSA
    ALSAInput input(ring, pool, pipeline.capture_head(),
                    "default", 48000, 256);

    // ou JUCE
    // JUCEInput input(ring, pool, pipeline.capture_head());

    pipeline.start();
    input.start();

    // ... tourne jusqu'à signal d'arrêt ...

    input.stop();
    pipeline.stop();
}
*/

#pragma once
#include "ring.hpp"
#include <thread>
#include <atomic>
#include <vector>

// ─── Module base ─────────────────────────────────────────────────────────────
// Chaque module :
//   - possède un write_head (sa sortie dans le ring)
//   - lit depuis un ou plusieurs Head* (ses entrées)
//   - tourne dans son propre thread
//   - se bloque kernel-side sur ses entrées via fd

class Module {
public:
    explicit Module(Ring& ring)
        : ring_(ring)
        , out_(ring)
        , running_(false)
    {}

    virtual ~Module() { stop(); }

    // Démarre le thread du module
    void start() {
        running_ = true;
        thread_ = std::thread(&Module::loop, this);
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    // Retourne le write_head de ce module — branché en entrée du suivant
    Head& output() { return out_; }

protected:
    // Boucle principale — bloque sur les entrées, traite, pousse en sortie
    virtual void loop() = 0;

    // Traitement effectif — à implémenter dans les sous-classes
    virtual Slot process(const Slot& in) = 0;

    Ring&         ring_;
    Head          out_;
    std::atomic<bool> running_;
    std::thread   thread_;
};

// ─── VAD ─────────────────────────────────────────────────────────────────────
// Entrée  : write_head de la capture
// Sortie  : write_head_VAD → lu par NR (read_head_NR_1)

class VAD : public Module {
public:
    VAD(Ring& ring, Head& capture_out)
        : Module(ring)
        , in_(capture_out)
    {}

protected:
    void loop() override {
        while (running_) {
            const Slot& s = in_.wait_next();   // bloque kernel-side
            Slot result   = process(s);
            out_.push(result);
        }
    }

    Slot process(const Slot& in) override {
        // TODO : calcul VAD, p_voice → écrit dans result.data
        return in;   // placeholder — même pointeur, traitement à ajouter
    }

private:
    Head& in_;
};

// ─── FFT ─────────────────────────────────────────────────────────────────────
// Entrée  : write_head de la capture
// Sortie  : write_head_FFT → lu par NR (read_head_NR_2)
// Accumule en interne pour la fenêtre glissante

class FFT : public Module {
public:
    FFT(Ring& ring, Head& capture_out, int window_size, int hop)
        : Module(ring)
        , in_(capture_out)
        , window_size_(window_size)
        , hop_(hop)
        , internal_buf_(window_size, 0.f)
        , write_pos_(0)
    {}

protected:
    void loop() override {
        while (running_) {
            const Slot& s = in_.wait_next();
            // Accumule dans le buffer interne
            for (int i = 0; i < s.frames && write_pos_ < window_size_; i++)
                internal_buf_[write_pos_++] = s.data[i];

            if (write_pos_ >= hop_) {
                Slot result = process(s);
                out_.push(result);
                // Glissement : décale de hop
                std::copy(internal_buf_.begin() + hop_,
                          internal_buf_.end(),
                          internal_buf_.begin());
                write_pos_ -= hop_;
            }
        }
    }

    Slot process(const Slot& in) override {
        // TODO : FFT sur internal_buf_, résultat dans out_.data
        return in;   // placeholder
    }

private:
    Head&             in_;
    int               window_size_;
    int               hop_;
    std::vector<float> internal_buf_;
    int               write_pos_;
};

// ─── NR ──────────────────────────────────────────────────────────────────────
// Entrées : write_head_VAD (read_head_NR_1)
//           write_head_FFT (read_head_NR_2)
// Sortie  : write_head_NR → lu par Filtre

class NR : public Module {
public:
    NR(Ring& ring, Head& vad_out, Head& fft_out)
        : Module(ring)
        , in_vad_(vad_out)
        , in_fft_(fft_out)
    {}

protected:
    void loop() override {
        while (running_) {
            // Bloque sur les deux entrées — attend VAD ET FFT
            const Slot& vad_slot = in_vad_.wait_next();
            const Slot& fft_slot = in_fft_.wait_next();
            Slot result = process_nr(vad_slot, fft_slot);
            out_.push(result);
        }
    }

    // NR a deux entrées — process() simple non utilisé ici
    Slot process(const Slot& in) override { return in; }

    virtual Slot process_nr(const Slot& vad, const Slot& fft) {
        // TODO : Wiener / RNNoise avec p_voice (vad) et spectre (fft)
        return vad;   // placeholder
    }

private:
    Head& in_vad_;
    Head& in_fft_;
};

// ─── Filtre ──────────────────────────────────────────────────────────────────
// Entrée  : write_head_NR
// Sortie  : write_head_Filtre → lu par AGC

class Filtre : public Module {
public:
    Filtre(Ring& ring, Head& nr_out)
        : Module(ring)
        , in_(nr_out)
    {}

protected:
    void loop() override {
        while (running_) {
            const Slot& s = in_.wait_next();
            Slot result   = process(s);
            out_.push(result);
        }
    }

    Slot process(const Slot& in) override {
        // TODO : filtre passe-bande IIR
        return in;
    }

private:
    Head& in_;
};

// ─── AGC ─────────────────────────────────────────────────────────────────────
// Entrée  : write_head_Filtre
// Sortie  : write_head_AGC → sortie finale

class AGC : public Module {
public:
    AGC(Ring& ring, Head& filtre_out)
        : Module(ring)
        , in_(filtre_out)
    {}

protected:
    void loop() override {
        while (running_) {
            const Slot& s = in_.wait_next();
            Slot result   = process(s);
            out_.push(result);
            // Dernier de la chaîne — ici on pourrait pool_release(result.data)
        }
    }

    Slot process(const Slot& in) override {
        // TODO : AGC, limiter, RMS historique interne
        return in;
    }

private:
    Head& in_;
};

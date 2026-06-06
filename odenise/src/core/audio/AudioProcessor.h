// ============================================================================
//  src/core/audio/AudioProcessor.h  --  Couche audio odenise.
//
//  Independante de JUCE, gtkmm et de tout framework audio.
//  Contient la logique de preparation, de cablage et de liberation
//  de la chaine audio. Le wrapper JUCE (JuceAudioProcessor) delegue ici.
//
//  Cycle de vie :
//    [CTRL] prepare(sample_rate, block_size)
//              -> reconfigure engine + backend
//              -> cree TrackIO depuis les buffers de l'interface selectionnee
//    [CTRL] setAudioIO(io)
//              -> cable les pointeurs audio sur le backend (hors RT)
//    [RT]   la couche audio appelle backend->process() directement
//              -> engine n'est plus dans la boucle RT
//    [CTRL] release()
//              -> suspend le backend
//
//  Relations :
//    AudioProcessor <-> Engine    (init, reconfigure, monitoring)
//    AudioProcessor <-> BackendBase (setAudioIO, prepare/release)
//    AudioProcessor n'est PAS dans la boucle RT
// ============================================================================
#pragma once

#include "engine.h"

#include <memory>
#include <string>
#include <vector>

namespace odenise::audio {

// ---------------------------------------------------------------------------
//  AudioProcessor -- logique de preparation et de cablage de la chaine audio.
//
//  Possede l'engine. Obtient un pointeur non-owning sur le backend actif
//  via engine apres chaque reconfigure.
// ---------------------------------------------------------------------------
class AudioProcessor {
public:
    // -----------------------------------------------------------------------
    //  Construction / destruction
    // -----------------------------------------------------------------------

    // Cree l'engine avec les caps par defaut.
    // Le backend et les modules sont charges au premier prepare().
    AudioProcessor();

    // Cree l'engine avec des caps explicites.
    explicit AudioProcessor(const EngineCaps& caps, const RuntimeConfig& cfg);

    ~AudioProcessor() = default;

    AudioProcessor(const AudioProcessor&)            = delete;
    AudioProcessor& operator=(const AudioProcessor&) = delete;

    // -----------------------------------------------------------------------
    //  Cycle de vie audio
    // -----------------------------------------------------------------------

    // [CTRL] Prepare la chaine pour un sample_rate et une taille de bloc.
    // Reconfigure l'engine et le backend. A appeler depuis prepareToPlay()
    // (JUCE) ou l'equivalent ALSA/CLAP avant le premier process().
    // Retourne false si le backend n'est pas disponible.
    bool prepare(int sample_rate, int block_size);

    // [CTRL] Cable les pointeurs audio de l'interface selectionnee sur le
    // backend. A appeler apres prepare(), quand les buffers sont connus.
    // Retourne false si le backend n'est pas disponible.
    bool setAudioIO(TrackIO io);

    // [CTRL] Suspend le backend. A appeler depuis releaseResources() (JUCE)
    // ou l'equivalent avant destruction ou changement d'interface.
    void release();

    // -----------------------------------------------------------------------
    //  Acces a l'engine (pour AudioEditor et le wrapper JUCE)
    // -----------------------------------------------------------------------
    Engine* engine() const noexcept { return engine_.get(); }

    // -----------------------------------------------------------------------
    //  Acces direct au backend (pour la boucle RT du wrapper JUCE).
    // -----------------------------------------------------------------------
    BackendBase* backend() const noexcept { return backend_; }

private:
    std::unique_ptr<Engine> engine_;
    BackendBase*            backend_ = nullptr;  // non-owning, possede par le registry
    int                     sample_rate_ = 0;
    int                     block_size_  = 0;
};

} // namespace odenise::audio

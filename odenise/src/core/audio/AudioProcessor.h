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
//    [CTRL] setAudioIO(io)
//              -> cable les pointeurs audio sur le backend (hors RT)
//    [RT]   la couche audio appelle backend->process() directement
//              -> engine n'est plus dans la boucle RT
//    [CTRL] release()
//              -> suspend le backend via engine
//
//  Configuration de la chaine audio :
//    AudioEditor -> AudioProcessor -> Engine -> BackendBase -> module
//    AudioProcessor ne connait pas BackendBase directement.
//
//  Relations :
//    AudioProcessor <-> Engine  (init, reconfigure, setAudioIO, release,
//                                configuration chaine)
//    AudioProcessor n'est PAS dans la boucle RT
// ============================================================================
#pragma once

#include "engine.h"

#include <memory>

namespace odenise::audio {

class AudioProcessor {
public:
    // -----------------------------------------------------------------------
    //  Construction / destruction
    // -----------------------------------------------------------------------

    // Cree l'engine avec les caps par defaut.
    AudioProcessor();

    // Cree l'engine avec des caps et config explicites.
    explicit AudioProcessor(const EngineCaps& caps, const RuntimeConfig& cfg);

    ~AudioProcessor() = default;

    AudioProcessor(const AudioProcessor&)            = delete;
    AudioProcessor& operator=(const AudioProcessor&) = delete;

    // -----------------------------------------------------------------------
    //  Cycle de vie audio
    // -----------------------------------------------------------------------

    // [CTRL] Prepare la chaine pour un sample_rate et une taille de bloc.
    // Reconfigure l'engine et le backend via engine->reconfigure().
    // A appeler depuis prepareToPlay() (JUCE) ou l'equivalent ALSA/CLAP.
    // Retourne false si l'engine n'est pas disponible.
    bool prepare(int sample_rate, int block_size);

    // [CTRL] Cable les pointeurs audio de l'interface sur le backend.
    // Delegue a engine->setAudioIO() (a ajouter dans Engine).
    // A appeler apres prepare(), quand les buffers sont connus.
    bool setAudioIO(TrackIO io);

    // [CTRL] Suspend le backend via engine->release() (a ajouter dans Engine).
    // A appeler depuis releaseResources() (JUCE) avant destruction ou
    // changement d'interface.
    void release();

    // -----------------------------------------------------------------------
    //  Configuration de la chaine audio
    //  Delegue a Engine qui propage : Engine -> BackendBase -> module.
    //  AudioProcessor ne connait pas BackendBase ni ModuleBase directement.
    //
    //  TODO : les methodes Engine correspondantes sont a ajouter :
    //    engine->bindModule(kind, id, position, cfg)
    //    engine->insertModule(kind, id, position, cfg)
    //    engine->replaceModule(kind, id, position, cfg)
    //    engine->removeModule(kind, position)
    //    engine->reconfigure(module_id, cfg)   <- reconfigure un module par id
    // -----------------------------------------------------------------------

    // Installe un module charge depuis le registry a la position donnee.
    bool installModule(ModuleKind kind, int module_id,
                       int position, const RuntimeConfig& cfg);

    // Insere un module a la position donnee (decale les suivants).
    bool insertModule(ModuleKind kind, int module_id,
                      int position, const RuntimeConfig& cfg);

    // Remplace le module a la position donnee.
    bool replaceModule(ModuleKind kind, int module_id,
                       int position, const RuntimeConfig& cfg);

    // Retire le module a la position donnee dans la chaine.
    void removeModule(ModuleKind kind, int position);

    // Reconfigure un module specifique par son id.
    // cfg peut etre une sous-classe de RuntimeConfig (cast dans le module).
    bool reconfigureModule(int module_id, const RuntimeConfig& cfg);

    // -----------------------------------------------------------------------
    //  Acces a l'engine (pour AudioEditor et le wrapper JUCE)
    // -----------------------------------------------------------------------
    Engine* engine() const noexcept { return engine_.get(); }

private:
    std::unique_ptr<Engine> engine_;
    RuntimeConfig           cfg_;         // config courante (pour prepare())
    int                     sample_rate_ = 0;
    int                     block_size_  = 0;
};

} // namespace odenise::audio

// ============================================================================
//  src/core/audio/AudioProcessor.h  --  Couche audio odenise.
//
//  Independante de JUCE, gtkmm et de tout framework audio.
//  Possede l'engine. Orchestre la preparation, le cablage audio et la
//  configuration de la chaine via Engine.
//
//  Cycle de vie :
//    [CTRL] prepare(sample_rate, block_size)
//              -> reconfigure engine + backend
//    [CTRL] setAudioIO(io)
//              -> cable les pointeurs audio sur le backend (hors RT)
//    [RT]   la couche audio appelle backend directement -- engine hors boucle
//    [CTRL] release()
//              -> suspend le backend via engine
//
//  Configuration de la chaine (une seule chaine, tous kinds confondus) :
//    AudioEditor -> AudioProcessor -> Engine -> BackendBase -> chaine
//
//  Identifiants :
//    available_id : id dans registry.available_ (pour insert/replace)
//    loaded_id    : id dans registry.loaded_     (pour reconfigure/remove)
// ============================================================================
#pragma once

#include "engine.h"

#include <memory>
// ---------------------------------------------------------------------------
//  Visibilite des symboles de libodenise_audio.
//  ODENISE_AUDIO_API : exporte depuis la lib, importe chez le consommateur.
//  Meme patron que ODENISE_API / LOGGER dans le reste du projet.
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(__MINGW32__)
    #ifdef AUDIO_EXPORTS
        #define AUDIO __declspec(dllexport)
    #elif defined(AUDIO_IMPORTS)
        #define AUDIO __declspec(dllimport)
    #else
        #define AUDIO
    #endif
#else
    #ifdef AUDIO_EXPORTS
        #define AUDIO __attribute__((visibility("default")))
    #else
        #define AUDIO
    #endif
#endif
namespace odenise::audio {

class AudioProcessor {
public:
    // -----------------------------------------------------------------------
    //  Construction / destruction
    // -----------------------------------------------------------------------
    AudioProcessor();
    explicit AudioProcessor(const EngineCaps& caps, const RuntimeConfig& cfg);

    ~AudioProcessor() = default;

    AudioProcessor(const AudioProcessor&)            = delete;
    AudioProcessor& operator=(const AudioProcessor&) = delete;

    // -----------------------------------------------------------------------
    //  Cycle de vie audio
    // -----------------------------------------------------------------------

    // [CTRL] Prepare la chaine (sample_rate, block_size).
    // Reconfigure l'engine et le backend.
    // TODO : Engine::reconfigure(EngineCaps, RuntimeConfig) a ajouter.
    AUDIO bool prepare(int sample_rate, int block_size);

    // [CTRL] Cable les pointeurs audio sur le backend via engine.
    // TODO : Engine::setAudioIO(TrackIO) a ajouter.
    AUDIO bool setAudioIO(TrackIO io);

    // [CTRL] Suspend le backend via engine.
    // TODO : Engine::release() a ajouter.
    AUDIO void release();

    // -----------------------------------------------------------------------
    //  Configuration de la chaine audio
    //  Delegue a Engine -> BackendBase -> chaine (unique, tous kinds).
    //
    //  TODO : methodes Engine correspondantes a ajouter :
    //    engine->insertModule(available_id, position, cfg)
    //    engine->replaceModule(available_id, position, cfg)
    //    engine->removeModule(position)
    //    engine->reconfigureModule(loaded_id, cfg)
    // -----------------------------------------------------------------------

    // Charge depuis available_ et insere a la position donnee.
    // installModule = insertModule en derniere position.
    AUDIO bool insertModule(size_t available_id, size_t position, const RuntimeConfig& cfg);

    // Charge depuis available_ et remplace le module a la position donnee.
    AUDIO bool replaceModule(size_t available_id, size_t position, const RuntimeConfig& cfg);

    // Retire le module a la position donnee, le decharge de loaded_.
    AUDIO void removeModule(size_t position);

    // Reconfigure un module deja dans loaded_ par son loaded_id.
    // cfg peut etre une sous-classe de RuntimeConfig (cast dans le module).
    AUDIO bool reconfigureModule(size_t loaded_id, const RuntimeConfig& cfg);

    // -----------------------------------------------------------------------
    //  Acces a l'engine (pour AudioEditor)
    // -----------------------------------------------------------------------
    Engine* engine() const noexcept { return engine_.get(); }

private:
    std::unique_ptr<Engine> engine_;
    RuntimeConfig           cfg_;
    int                     sample_rate_ = 0;
    int                     block_size_  = 0;
};

} // namespace odenise::audio

// ============================================================================
//  src/plugin/juce/ui/AudioChainView.h  --  Vue graphique de l'AudioChain.
//
//  Affiche les modules charges sous forme de blocs connectes.
//  Chaque bloc expose ses ports (entrees/sorties) colores par kind :
//    kPortAudio    -> jaune
//    kPortSpectral -> bleu
//    kPortCtrl     -> rouge
//    kPortSync     -> violet
//
//  Interactions :
//    - Clic sur un bloc    : selection du module
//    - Drag d'un bloc      : repositionnement (moveNode dans AudioEditor)
//    - Drag port -> port   : connexion (connectPorts dans AudioEditor)
//    - Clic droit arête    : deconnexion (disconnectPort dans AudioEditor)
//    - Toggle layout       : horizontal <-> vertical
//
//  La vue est prescriptive ET descriptive :
//    - elle lit graph() depuis AudioEditor apres chaque modification.
//    - elle pousse les changements (connect/disconnect/move) dans AudioEditor.
//
//  L'AudioEditor est la source de verite ; AudioChainView n'en est que le
//  miroir editable. rebuildGraph() doit etre appele avant refresh().
// ============================================================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEditor.h"

namespace odenise::plugin {

// ---------------------------------------------------------------------------
//  Couleurs normalisees par PortKind (palette pro studio/Eurorack).
// ---------------------------------------------------------------------------
inline juce::Colour portKindColour(odenise::PortKind kind) noexcept {
    switch (kind) {
        case odenise::kPortAudio:    return juce::Colour(0xFFE8C840); // jaune patchbay
        case odenise::kPortSpectral: return juce::Colour(0xFF4090E0); // bleu numerique
        case odenise::kPortCtrl:     return juce::Colour(0xFFE04040); // rouge CV/gate
        case odenise::kPortSync:     return juce::Colour(0xFFAA44CC); // violet word clock
        default: {
            // Kind tiers : teinte derivee de l'id pour etre reproductible.
            const uint8_t h = static_cast<uint8_t>((kind * 61) & 0xFF);
            return juce::Colour::fromHSV(h / 255.0f, 0.7f, 0.85f, 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------
//  Callback de selection de module (loaded_id, ou -1 si deselection).
// ---------------------------------------------------------------------------
using ModuleSelectedCallback = std::function<void(int loaded_id)>;

// ---------------------------------------------------------------------------
//  AudioChainView
// ---------------------------------------------------------------------------
class AudioChainView : public juce::Component {
public:
    // -----------------------------------------------------------------------
    //  Construction.
    //  editor      : AudioEditor source de verite (non possede).
    //  on_selected : callback appele lors de la selection d'un bloc.
    // -----------------------------------------------------------------------
    explicit AudioChainView(odenise::audio::AudioEditor* editor,
                            ModuleSelectedCallback       on_selected = nullptr);
    ~AudioChainView() override = default;

    // -----------------------------------------------------------------------
    //  Rafraichit la vue depuis graph() de l'AudioEditor.
    //  A appeler depuis le thread UI apres toute modification structurelle.
    // -----------------------------------------------------------------------
    void refresh();

    // -----------------------------------------------------------------------
    //  Bascule la disposition des blocs : horizontal <-> vertical.
    // -----------------------------------------------------------------------
    void toggleLayout();

    // juce::Component
    void paint   (juce::Graphics& g)             override;
    void resized ()                              override;
    void mouseDown (const juce::MouseEvent& e)   override;
    void mouseDrag (const juce::MouseEvent& e)   override;
    void mouseUp   (const juce::MouseEvent& e)   override;

private:
    // -----------------------------------------------------------------------
    //  Geometrie d'un bloc de module.
    // -----------------------------------------------------------------------
    struct BlockGeom {
        int          loaded_id;
        juce::String label;         // nom du module
        juce::String kind_label;    // kind (Suppression, ComputeBackend, ...)

        // Ports : positions calculees lors de paint/resized.
        struct PortGeom {
            int          port_id;
            odenise::PortKind kind;
            odenise::PortDir  dir;
            juce::String name;
            juce::Point<int> centre; // position absolue dans le composant
        };
        std::vector<PortGeom> ports;

        juce::Rectangle<int> bounds; // rectangle du bloc
    };

    // -----------------------------------------------------------------------
    //  Etat du drag en cours.
    // -----------------------------------------------------------------------
    enum class DragMode { None, MoveBlock, DrawEdge };

    struct DragState {
        DragMode mode         = DragMode::None;
        int      block_idx    = -1;  // index dans blocks_
        int      port_idx     = -1;  // index dans block.ports (DrawEdge)
        juce::Point<int> origin;     // position souris au debut du drag
        juce::Point<int> current;    // position souris courante
    };

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------
    void   rebuildBlocks();  // reconstruit blocks_ depuis graph_
    void   layoutBlocks();   // calcule bounds + ports depuis layout_ et taille
    void   drawBlock  (juce::Graphics& g, const BlockGeom& blk, bool selected) const;
    void   drawEdges  (juce::Graphics& g) const;
    void   drawDragEdge(juce::Graphics& g) const;

    // Retourne l'index dans blocks_ sous le point, ou -1.
    int    hitTestBlock(juce::Point<int> pt) const;
    // Retourne l'index de port sous le point dans un bloc, ou -1.
    int    hitTestPort (const BlockGeom& blk, juce::Point<int> pt) const;

    // Construit la courbe de Bézier entre deux centres de ports.
    static juce::Path bezierEdge(juce::Point<int> from, juce::Point<int> to);

    // -----------------------------------------------------------------------
    //  Donnees
    // -----------------------------------------------------------------------
    odenise::audio::AudioEditor* editor_      = nullptr;
    ModuleSelectedCallback       on_selected_;

    std::vector<BlockGeom>       blocks_;
    int                          selected_block_ = -1;  // index dans blocks_

    DragState                    drag_;

    enum class Layout { Horizontal, Vertical };
    Layout                       layout_ = Layout::Horizontal;

    // Geometrie des blocs
    static constexpr int kBlockW     = 120; // largeur d'un bloc
    static constexpr int kBlockH     = 80;  // hauteur d'un bloc
    static constexpr int kPortRadius = 6;   // rayon d'un point de port
    static constexpr int kBlockGapH  = 60;  // espacement horizontal entre blocs
    static constexpr int kBlockGapV  = 50;  // espacement vertical entre blocs
    static constexpr int kCornerR    = 6;   // rayon des coins arrondis
};

} // namespace odenise::plugin

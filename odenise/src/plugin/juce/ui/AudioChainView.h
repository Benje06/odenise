// ============================================================================
//  src/plugin/juce/ui/AudioChainView.h  --  Vue graphique de l'AudioChain.
//
//  Affiche les modules charges sous forme de blocs connectes.
//  Chaque bloc expose ses ports colores par kind :
//    kPortAudio    -> jaune  (convention patchbay analogique)
//    kPortSpectral -> bleu   (numerique/analyse)
//    kPortCtrl     -> rouge  (CV/gate Eurorack, automation DAW)
//    kPortSync     -> violet (word clock, DIN sync)
//
//  Interactions :
//    - Clic sur un bloc      : selection
//    - Drag d'un bloc        : repositionnement (positions_ local)
//    - Drag port -> port     : connexion -> AudioEditor::connectPorts()
//    - Clic droit sur arete  : deconnexion -> AudioEditor::disconnectPort()
//    - Bouton H/V            : bascule disposition
//
//  Prescriptive ET descriptive :
//    - lit get_chain() depuis AudioEditor (etat courant de l'AudioChain)
//    - pousse les connexions/deconnexions dans AudioEditor
//    - gere les positions UI des blocs en local (positions_)
//
//  refresh() doit etre appele depuis le thread UI apres toute modification
//  structurelle de la chaine (insert, replace, remove).
//  blocks_ n'est reconstruit que sur refresh(), jamais dans paint().
// ============================================================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEditor.h"
#include <unordered_map>

namespace odenise::plugin {

// ---------------------------------------------------------------------------
//  Couleur normalisee par PortKind (palette pro studio/Eurorack).
// ---------------------------------------------------------------------------
inline juce::Colour portKindColour(PortKind kind) noexcept {
    switch (kind) {
        case kPortAudio:    return juce::Colour(0xFFE8C840); // jaune patchbay
        case kPortSpectral: return juce::Colour(0xFF4090E0); // bleu numerique
        case kPortCtrl:     return juce::Colour(0xFFE04040); // rouge CV/gate
        case kPortSync:     return juce::Colour(0xFFAA44CC); // violet word clock
        default: {
            // Kind tiers : teinte derivee de l'id, reproductible.
            const uint8_t h = static_cast<uint8_t>((kind * 61) & 0xFF);
            return juce::Colour::fromHSV(h / 255.0f, 0.7f, 0.85f, 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------
//  AudioChainView
// ---------------------------------------------------------------------------
class AudioChainView : public juce::Component {
public:
    explicit AudioChainView(odenise::audio::AudioEditor* editor);
    ~AudioChainView() override = default;

    // Rafraichit blocks_ depuis graph() de l'AudioEditor.
    // A appeler depuis le thread UI apres toute modification structurelle.
    // Ne doit PAS etre appele depuis paint().
    void refresh();

    // Bascule la disposition des blocs : horizontal <-> vertical.
    void toggleLayout();

    void paint    (juce::Graphics& g)           override;
    void resized  ()                            override;
    void mouseDown(const juce::MouseEvent& e)   override;
    void mouseDrag(const juce::MouseEvent& e)   override;
    void mouseUp  (const juce::MouseEvent& e)   override;

private:
    // -----------------------------------------------------------------------
    //  Geometrie d'un port dans un bloc.
    // -----------------------------------------------------------------------
    struct PortGeom {
        int              port_id;
        PortKind         kind;
        PortDir          dir;
        juce::String     name;
        juce::Point<int> centre; // position absolue dans le composant
    };

    // -----------------------------------------------------------------------
    //  Geometrie d'un bloc de module.
    //  Reconstruit sur refresh(), stable entre deux refresh().
    // -----------------------------------------------------------------------
    struct BlockGeom {
        size_t               loaded_id;
        juce::String         label;      // nom du module
        juce::String         kind_label; // kindName()
        std::vector<PortGeom> ports;
        juce::Rectangle<int> bounds;
    };

    // -----------------------------------------------------------------------
    //  Etat du drag en cours.
    // -----------------------------------------------------------------------
    enum class DragMode { None, MoveBlock, DrawEdge };

    struct DragState {
        DragMode         mode      = DragMode::None;
        int              block_idx = -1; // index dans blocks_
        int              port_idx  = -1; // index dans block.ports (DrawEdge)
        juce::Point<int> origin;
        juce::Point<int> current;
    };

    // -----------------------------------------------------------------------
    //  Helpers
    // -----------------------------------------------------------------------
    void rebuildBlocks();   // reconstruit blocks_ depuis get_chain() -- appele par refresh()
    void layoutBlocks();    // calcule bounds + centres de ports
    void computePortCentres(BlockGeom& blk);
    void distributePortsOnEdge(std::vector<PortGeom*>& ports,
                               int edge_coord, int start, int length, bool vertical);

    void drawBlock   (juce::Graphics& g, const BlockGeom& blk, bool selected) const;
    void drawEdges   (juce::Graphics& g) const;
    void drawDragEdge(juce::Graphics& g) const;

    int  hitTestBlock(juce::Point<int> pt) const;
    int  hitTestPort (const BlockGeom& blk, juce::Point<int> pt) const;

    static juce::Path bezierEdge(juce::Point<int> from, juce::Point<int> to);

    // -----------------------------------------------------------------------
    //  Donnees
    // -----------------------------------------------------------------------
    odenise::audio::AudioEditor* editor_       = nullptr;
    std::vector<BlockGeom>       blocks_;       // stable entre deux refresh()
    // Positions UI des blocs, indexees par loaded_id.
    // Propriete exclusive de AudioChainView -- jamais transmises au coeur.
    // Initialisees par layoutBlocks() pour les nouveaux noeuds,
    // mises a jour par mouseUp (MoveBlock).
    std::unordered_map<size_t, juce::Point<int>> positions_;
    // Derniere ChainDesc lue depuis AudioEditor::get_chain().
    // Mise a jour par refresh(), lue par drawEdges() et rebuildBlocks().
    odenise::audio::ChainDesc    chain_desc_;
    int                          selected_block_ = -1;
    DragState                    drag_;

    enum class Layout { Horizontal, Vertical };
    Layout layout_ = Layout::Horizontal;

    // Geometrie
    static constexpr int kBlockW    = 120;
    static constexpr int kBlockH    = 80;
    static constexpr int kPortR     = 6;   // rayon d'un point de port
    static constexpr int kBlockGapH = 60;
    static constexpr int kBlockGapV = 50;
    static constexpr int kCornerR   = 6;
};

} // namespace odenise::plugin

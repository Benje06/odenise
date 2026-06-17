// ============================================================================
//  src/plugin/juce/ui/AudioChainView.cpp
// ============================================================================
#include "AudioChainView.h"
#include "engine.h"

namespace odenise::plugin {

// ============================================================================
//  Construction
// ============================================================================
AudioChainView::AudioChainView(odenise::audio::AudioEditor* editor,
                               ModuleSelectedCallback       on_selected)
    : editor_(editor)
    , on_selected_(std::move(on_selected))
{
    setOpaque(false);
}

// ============================================================================
//  Rafraichissement depuis le graphe
// ============================================================================
void AudioChainView::refresh() {
    rebuildBlocks();
    layoutBlocks();
    repaint();
}

void AudioChainView::toggleLayout() {
    layout_ = (layout_ == Layout::Horizontal)
              ? Layout::Vertical
              : Layout::Horizontal;
    layoutBlocks();
    repaint();
}

// ============================================================================
//  Reconstruction des blocs depuis graph() de l'AudioEditor
// ============================================================================
void AudioChainView::rebuildBlocks() {
    blocks_.clear();
    if (!editor_) return;

    const auto& graph = editor_->graph();
    const auto& mods  = editor_->loaded_modules(); // liste des ModuleInfo (loaded)

    for (const auto& nd : graph.nodes) {
        BlockGeom blk;
        blk.loaded_id = nd.loaded_id;
        blk.bounds    = juce::Rectangle<int>(nd.x, nd.y, kBlockW, kBlockH);

        // Cherche le ModuleInfo correspondant pour le nom et le kind.
        for (const auto& info : mods) {
            if (static_cast<int>(info.id) == nd.loaded_id) {
                blk.label      = juce::String(info.name);
                blk.kind_label = juce::String(odenise::kindName(info.kind));
                break;
            }
        }
        if (blk.label.isEmpty())
            blk.label = "module #" + juce::String(nd.loaded_id);

        // Recupere les ports depuis la vtable C++ du module charge.
        // On passe par engine()->modules() qui expose les ModuleInfo ;
        // pour les PortDef on cast le ModuleBase depuis le registre.
        // Pour l'instant on construit des ports generiques si ports() = nullptr
        // (le module ne surcharge pas encore ports()).
        // Sera affine quand les modules concrets implementeront ports().
        blk.ports.clear();

        // Ports generiques par defaut (audio_in + audio_out) en attendant
        // que chaque module surcharge ports().
        BlockGeom::PortGeom pin;
        pin.port_id = 0;
        pin.kind    = odenise::kPortAudio;
        pin.dir     = odenise::PortDir::In;
        pin.name    = "audio_in";
        blk.ports.push_back(pin);

        BlockGeom::PortGeom pout;
        pout.port_id = 1;
        pout.kind    = odenise::kPortAudio;
        pout.dir     = odenise::PortDir::Out;
        pout.name    = "audio_out";
        blk.ports.push_back(pout);

        blocks_.push_back(blk);
    }
}

// ============================================================================
//  Layout : calcule bounds et centres de ports
// ============================================================================
void AudioChainView::layoutBlocks() {
    const int w = getWidth();
    const int h = getHeight();
    if (blocks_.empty() || w == 0 || h == 0) return;

    if (layout_ == Layout::Horizontal) {
        // Disposition horizontale : blocs alignes en ligne, centres verticalement.
        const int total_w = static_cast<int>(blocks_.size()) * kBlockW
                          + static_cast<int>(blocks_.size() - 1) * kBlockGapH;
        int x = (w - total_w) / 2;
        const int y = (h - kBlockH) / 2;

        for (auto& blk : blocks_) {
            blk.bounds = juce::Rectangle<int>(x, y, kBlockW, kBlockH);
            x += kBlockW + kBlockGapH;
            computePortCentres(blk);
        }
    } else {
        // Disposition verticale : blocs alignes en colonne, centres horizontalement.
        const int total_h = static_cast<int>(blocks_.size()) * kBlockH
                          + static_cast<int>(blocks_.size() - 1) * kBlockGapV;
        const int x = (w - kBlockW) / 2;
        int y = (h - total_h) / 2;

        for (auto& blk : blocks_) {
            blk.bounds = juce::Rectangle<int>(x, y, kBlockW, kBlockH);
            y += kBlockH + kBlockGapV;
            computePortCentres(blk);
        }
    }
}

// Calcule les centres absolus des ports d'un bloc selon sa position.
// Les ports In sont sur le cote gauche (horizontal) ou en haut (vertical).
// Les ports Out sont sur le cote droit (horizontal) ou en bas (vertical).
void AudioChainView::computePortCentres(BlockGeom& blk) {
    std::vector<BlockGeom::PortGeom*> ins, outs;
    for (auto& pg : blk.ports)
        (pg.dir == odenise::PortDir::In ? ins : outs).push_back(&pg);

    const auto& b = blk.bounds;

    if (layout_ == Layout::Horizontal) {
        // Entrees : cote gauche, espaces verticalement.
        distributePortsOnEdge(ins,  b.getX(),           b.getY(), b.getHeight(), true);
        // Sorties : cote droit, espaces verticalement.
        distributePortsOnEdge(outs, b.getRight(),        b.getY(), b.getHeight(), true);
    } else {
        // Entrees : bord superieur, espaces horizontalement.
        distributePortsOnEdge(ins,  b.getY(),            b.getX(), b.getWidth(),  false);
        // Sorties : bord inferieur, espaces horizontalement.
        distributePortsOnEdge(outs, b.getBottom(),       b.getX(), b.getWidth(),  false);
    }
}

// Repartit les ports sur un bord (vertical ou horizontal).
// edge_coord : coordonnee fixe du bord (x pour vertical, y pour horizontal).
// start      : coordonnee de depart de l'axe de distribution.
// length     : longueur disponible sur cet axe.
// vertical   : true = ports sur bord vertical (x fixe, y varie).
void AudioChainView::distributePortsOnEdge(
    std::vector<BlockGeom::PortGeom*>& ports,
    int edge_coord, int start, int length, bool vertical)
{
    if (ports.empty()) return;
    const int step = length / (static_cast<int>(ports.size()) + 1);
    int pos = start + step;
    for (auto* pg : ports) {
        pg->centre = vertical
            ? juce::Point<int>(edge_coord, pos)
            : juce::Point<int>(pos, edge_coord);
        pos += step;
    }
}

// ============================================================================
//  Paint
// ============================================================================
void AudioChainView::paint(juce::Graphics& g) {
    // Fond transparent -- la fenetre parente fournit l'arriere-plan.
    g.setColour(juce::Colours::transparentBlack);
    g.fillAll();

    // Aretes entre modules.
    drawEdges(g);

    // Arete en cours de drag (connexion en creation).
    if (drag_.mode == DragMode::DrawEdge)
        drawDragEdge(g);

    // Blocs.
    for (int i = 0; i < static_cast<int>(blocks_.size()); ++i)
        drawBlock(g, blocks_[i], i == selected_block_);
}

void AudioChainView::drawBlock(juce::Graphics& g,
                               const BlockGeom& blk,
                               bool selected) const {
    const auto& b = blk.bounds;

    // Corps du bloc.
    const juce::Colour bg = selected
        ? juce::Colour(0xFF2A3A4A)
        : juce::Colour(0xFF1E2A34);
    const juce::Colour border = selected
        ? juce::Colour(0xFF80C0FF)
        : juce::Colour(0xFF4A6070);

    g.setColour(bg);
    g.fillRoundedRectangle(b.toFloat(), static_cast<float>(kCornerR));
    g.setColour(border);
    g.drawRoundedRectangle(b.toFloat(), static_cast<float>(kCornerR), 1.5f);

    // Label nom du module.
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(blk.label,
               b.getX() + 4, b.getY() + 8,
               b.getWidth() - 8, 16,
               juce::Justification::centred, true);

    // Label kind.
    g.setColour(juce::Colour(0xFFAABBCC));
    g.setFont(juce::Font(10.0f));
    g.drawText(blk.kind_label,
               b.getX() + 4, b.getY() + 26,
               b.getWidth() - 8, 14,
               juce::Justification::centred, true);

    // Points de port.
    for (const auto& pg : blk.ports) {
        const juce::Colour pc = portKindColour(pg.kind);
        const auto centre = pg.centre.toFloat();
        const float r = static_cast<float>(kPortRadius);

        g.setColour(pc.darker(0.3f));
        g.fillEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f);
        g.setColour(pc);
        g.drawEllipse(centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, 1.5f);

        // Nom du port en petit.
        g.setColour(pc.brighter(0.2f));
        g.setFont(juce::Font(9.0f));
        const bool is_in = (pg.dir == odenise::PortDir::In);
        const int lx = is_in
            ? centre.x + kPortRadius + 2
            : centre.x - kPortRadius - 42;
        g.drawText(pg.name,
                   static_cast<int>(lx), static_cast<int>(centre.y) - 7,
                   40, 14,
                   is_in ? juce::Justification::left : juce::Justification::right,
                   true);
    }
}

void AudioChainView::drawEdges(juce::Graphics& g) const {
    if (!editor_) return;
    const auto& edges = editor_->graph().edges;

    for (const auto& ed : edges) {
        // Trouve les centres des ports source et destination.
        juce::Point<int> from_pt, to_pt;
        bool from_ok = false, to_ok = false;

        for (const auto& blk : blocks_) {
            if (blk.loaded_id == ed.from.node_loaded_id) {
                for (const auto& pg : blk.ports) {
                    if (pg.port_id == ed.from.port_id) {
                        from_pt = pg.centre;
                        from_ok = true;
                        break;
                    }
                }
            }
            if (blk.loaded_id == ed.to.node_loaded_id) {
                for (const auto& pg : blk.ports) {
                    if (pg.port_id == ed.to.port_id) {
                        to_pt = pg.centre;
                        to_ok = true;
                        break;
                    }
                }
            }
        }
        if (!from_ok || !to_ok) continue;

        // Couleur de l'arete : kind du port source.
        odenise::PortKind edge_kind = odenise::kPortAudio;
        for (const auto& blk : blocks_) {
            if (blk.loaded_id == ed.from.node_loaded_id) {
                for (const auto& pg : blk.ports) {
                    if (pg.port_id == ed.from.port_id) {
                        edge_kind = pg.kind;
                        break;
                    }
                }
            }
        }

        g.setColour(portKindColour(edge_kind).withAlpha(0.8f));
        g.strokePath(bezierEdge(from_pt, to_pt),
                     juce::PathStrokeType(2.0f));
    }
}

void AudioChainView::drawDragEdge(juce::Graphics& g) const {
    if (drag_.block_idx < 0 || drag_.port_idx < 0) return;
    const auto& blk = blocks_[drag_.block_idx];
    const auto& pg  = blk.ports[drag_.port_idx];

    g.setColour(portKindColour(pg.kind).withAlpha(0.6f));
    g.strokePath(bezierEdge(pg.centre, drag_.current),
                 juce::PathStrokeType(2.0f,
                     juce::PathStrokeType::curved,
                     juce::PathStrokeType::rounded));
}

// Courbe de Bézier cubique entre deux points de port.
// Le vecteur de controle est horizontal en layout H, vertical en layout V.
juce::Path AudioChainView::bezierEdge(juce::Point<int> from,
                                       juce::Point<int> to) {
    const float fx = static_cast<float>(from.x);
    const float fy = static_cast<float>(from.y);
    const float tx = static_cast<float>(to.x);
    const float ty = static_cast<float>(to.y);
    const float cx = (fx + tx) * 0.5f;
    const float cy = (fy + ty) * 0.5f;

    juce::Path p;
    p.startNewSubPath(fx, fy);
    p.cubicTo(cx, fy, cx, ty, tx, ty);
    return p;
}

// ============================================================================
//  Interactions souris
// ============================================================================
void AudioChainView::mouseDown(const juce::MouseEvent& e) {
    const auto pt = e.getPosition();
    drag_ = DragState{};

    // Teste si on clique sur un port (debut de connexion).
    for (int bi = 0; bi < static_cast<int>(blocks_.size()); ++bi) {
        const int pi = hitTestPort(blocks_[bi], pt);
        if (pi >= 0) {
            drag_.mode      = DragMode::DrawEdge;
            drag_.block_idx = bi;
            drag_.port_idx  = pi;
            drag_.origin    = pt;
            drag_.current   = pt;
            return;
        }
    }

    // Teste si on clique sur un bloc (selection + debut de deplacement).
    const int bi = hitTestBlock(pt);
    if (bi >= 0) {
        selected_block_ = bi;
        drag_.mode      = DragMode::MoveBlock;
        drag_.block_idx = bi;
        drag_.origin    = pt;
        if (on_selected_)
            on_selected_(blocks_[bi].loaded_id);
        repaint();
        return;
    }

    // Clic dans le vide : deselection.
    selected_block_ = -1;
    if (on_selected_) on_selected_(-1);
    repaint();
}

void AudioChainView::mouseDrag(const juce::MouseEvent& e) {
    const auto pt = e.getPosition();

    if (drag_.mode == DragMode::MoveBlock && drag_.block_idx >= 0) {
        auto& blk = blocks_[drag_.block_idx];
        const int dx = pt.x - drag_.origin.x;
        const int dy = pt.y - drag_.origin.y;
        blk.bounds.translate(dx, dy);
        computePortCentres(blk);
        drag_.origin = pt;
        repaint();
    } else if (drag_.mode == DragMode::DrawEdge) {
        drag_.current = pt;
        repaint();
    }
}

void AudioChainView::mouseUp(const juce::MouseEvent& e) {
    const auto pt = e.getPosition();

    if (drag_.mode == DragMode::MoveBlock && drag_.block_idx >= 0 && editor_) {
        // Pousse la nouvelle position dans AudioEditor.
        const auto& blk = blocks_[drag_.block_idx];
        editor_->moveNode(blk.loaded_id, blk.bounds.getX(), blk.bounds.getY());
    }

    if (drag_.mode == DragMode::DrawEdge
        && drag_.block_idx >= 0
        && drag_.port_idx  >= 0
        && editor_)
    {
        // Cherche un port cible sous le curseur.
        for (int bi = 0; bi < static_cast<int>(blocks_.size()); ++bi) {
            if (bi == drag_.block_idx) continue;
            const int pi = hitTestPort(blocks_[bi], pt);
            if (pi >= 0) {
                const auto& src = blocks_[drag_.block_idx];
                const auto& dst = blocks_[bi];
                editor_->connectPorts(
                    src.loaded_id, src.ports[drag_.port_idx].port_id,
                    dst.loaded_id, dst.ports[pi].port_id);
                editor_->rebuildGraph();
                refresh();
                break;
            }
        }
    }

    drag_ = DragState{};
    repaint();
}

void AudioChainView::resized() {
    layoutBlocks();
}

// ============================================================================
//  Hit tests
// ============================================================================
int AudioChainView::hitTestBlock(juce::Point<int> pt) const {
    for (int i = 0; i < static_cast<int>(blocks_.size()); ++i)
        if (blocks_[i].bounds.contains(pt))
            return i;
    return -1;
}

int AudioChainView::hitTestPort(const BlockGeom& blk,
                                 juce::Point<int> pt) const {
    for (int i = 0; i < static_cast<int>(blk.ports.size()); ++i) {
        const auto& pg = blk.ports[i];
        const int dx = pt.x - pg.centre.x;
        const int dy = pt.y - pg.centre.y;
        if (dx * dx + dy * dy <= (kPortRadius + 2) * (kPortRadius + 2))
            return i;
    }
    return -1;
}

// ============================================================================
//  Helpers geometrie ports (declarations manquantes dans .h -- stubs internes)
// ============================================================================
void AudioChainView::computePortCentres(BlockGeom& blk) {
    std::vector<BlockGeom::PortGeom*> ins, outs;
    for (auto& pg : blk.ports)
        (pg.dir == odenise::PortDir::In ? ins : outs).push_back(&pg);

    const auto& b = blk.bounds;
    if (layout_ == Layout::Horizontal) {
        distributePortsOnEdge(ins,  b.getX(),    b.getY(), b.getHeight(), true);
        distributePortsOnEdge(outs, b.getRight(), b.getY(), b.getHeight(), true);
    } else {
        distributePortsOnEdge(ins,  b.getY(),      b.getX(), b.getWidth(), false);
        distributePortsOnEdge(outs, b.getBottom(), b.getX(), b.getWidth(), false);
    }
}

void AudioChainView::distributePortsOnEdge(
    std::vector<BlockGeom::PortGeom*>& ports,
    int edge_coord, int start, int length, bool vertical)
{
    if (ports.empty()) return;
    const int step = length / (static_cast<int>(ports.size()) + 1);
    int pos = start + step;
    for (auto* pg : ports) {
        pg->centre = vertical
            ? juce::Point<int>(edge_coord, pos)
            : juce::Point<int>(pos, edge_coord);
        pos += step;
    }
}

} // namespace odenise::plugin

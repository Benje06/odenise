// ============================================================================
//  src/plugin/juce/ui/AudioChainView.cpp
// ============================================================================
#include "AudioChainView.h"

namespace odenise::plugin {

// ============================================================================
//  Construction
// ============================================================================
AudioChainView::AudioChainView(odenise::audio::AudioEditor* editor)
    : editor_(editor)
{
    setOpaque(false);
}

// ============================================================================
//  refresh() -- unique point de reconstruction de blocks_.
//  Appele depuis le thread UI apres modification structurelle.
//  JAMAIS appele depuis paint().
// ============================================================================
void AudioChainView::refresh() {
    if (editor_)
        chain_desc_ = editor_->get_chain();
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
//  rebuildBlocks() -- construit blocks_ depuis chain_desc_
// ============================================================================
void AudioChainView::rebuildBlocks() {
    blocks_.clear();
    if (!editor_) return;

    for (const auto& mi : chain_desc_.nodes) {
        BlockGeom blk;
        blk.loaded_id  = mi.id;
        blk.label      = juce::String(mi.name);
        blk.kind_label = juce::String(odenise::kindName(mi.kind));

        // Position UI depuis positions_ -- initialisee par layoutBlocks().
        const auto it = positions_.find(mi.id);
        if (it != positions_.end())
            blk.bounds = juce::Rectangle<int>(it->second.getX(), it->second.getY(),
                                              kBlockW, kBlockH);
        else
            blk.bounds = juce::Rectangle<int>(0, 0, kBlockW, kBlockH);

        blk.ports.clear();
        if (mi.ports && mi.port_count > 0) {
            for (int pi = 0; pi < mi.port_count; ++pi) {
                const auto& pd = mi.ports[pi];
                PortGeom pg;
                pg.port_id = pd.id;
                pg.kind    = pd.kind;
                pg.dir     = pd.dir;
                pg.name    = juce::String(pd.name ? pd.name : "");
                blk.ports.push_back(pg);
            }
        } else {
            // Ports par defaut si le module n'en declare pas.
            PortGeom pin;
            pin.port_id = 0; pin.kind = kPortAudio;
            pin.dir = PortDir::In; pin.name = "audio_in";
            blk.ports.push_back(pin);
           PortGeom pout;
            pout.port_id = 1; pout.kind = kPortAudio;
            pout.dir = PortDir::Out; pout.name = "audio_out";
            blk.ports.push_back(pout);
        }

        if (blk.label.isEmpty())
            blk.label = "module #" + juce::String(static_cast<int>(mi.id));

        blocks_.push_back(std::move(blk));
    }
}

// ============================================================================
//  layoutBlocks() -- calcule bounds et centres de ports
// ============================================================================
void AudioChainView::layoutBlocks() {
    const int w = getWidth();
    const int h = getHeight();
    if (blocks_.empty() || w == 0 || h == 0) return;

    if (layout_ == Layout::Horizontal) {
        const int total_w = static_cast<int>(blocks_.size()) * kBlockW
                          + static_cast<int>(blocks_.size() - 1) * kBlockGapH;
        int x = (w - total_w) / 2;
        const int y = (h - kBlockH) / 2;
        for (auto& blk : blocks_) {
             if (positions_.find(blk.loaded_id) == positions_.end()) {
                positions_[blk.loaded_id] = juce::Point<int>(x, y);
                blk.bounds = juce::Rectangle<int>(x, y, kBlockW, kBlockH);
            }
            x += kBlockW + kBlockGapH;
            computePortCentres(blk);
        }
    } else {
        const int total_h = static_cast<int>(blocks_.size()) * kBlockH
                          + static_cast<int>(blocks_.size() - 1) * kBlockGapV;
        const int x = (w - kBlockW) / 2;
        int y = (h - total_h) / 2;
        for (auto& blk : blocks_) {
            if (positions_.find(blk.loaded_id) == positions_.end()) {
                positions_[blk.loaded_id] = juce::Point<int>(x, y);
                blk.bounds = juce::Rectangle<int>(x, y, kBlockW, kBlockH);
            }
            y += kBlockH + kBlockGapV;
            computePortCentres(blk);
        }
    }
}

void AudioChainView::computePortCentres(BlockGeom& blk) {
    std::vector<PortGeom*> ins, outs;
    for (auto& pg : blk.ports)
        (pg.dir == PortDir::In ? ins : outs).push_back(&pg);

    const auto& b = blk.bounds;
    if (layout_ == Layout::Horizontal) {
        distributePortsOnEdge(ins,  b.getX(),     b.getY(), b.getHeight(), true);
        distributePortsOnEdge(outs, b.getRight(),  b.getY(), b.getHeight(), true);
    } else {
        distributePortsOnEdge(ins,  b.getY(),      b.getX(), b.getWidth(),  false);
        distributePortsOnEdge(outs, b.getBottom(), b.getX(), b.getWidth(),  false);
    }
}

void AudioChainView::distributePortsOnEdge(
    std::vector<PortGeom*>& ports,
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
//  paint() -- dessine uniquement ce qui est dans blocks_, sans reconstruire.
// ============================================================================
void AudioChainView::paint(juce::Graphics& g) {
    drawEdges(g);
    if (drag_.mode == DragMode::DrawEdge)
        drawDragEdge(g);
    for (int i = 0; i < static_cast<int>(blocks_.size()); ++i)
        drawBlock(g, blocks_[i], i == selected_block_);
}

void AudioChainView::drawBlock(juce::Graphics& g,
                               const BlockGeom& blk,
                               bool selected) const {
    const auto& b = blk.bounds;

    const juce::Colour bg     = selected ? juce::Colour(0xFF2A3A4A) : juce::Colour(0xFF1E2A34);
    const juce::Colour border = selected ? juce::Colour(0xFF80C0FF) : juce::Colour(0xFF4A6070);

    g.setColour(bg);
    g.fillRoundedRectangle(b.toFloat(), static_cast<float>(kCornerR));
    g.setColour(border);
    g.drawRoundedRectangle(b.toFloat(), static_cast<float>(kCornerR), 1.5f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f).withStyle("Bold")));
    g.drawText(blk.label,
               b.getX() + 4, b.getY() + 8, b.getWidth() - 8, 16,
               juce::Justification::centred, true);

    g.setColour(juce::Colour(0xFFAABBCC));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(blk.kind_label,
               b.getX() + 4, b.getY() + 26, b.getWidth() - 8, 14,
               juce::Justification::centred, true);

    for (const auto& pg : blk.ports) {
        const juce::Colour pc = portKindColour(pg.kind);
        const float cx = static_cast<float>(pg.centre.x);
        const float cy = static_cast<float>(pg.centre.y);
        const float r  = static_cast<float>(kPortR);

        g.setColour(pc.darker(0.3f));
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(pc);
        g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);

        g.setColour(pc.brighter(0.2f));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
        const bool is_in = (pg.dir == PortDir::In);
        const int lx = is_in
            ? pg.centre.x + kPortR + 2
            : pg.centre.x - kPortR - 42;
        g.drawText(pg.name,
                   lx, pg.centre.y - 7, 40, 14,
                   is_in ? juce::Justification::left : juce::Justification::right,
                   true);
    }
}

void AudioChainView::drawEdges(juce::Graphics& g) const {
    if (!editor_) return;
    for (const auto& conn : chain_desc_.connections) {
        juce::Point<int> from_pt, to_pt;
        bool from_ok = false, to_ok = false;
        PortKind edge_kind = kPortAudio;

        for (const auto& blk : blocks_) {
             if (blk.loaded_id == conn.from_loaded_id) {
                for (const auto& pg : blk.ports) {
                    if (pg.port_id == conn.from_port_id) {
                        from_pt   = pg.centre;
                        edge_kind = pg.kind;
                        from_ok   = true;
                        break;
                    }
                }
            }
             if (blk.loaded_id == conn.to_loaded_id) {
                for (const auto& pg : blk.ports) {
                    if (pg.port_id == conn.to_port_id) {
                        to_pt = pg.centre;
                        to_ok = true;
                        break;
                    }
                }
            }
        }
        if (!from_ok || !to_ok) continue;

        g.setColour(portKindColour(edge_kind).withAlpha(0.8f));
        g.strokePath(bezierEdge(from_pt, to_pt),
                     juce::PathStrokeType(2.0f));
    }
}

void AudioChainView::drawDragEdge(juce::Graphics& g) const {
    if (drag_.block_idx < 0 || drag_.port_idx < 0) return;
    const auto& pg = blocks_[drag_.block_idx].ports[drag_.port_idx];
    g.setColour(portKindColour(pg.kind).withAlpha(0.6f));
    g.strokePath(bezierEdge(pg.centre, drag_.current),
                 juce::PathStrokeType(2.0f,
                     juce::PathStrokeType::curved,
                     juce::PathStrokeType::rounded));
}

juce::Path AudioChainView::bezierEdge(juce::Point<int> from, juce::Point<int> to) {
    const float fx = static_cast<float>(from.x);
    const float fy = static_cast<float>(from.y);
    const float tx = static_cast<float>(to.x);
    const float ty = static_cast<float>(to.y);
    const float cx = (fx + tx) * 0.5f;

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

    // Teste d'abord les ports (priorite sur le clic de bloc).
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

    const int bi = hitTestBlock(pt);
    if (bi >= 0) {
        selected_block_ = bi;
        drag_.mode      = DragMode::MoveBlock;
        drag_.block_idx = bi;
        drag_.origin    = pt;
        repaint();
        return;
    }

    selected_block_ = -1;
    repaint();
}

void AudioChainView::mouseDrag(const juce::MouseEvent& e) {
    const auto pt = e.getPosition();

    if (drag_.mode == DragMode::MoveBlock && drag_.block_idx >= 0) {
        auto& blk = blocks_[drag_.block_idx];
        blk.bounds.translate(pt.x - drag_.origin.x, pt.y - drag_.origin.y);
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
        const auto& blk = blocks_[drag_.block_idx];
        // Position UI : propriete exclusive de AudioChainView.
        positions_[blk.loaded_id] = juce::Point<int>(blk.bounds.getX(),
                                                      blk.bounds.getY());
    }

    if (drag_.mode == DragMode::DrawEdge
        && drag_.block_idx >= 0
        && drag_.port_idx  >= 0
        && editor_)
    {
        for (int bi = 0; bi < static_cast<int>(blocks_.size()); ++bi) {
            if (bi == drag_.block_idx) continue;
            const int pi = hitTestPort(blocks_[bi], pt);
            if (pi >= 0) {
                const auto& src = blocks_[drag_.block_idx];
                const auto& dst = blocks_[bi];
                editor_->connectPorts(
                    src.loaded_id, src.ports[drag_.port_idx].port_id,
                    dst.loaded_id, dst.ports[pi].port_id);
                    // connectPorts() cable l'AudioChain.
                    // refresh() relit get_chain() pour mettre a jour blocks_.
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

int AudioChainView::hitTestPort(const BlockGeom& blk, juce::Point<int> pt) const {
    for (int i = 0; i < static_cast<int>(blk.ports.size()); ++i) {
        const auto& pg = blk.ports[i];
        const int dx = pt.x - pg.centre.x;
        const int dy = pt.y - pg.centre.y;
        if (dx * dx + dy * dy <= (kPortR + 2) * (kPortR + 2))
            return i;
    }
    return -1;
}

} // namespace odenise::plugin

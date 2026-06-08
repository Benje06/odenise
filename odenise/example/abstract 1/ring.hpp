#pragma once
#include <atomic>
#include <cstdint>
#include <cassert>
#include <sys/eventfd.h>
#include <unistd.h>

static constexpr int RING_SIZE = 64;  // puissance de 2

// ─── Slot ─────────────────────────────────────────────────────────────────────
// Unité de données dans le ring — seq est le référentiel commun

struct Slot {
    float*   data   = nullptr;
    int      frames = 0;
    uint64_t seq    = 0;
};

// ─── Ring ─────────────────────────────────────────────────────────────────────
// Un seul ring global, partagé par tous les modules
// Chaque module a ses propres Head(s) dessus

class Ring {
public:
    Slot slots[RING_SIZE];

    void write(uint64_t seq, const Slot& s) {
        slots[seq & (RING_SIZE - 1)] = s;
    }

    const Slot& read(uint64_t seq) const {
        return slots[seq & (RING_SIZE - 1)];
    }
};

// ─── Head ─────────────────────────────────────────────────────────────────────
// Curseur sur le ring — bloque kernel-side via eventfd
// push() : écrit dans le ring + signal
// wait() : bloque jusqu'à signal, retourne le slot

class Head {
public:
    explicit Head(Ring& ring) : ring_(ring), pos_(0) {
        fd_ = eventfd(0, EFD_SEMAPHORE);
        assert(fd_ >= 0);
    }
    ~Head() { close(fd_); }

    // Non copiable — possède un fd
    Head(const Head&)            = delete;
    Head& operator=(const Head&) = delete;

    void push(Slot s) {
        s.seq = pos_;
        ring_.write(pos_++, s);
        uint64_t v = 1;
        ::write(fd_, &v, sizeof(v));
    }

    const Slot& wait() {
        uint64_t v;
        ::read(fd_, &v, sizeof(v));      // bloque kernel-side, 0 CPU
        return ring_.read(pos_++);
    }

    // Retourne le slot à pos - decalage (blocs passés dans le ring)
    const Slot& peek(uint64_t decalage) const {
        return ring_.read(pos_ - decalage);
    }

    int      fd()  const { return fd_;  }
    uint64_t pos() const { return pos_; }

private:
    Ring&    ring_;
    uint64_t pos_;
    int      fd_;
};

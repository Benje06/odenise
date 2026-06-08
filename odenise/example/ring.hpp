#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <sys/eventfd.h>
#include <unistd.h>

// ─── Slot ────────────────────────────────────────────────────────────────────

struct Slot {
    float*   data     = nullptr;   // pointeur dans le pool scratch
    int      frames   = 0;
    uint64_t seq      = 0;         // numéro de séquence global
};

// ─── Ring global ─────────────────────────────────────────────────────────────
// Un seul ring partagé par tous les modules.
// Chaque module a son propre read_head (Head) et son propre write_head.
// La taille doit couvrir le module le plus lent.

static constexpr int RING_SIZE = 64;   // puissance de 2

class Ring {
public:
    Slot slots[RING_SIZE];

    // Écrit un slot à la position seq, signal sur le fd du slot
    void write(uint64_t seq, Slot s) {
        slots[seq & (RING_SIZE - 1)] = s;
    }

    const Slot& read(uint64_t seq) const {
        return slots[seq & (RING_SIZE - 1)];
    }
};

// ─── Head ────────────────────────────────────────────────────────────────────
// Curseur de lecture ou d'écriture sur le ring global.
// Un module possède un write_head et autant de read_heads que d'entrées.

class Head {
public:
    explicit Head(Ring& ring)
        : ring_(ring), pos_(0)
    {
        fd_ = eventfd(0, EFD_SEMAPHORE);
        assert(fd_ >= 0);
    }

    ~Head() { close(fd_); }

    // Producteur : écrit et signale
    void push(Slot s) {
        s.seq = pos_;
        ring_.write(pos_, s);
        pos_++;
        uint64_t v = 1;
        write(fd_, &v, sizeof(v));       // signal kernel — débloque les waiters
    }

    // Consommateur : bloque kernel-side jusqu'à disponibilité
    const Slot& wait_next() {
        uint64_t v;
        ::read(fd_, &v, sizeof(v));      // bloque dans le kernel, 0 CPU
        return ring_.read(pos_++);
    }

    int fd() const { return fd_; }

private:
    Ring&    ring_;
    uint64_t pos_;
    int      fd_;
};

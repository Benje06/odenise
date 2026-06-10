/* ----------------------------------------------------------------------------
 * tools/gxthread.cc -- gxthread dual thread provider
 * extended p_thread
 *
 * ----------------------------------------------------------------------------
 * copyright © 2006, 2007, 2008, 2009, 2010 Jérôme BENHAÏM <benhaimjerome@gmail.com>
 *
 * ----------------------------------------------------------------------------
 *
 *   This file is part of GxInterface.
 *
 *	 GxInterface is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ----------------------------------------------------------------------------
 *
 * Classe Thread :
 * classe générique qui comporte :
 * une méthode statique à faire éxécuter par un thread
 * une méthode abstraite à redéfinir qui retourne la condition de continuation de la boucle du thread
 * une méthode de lancement d'arret et de jonction : start arret join
 *
 */
#include "gxthread.h"
#include <thread>   // std::this_thread::yield (pause coopérative dans Run())

/* ============================================================================
 * Implémentation pthread (Linux / GCC / UCRT64-MinGW)
 * ========================================================================== */
#ifndef _MSC_VER

Thread::Thread()  {}
Thread::~Thread() {}

void* Thread::T_Loop(void* thread) {
    Thread* iThread = static_cast<Thread*>(thread);
    while (iThread->Run()) {}
    return nullptr;
}

void* Thread::T_Loop2(void* thread) {
    Thread* iThread = static_cast<Thread*>(thread);
    while (iThread->Run2()) {}
    return nullptr;
}

int Thread::S_Thread(void* (*f)(void*)) {
    if ((err = pthread_create(&thread, nullptr, f, static_cast<void*>(this)))) {
        std::string msg_err = error(__func__,
            _("cannot create thread"),
            std::to_string(err));
        LOG_ERR(msg_err);
        return err;
    }
    LOG("Thread: start");
    return 0;
}

int Thread::S_Thread()  { return S_Thread(&T_Loop);  }
int Thread::S_Thread2() { return S_Thread(&T_Loop2); }

int Thread::J_Thread() {
    LOG(LOG_IN());
    pthread_join(thread, nullptr);
    LOG(LOG_OUT());
    return 0;
}

int Thread::J_Thread2() {
    LOG(LOG_IN());
    pthread_join(thread2, nullptr);
    LOG(LOG_OUT());
    return 0;
}

int Thread::T_Thread() {
    LOG(LOG_IN());
    pthread_cancel(thread);
    J_Thread();
    LOG(LOG_OUT());
    return 0;
}

int Thread::T_Thread2() {
    LOG(LOG_IN());
    pthread_cancel(thread2);
    J_Thread2();
    LOG(LOG_OUT());
    return 0;
}

/* P_Thread -- suspend Run() de façon coopérative, RT-safe.
 * Pose pause_=true, puis busy-wait court jusqu'à ce que Run() confirme
 * sa suspension via paused_=true. Jamais appelé depuis Run().
 *
 * Convention attendue dans Run() :
 *   if (pause_requested()) {
 *       paused_.store(true,  std::memory_order_release);
 *       while (pause_requested()) { std::this_thread::yield(); }
 *       paused_.store(false, std::memory_order_release);
 *   }
 */
int Thread::P_Thread() {
    pause_.store(true, std::memory_order_release);
    while (!paused_.load(std::memory_order_acquire)) {}
    return 0;
}

/* R_Thread -- reprend Run() après une pause. */
int Thread::R_Thread() {
    pause_.store(false, std::memory_order_release);
    return 0;
}

/* P_Thread2 / R_Thread2 -- même logique pour Run2(). */
int Thread::P_Thread2() {
    pause2_.store(true, std::memory_order_release);
    while (!paused2_.load(std::memory_order_acquire)) {}
    return 0;
}

int Thread::R_Thread2() {
    pause2_.store(false, std::memory_order_release);
    return 0;
}

#else /* _MSC_VER */
/* ============================================================================
 * Implémentation std::thread (Windows / MSVC)
 *
 * T_Thread() : signale l'arrêt via stop_ (store release) puis joint thread_.
 * Run() doit consulter stop_requested() pour terminer -- pas de pthread_cancel
 * sous MSVC. Run2() / stop2_ suivent le même modèle.
 * ========================================================================== */

Thread::Thread()  {}
Thread::~Thread() {}

/* S_Thread_fn -- lance custom_fn_(this) dans thread_.
 * Sémantique identique à pthread_create(..., custom_fn_, this) :
 * la fonction reçoit this comme void*, son retour void* est ignoré. */
int Thread::S_Thread_fn() {
    try {
        stop_.store(false, std::memory_order_release);
        thread_ = std::thread([this] {
            custom_fn_(static_cast<void*>(this));
        });
        LOG("Thread: start (custom fn)");
    } catch (const std::exception& e) {
        std::string msg_err = error(__func__,
            _("cannot create thread (custom fn)"),
            e.what());
        LOG_ERR(msg_err);
        return 1;
    }
    return 0;
}

int Thread::S_Thread() {
    try {
        stop_.store(false, std::memory_order_release);
        thread_ = std::thread([this] {
            while (Run()) {}
        });
        LOG("Thread: start");
    } catch (const std::exception& e) {
        std::string msg_err = error(__func__,
            _("cannot create thread"),
            e.what());
        LOG_ERR(msg_err);
        return 1;
    }
    return 0;
}

int Thread::S_Thread2() {
    try {
        stop2_.store(false, std::memory_order_release);
        thread2_ = std::thread([this] {
            while (Run2()) {};
        });
        LOG("Thread2: start");
    } catch (const std::exception& e) {
        std::string msg_err = error(__func__,
            _("cannot create thread2"),
            e.what());
        LOG_ERR(msg_err);
        return 1;
    }
    return 0;
}

int Thread::J_Thread() {
    LOG(LOG_IN());
    if (thread_.joinable())
        thread_.join();
    LOG(LOG_OUT());
    return 0;
}

int Thread::J_Thread2() {
    LOG(LOG_IN());
    if (thread2_.joinable())
        thread2_.join();
    LOG(LOG_OUT());
    return 0;
}

/* T_Thread : signale l'arrêt à thread_ puis joint. */
int Thread::T_Thread() {
    LOG(LOG_IN());
    stop_.store(true, std::memory_order_release);   // specific windows msvc
    J_Thread();
    LOG(LOG_OUT());
    return 0;
}

/* T_Thread2 : signale l'arrêt à thread2_ puis joint. */
int Thread::T_Thread2() {
    LOG(LOG_IN());
    stop2_.store(true, std::memory_order_release);   // specific windows msvc
    J_Thread2();
    LOG(LOG_OUT());
    return 0;
}

/* P_Thread -- suspend Run() de façon coopérative, RT-safe.
 * Pose pause_=true, puis busy-wait court jusqu'à ce que Run() confirme
 * sa suspension via paused_=true. Jamais appelé depuis Run().
 *
 * Convention attendue dans Run() :
 *   if (pause_requested()) {
 *       paused_.store(true,  std::memory_order_release);
 *       while (pause_requested()) { std::this_thread::yield(); }
 *       paused_.store(false, std::memory_order_release);
 *   }
 */

// Le thread s'endort jusqu'à R_Thread()
int Thread::P_Thread() {
    LOG("Thread paused");
    pause_.store(true, std::memory_order_release);
    return 0;
}

// L'owner réveille le thread
int Thread::R_Thread() {
    LOG("Thread restart");
    pause_.store(false, std::memory_order_release);
    cv_.notify_one();
    return 0;
}

/* P_Thread2 / R_Thread2 -- même logique pour Run2(). */
int Thread::P_Thread2() {
    LOG("Thread_2 pause");
    pause2_.store(true, std::memory_order_release);
    return 0;
}

int Thread::R_Thread2() {
    LOG("Thread_2 restart");
    pause2_.store(false, std::memory_order_release);
    cv2_.notify_one();
    return 0;
}

#endif /* _MSC_VER */

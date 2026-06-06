/* ----------------------------------------------------------------------------
 * tools/gxthread.h -- gxthread header dual thread provider
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
 * Implémentation :
 *   - Linux / GCC   : pthread (comportement historique inchangé)
 *   - Windows / MSVC : std::thread + std::atomic<bool> stop_
 *     T_Thread() pose stop_ = true puis joint -- Run() doit consulter
 *     stop_ pour terminer (pas d'équivalent portable à pthread_cancel).
 *
 */
#ifndef GXTHREAD_H
#define GXTHREAD_H

#include "common.h"

#ifndef _MSC_VER
/* -------------------------------------------------------------------------
 * Implémentation pthread (Linux / GCC / UCRT64-MinGW)
 * ------------------------------------------------------------------------- */
#include <pthread.h>

class Thread {
private:
    int       err;
    pthread_t thread;
    pthread_t thread2;
    /* thread loop function */
    static void* T_Loop (void*);
    static void* T_Loop2(void*);
protected:
    /* function to define in your class, is what the thread do */
    virtual bool Run()  = 0;
    // Run2 est optionnel : à surcharger uniquement si un second thread est nécessaire.
    virtual bool Run2() = 0;
    /* internal start */
    int S_Thread(void* (*f)(void*));
    /* thread start / stop / join */
    int S_Thread();
    int S_Thread2();
    int J_Thread();
    int T_Thread();
    /* internal constructor */
    Thread(void* (*f)(void*)) { S_Thread(f); }
public:
    Thread();
    ~Thread();
};

#else /* _MSC_VER */
/* -------------------------------------------------------------------------
 * Implémentation std::thread (Windows / MSVC)
 * T_Thread() : pose stop_ = true puis joint thread_.
 * Run() doit consulter stop_ pour terminer proprement (pas de pthread_cancel
 * sous MSVC). Run2() / stop2_ suivent le même modèle.
 * ------------------------------------------------------------------------- */
#include <atomic>
#include <thread>

class Thread {
private:
    std::thread          thread_;
    std::thread          thread2_;
    std::atomic<bool>    stop_ {false};
    std::atomic<bool>    stop2_{false};
    /* fonction arbitraire passée au constructeur interne (peut être nullptr).
     * Sémantique identique à pthread_create(..., f, this) :
     * f reçoit this comme void*, son retour est ignoré. */
    void* (*custom_fn_)(void*) = nullptr;
    /* lance custom_fn_(this) dans thread_ -- appelé par le constructeur interne */
    int S_Thread_fn();
protected:
    /* function to define in your class, is what the thread do */
    virtual bool Run()  = 0;
    // Run2 est optionnel : à surcharger uniquement si un second thread est nécessaire.
    virtual bool Run2() = 0;
    /* thread start / stop / join */
    int S_Thread();
    int S_Thread2();
    int J_Thread();
    int T_Thread();
    /* internal constructor : lance f(this) dans thread_, comme pthread_create.
     * T_Thread() pose stop_ = true puis joint -- f doit consulter
     * stop_requested() pour terminer proprement. */
    Thread(void* (*f)(void*)) : custom_fn_(f) { S_Thread_fn(); }
public:
    /* Expose stop_ en lecture seule pour que Run() puisse le consulter.
     * Usage : while (!stop_requested() && /* condition métier *\/) { ... } */
    bool stop_requested()  const noexcept { return stop_ .load(std::memory_order_acquire); }
    bool stop2_requested() const noexcept { return stop2_.load(std::memory_order_acquire); }
    Thread();
    ~Thread();
};

#endif /* _MSC_VER */

#endif /* GXTHREAD_H */

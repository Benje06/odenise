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
 * Pause coopérative (commune aux deux implémentations) :
 *   P_Thread() / P_Thread2() : pose pause_=true, attend paused_=true (hors RT).
 *   R_Thread() / R_Thread2() : pose pause_=false, Run() reprend.
 *   Run() doit implémenter :
 *     if (pause_requested()) {
 *         paused_.store(true,  memory_order_release);
 *         while (pause_requested()) { std::this_thread::yield(); }
 *         paused_.store(false, memory_order_release);
 *     }
 *
 */
#ifndef GXTHREAD_H
#define GXTHREAD_H
// ---------------------------------------------------------------------------
//  Visibilite des symboles de libodenise_thread.
//  ODENISE_THREAD_API : exporte depuis la lib, importe chez le consommateur.
//  Meme patron que ODENISE_API / THREAD dans le reste du projet.
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(__MINGW32__)
    #ifdef THREAD_EXPORTS
        #define THREAD __declspec(dllexport)
    #elif defined(THREAD_IMPORTS)
        #define THREAD __declspec(dllimport)
    #else
        #define THREAD
    #endif
#else
    #ifdef THREAD_EXPORTS
        #define THREAD __attribute__((visibility("default")))
    #else
        #define THREAD
    #endif
#endif
#include "common.h"
#include <atomic>

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
    /* thread loop functions */
    static void* T_Loop (void*);
    static void* T_Loop2(void*);
    /* Commandes de pause -- posées par P_Thread()/P_Thread2() hors RT,
     * lues par Run()/Run2() dans la boucle (RT-safe, zéro mutex). */
    std::atomic<bool> pause_ {false};
    std::atomic<bool> pause2_{false};
protected:
    /* Confirmations de pause -- écrites par Run()/Run2() quand ils entrent
     * et sortent de pause. Lues par P_Thread()/P_Thread2() hors RT. */
    std::atomic<bool> paused_ {false};
    std::atomic<bool> paused2_{false};
    /* function to define in your class, is what the thread do */
    virtual bool Run()  = 0;
    // Run2 est optionnel : à surcharger uniquement si un second thread est nécessaire.
    virtual bool Run2() = 0;
    /* internal start */
    int S_Thread(void* (*f)(void*));
    /* thread start / stop / join / pause / resume */
    int S_Thread();
    int S_Thread2();
    int J_Thread();
    int J_Thread2();
    int T_Thread();
    int T_Thread2();
    int P_Thread();   // pause  thread  (attend confirmation de suspension)
    int R_Thread();   // resume thread
    int P_Thread2();  // pause  thread2
    int R_Thread2();  // resume thread2
    /* internal constructor */
    Thread(void* (*f)(void*)) { S_Thread(f); }
public:
    /* Lecture des flags pause depuis Run() -- RT-safe (load acquire, zéro mutex). */
    bool pause_requested()  const noexcept { return pause_ .load(std::memory_order_acquire); }
    bool pause2_requested() const noexcept { return pause2_.load(std::memory_order_acquire); }
    Thread();
    ~Thread();
};

#else /* _MSC_VER */
/* -------------------------------------------------------------------------
 * Implémentation std::thread (Windows / MSVC)
 * T_Thread()  : pose stop_  = true puis joint thread_.
 * T_Thread2() : pose stop2_ = true puis joint thread2_.
 * Run()/Run2() doivent consulter stop_requested()/stop2_requested() pour
 * terminer proprement (pas de pthread_cancel sous MSVC).
 * ------------------------------------------------------------------------- */
#include <thread>

class Thread {
private:
    std::thread          thread_;
    std::thread          thread2_;
    std::atomic<bool>    stop_ {false};
    std::atomic<bool>    stop2_{false};
    /* Commandes de pause -- posées par P_Thread()/P_Thread2() hors RT,
     * lues par Run()/Run2() dans la boucle (RT-safe, zéro mutex). */
    std::atomic<bool>    pause_ {false};
    std::atomic<bool>    pause2_{false};
    /* fonction arbitraire passée au constructeur interne (peut être nullptr).
     * Sémantique identique à pthread_create(..., f, this) :
     * f reçoit this comme void*, son retour est ignoré. */
    void* (*custom_fn_)(void*) = nullptr;
    /* lance custom_fn_(this) dans thread_ -- appelé par le constructeur interne */
    int S_Thread_fn();
protected:
    /* Confirmations de pause -- écrites par Run()/Run2() quand ils entrent
     * et sortent de pause. Lues par P_Thread()/P_Thread2() hors RT. */
    std::atomic<bool>    paused_ {false};
    std::atomic<bool>    paused2_{false};
    /* function to define in your class, is what the thread do */
    THREAD virtual bool Run()  = 0;
    // Run2 est optionnel : à surcharger uniquement si un second thread est nécessaire.
    THREAD virtual bool Run2() = 0;
    /* thread start / stop / join / pause / resume */
    THREAD int S_Thread();
    THREAD int S_Thread2();
    THREAD int J_Thread();
    THREAD int J_Thread2();
    THREAD int T_Thread();
    THREAD int T_Thread2();
    THREAD int P_Thread();   // pause  thread  (attend confirmation de suspension)
    THREAD int R_Thread();   // resume thread
    THREAD int P_Thread2();  // pause  thread2
    THREAD int R_Thread2();  // resume thread2
    /* internal constructor : lance f(this) dans thread_, comme pthread_create.
     * T_Thread() pose stop_ = true puis joint -- f doit consulter
     * stop_requested() pour terminer proprement. */
    THREAD Thread(void* (*f)(void*)) : custom_fn_(f) { S_Thread_fn(); }
public:
    /* Lecture des flags depuis Run() -- RT-safe (load acquire, zéro mutex).
     * stop_requested()  : Run() doit retourner false pour terminer.
     * pause_requested() : Run() doit entrer en pause (pose paused_=true,
     *                     yield en boucle, repose paused_=false à la reprise). */
    THREAD bool stop_requested()   const noexcept { return stop_  .load(std::memory_order_acquire); }
    THREAD bool stop2_requested()  const noexcept { return stop2_ .load(std::memory_order_acquire); }
    THREAD bool pause_requested()  const noexcept { return pause_ .load(std::memory_order_acquire); }
    THREAD bool pause2_requested() const noexcept { return pause2_.load(std::memory_order_acquire); }
    THREAD Thread();
    THREAD ~Thread();
};

#endif /* _MSC_VER */

#endif /* GXTHREAD_H */

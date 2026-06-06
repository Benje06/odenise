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

Thread::Thread() {} ;
Thread::~Thread() {} ;

void* Thread::T_Loop(void * thread) {
    Thread * iThread = (Thread *)thread;
    while(iThread->Run()){};
    return 0;
};

void* Thread::T_Loop2(void * thread) {
    Thread * iThread = (Thread *)thread;
    while(iThread->Run2()){};
    return 0;
};

int Thread::S_Thread(void* (*f) (void*)) {
    if ( (err = pthread_create( &thread, nullptr, f, (void *)this)) ){
        std::string msg_err = error(__func__,
            _("cannot create thread"),
            std::to_string(err));
        LOG_ERR(msg_err);
        return err;
    } else {
        LOG("Thread: start");
    }
    return 0;
}

int Thread::S_Thread() { return S_Thread(&T_Loop); }
int Thread::S_Thread2() { return S_Thread(&T_Loop2); }

int Thread::J_Thread() {
    LOG(LOG_IN());
    pthread_join(thread, nullptr);
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

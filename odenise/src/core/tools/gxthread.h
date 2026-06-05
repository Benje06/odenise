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
 */
#ifndef GXTHREAD_H
    #define GXTHREAD_H
    #include <pthread.h>
    #include "common.h"
    #include "lang.h" // for t_fatal
 
class Thread {
    private:
        int err;
        pthread_t thread;
        pthread_t thread2;
        /* thread loop function */
        static void* T_Loop(void*);
        static void* T_Loop2(void*);
    protected:
        /* function to define in your class, is what the thread do */
        virtual bool Run() = 0;
        virtual bool Run2() = 0;
        /* internal start */
        virtual int S_Thread(void* (*f) (void*));
        /* thread start */
        int S_Thread();
        int S_Thread2();
        int J_Thread();
        int T_Thread();
        /* internal constructor */
        Thread(void* (*f) (void*)) { S_Thread(f); };
    public:
        /* constructor */
        Thread();
        ~Thread();
};
#endif /* GXTHREAD_H */

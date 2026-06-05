/* ----------------------------------------------------------------------------
 * tools/lang.h -- 
 * Lang Header
 *      
 * ----------------------------------------------------------------------------
 * copyright © 2006, 2007, 2008, 2009, 2010 Jérôme BENHAÏM <benhaimjerome@gmail.com>
 *
 * ----------------------------------------------------------------------------
 *
 *   This program is free software: you can redistribute it and/or modify
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
 */
/* Constant lang for C messages */
/* TODO : using gettext  */

#if !defined(LANG_H)
    #define LANG_H
    #if !defined(LANG)
        #define LANG "fr"
        #define t_fatal "FATALE"
        #define t_crit "CRITIQUE"
        #define t_warn "AVERTISEMENT"
        #define t_info "INFORMATION"
        #define t_err "ERREUR"
        #define t_msg "MESSAGE"

        #define msgcommand "Programme:"
        #define msgparam "Argument invalide:"
        #define msghelp "Il manque un argument pour le mode client: \n Options attendues:\t-c IP \n Options reçus:\t\t"
        #define msgthreadcreate "ne peux CRÉER le thread"
        #define msgthreadjoin "ne peux JOINDRE le thread"
        #define msgthreadquit "ne peux QUITTER le thread"
        #define msginitsocket "ne peux INITIALISER le socket"
        #define msgwritesocket "Ne peux ECRIRE sur le socket"
        #define msgreadsocket "Ne peux LIRE sur le socket"
    #endif
#endif

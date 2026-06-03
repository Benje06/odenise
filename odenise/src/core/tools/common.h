/* ----------------------------------------------------------------------------
 * Common C++ tools Header
 *
 * ----------------------------------------------------------------------------
 * copyright © 2006, 2007, 2008, 2009, 2010Jérôme BENHAÏM <benhaimjerome@gmail.com>
 *
 * ----------------------------------------------------------------------------
 *
 *   This file is part of GxInterface.
 *
 *	 GxInterface  is free software: you can redistribute it and/or modify
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

 /* 
  * Derive des outils communs de gxinterface, nettoye pour le coeur reutilisable :
  *
  * Fournit : DS / EOL, tostr<T>, str_to_ascii, str_const_hash + ""_hash,
  *           get_time(), error(from,what,why), help_options/help_format, init_nls().Define by Operating System : 
  * 	- include
  * 	- constant :
  * 		DS		Directory Separator
  * 		EOL		End Of Line
  * 	- generic function :
  * 		tostr<T_>(V_) convert to string a variable V_ of type T_
  *         constexpr std::size_t str_const_hash(const char* str) convert str to a hash
  *         constexpr std::size_t operator""_hash(const char* str, std::size_t) HASH operator for switch case use
  */
#pragma once
#ifndef interface_COMMON_H
    #define interface_COMMON_H

    #ifdef HAVE_CONFIG_H
        #include <config.h>
    #endif

    #include "debug.h"

    #include <iostream>
    #include <filesystem>
    #include <regex>
    #include <chrono>
    #include <ctime>
    #include <iomanip>
    #include <sstream>
    #include <string>
    #include <vector>
    #include <getopt.h>

    #if defined(_WIN32) || defined(__MINGW32__)
        #define WIN32_LEAN_AND_MEAN
        #include <windows.h>
        #undef ERROR
        #undef IN
        #undef OUT
        #undef near
        #define DS "\\"
        #undef EOL
        #define EOL "\r\n"
        #include <system_error>
        #include <locale>
        #include <cstdlib>
    #else
        #define EOL ("\n")
        #define DS  ("/")
    #endif

    /*** i18n (gettext pur, sans glibmm) ***/
    #ifdef ENABLE_NLS
        #include <libintl.h>
        #include <locale>
        #ifndef _
            #define _(String)  gettext(String)
        #endif
        #ifndef N_
            #define N_(String) (String)
        #endif
    #else
        #ifndef _
            #define _(String)  (String)
        #endif
        #ifndef N_
            #define N_(String) (String)
        #endif
    #endif

	/*** CONSTANTS ***/
	// for action_type
	#define ACT_OPEN 1
	#define ACT_SAVE 2
	#define ACT_INSERT 3 
	#define ACT_DELETE 4
	#define ACT_REPLACE 5
	#define ACT_MOOVE 6
	#define ACT_WARNING 7

    /*** String Convert ***/
    /* convert to string any type of number */
    template <class paramType>
    std::string tostr(paramType val) {
        std::ostringstream strm;
        strm << val;
        return strm.str();
    };
    /* keep only 7-bit ASCII, replace the rest with '?' */
    inline std::string str_to_ascii(const std::string& input) {
        std::string result;
        for( unsigned char ch : input ){
            if( ch < 128 ){
                result += static_cast<char>(ch);
            } else {
                result += '?';
            };
        };
        return result;
    };
    /* compile-time hash (ex: switch/case sur des chaines) */
    constexpr std::size_t str_const_hash(const char* str) {
        std::size_t h = 0;
        for (; *str; ++str) {
            h = h * 31 + static_cast<std::size_t>(*str);
        };
        return h;
    };
    constexpr std::size_t operator""_hash(const char* str, std::size_t) {
        return str_const_hash(str);
    };
    /* horodatage thread-safe "YYYY-MM-DD HH:MM:SS" */
    inline std::string get_time(){
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buffer{};
        #if defined(_WIN32) || defined(__MINGW32__)
            localtime_s(&tm_buffer, &now_time);
        #else
            localtime_r(&now_time, &tm_buffer);
        #endif
        std::ostringstream ostr;
        ostr << std::put_time(&tm_buffer, "%Y-%m-%d %H:%M:%S");
        return ostr.str();
    };
    /* message d'erreur standard : From / What / Reason */
    inline std::string error(const std::string& from, const std::string& what, const std::string& why){
        std::string msg = std::string(_("From: ")) + from + "\n\t"
                        + what + "\n\t"
                        + std::string(_("Reason => ")) + why;
        return msg;
    };

    /*** HELP FORMATTING ***/
    /* One option line in a --help output.
     *   short_opt   : single short letter, or 0 if no short form
     *   long_opt    : long form without the leading "--"
     *   arg_name    : argument placeholder (e.g. "CHAR"), empty if no argument
     *   description : one entry per logical line; each entry is passed
     *                 separately to gettext at the call site, so no '\n' is
     *                 ever embedded inside a translatable string.
     */
    struct help_options {
        char                     short_opt;
        std::string              long_opt;
        std::string              arg_name;
        std::vector<std::string> description;
    };
    /* Format a --help message in GNU style.
     * Layout:
     *   - description column is fixed at 24
     *   - if the option form is <= 23 chars: description on the same line
     *   - if >= 24 chars: option alone, description on the next line
     *   - additional description entries: indented to column 24
     * The header (Usage / Options:) is built here so callers don't repeat it.
     */
    inline std::string help_format( const std::string& prog_name,
                                    const std::vector<help_options>& options){
        constexpr std::size_t COL = 24;
        const std::string indent(COL, ' ');
        static bool header_done = false;
        std::string msg;
        if (!header_done) {
            msg = std::string(_("Usage: ")) + prog_name + _(" [OPTIONS]") + "\n\n"
                + _("Options:") + "\n";
            header_done = true;
        }
        for (const auto& o : options){
            std::string form = "  -";
                        form += o.short_opt;
                        form += ", --";
                        form += o.long_opt;
            if (!o.arg_name.empty()){
                form += "=" + o.arg_name;
            }
            if (form.size() < COL){
                form.append(COL - form.size(), ' ');
            }else{
                form += "\n" + indent;
            }
            msg += form;
            for (std::size_t i = 0; i < o.description.size(); ++i){
                if (i > 0){
                    msg += indent;
                }
                msg += o.description[i] + "\n";
            }
        }
        return msg;
    }

    /*** INIT NLS ***/
    inline void init_nls(){
        #ifdef ENABLE_NLS
            setlocale(LC_ALL, "");
            try { std::locale::global(std::locale("")); } catch (...) { /* locale absente : ignore */ }
            #ifdef GETTEXT_PACKAGE
                textdomain(GETTEXT_PACKAGE);
                #ifdef LOCALEDIR
                    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
                #endif
                bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
            #endif
        #endif
    }

    #include "logger.h"
#endif /* interface_COMMON_H */

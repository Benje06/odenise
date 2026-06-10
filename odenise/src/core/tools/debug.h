/* ----------------------------------------------------------------------------
 * Debug macros Header
 *
 * ----------------------------------------------------------------------------
 * copyright © 2006, 2007, 2008, 2009, 2010 Jérôme BENHAÏM <benhaimjerome@gmail.com>,
 *					  Mirsal ENNAIME <mirsal.ennaime@gmail.com>
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
 */
#pragma once
#ifndef _TOOLS_DEBUG_H
    #define _TOOLS_DEBUG_H
    #ifdef __cplusplus
    #ifdef _MSC_VER
        #define CURRENT_FUNCTION __FUNCSIG__
    #elif defined(__GNUC__)
        #define CURRENT_FUNCTION __PRETTY_FUNCTION__
    #else
        #define CURRENT_FUNCTION __func__
    #endif
    #if defined(DEBUG_TOOL) || defined(DEBUG_TOOL_AS_ERROR)
        #   define PRE_LOG(str1, str2, ...)    (std::string(str1) + std::string("-> ") + std::string(str2))
        #   define POST_LOG(str1, str2, ...)   (std::string(str1) + std::string(" <-") + std::string(str2))
        #   define TRACE(f, ...)   f "\n", ##__VA_ARGS__
        #   ifdef DEBUG_TOOL_AS_ERROR
        #       define LOG_IN()  (std::string(" -> ") + CURRENT_FUNCTION + std::string(" line ") + std::to_string(__LINE__) + std::string(" of ") + __FILE__)
        #       define LOG_OUT() (std::string("<-  ") + CURRENT_FUNCTION)
        #   else
        #       define LOG_IN()  (std::string(" -> ") + CURRENT_FUNCTION)
        #       define LOG_OUT() (std::string("<-  ") + CURRENT_FUNCTION)
        #   endif /* DEBUG_TOOL_AS_ERROR */
        #else  /* NOT DEBUG_TOOL */
        #   define PRE_LOG(str1, str2)  (void)(0)
        #   define POST_LOG(str1, str2) (void)(0)
        #   define LOG_IN()             (void)(0)
        #   define LOG_OUT()            (void)(0)
        #   define TRACE(f, ...)        (void)(0)
        #endif /* DEBUG_TOOL */
    #else
        #include <stdlib.h>
        #include <stdio.h>
        #ifdef DEBUG_TOOL
        #   define PRE_LOG(str1, str2, ...)  fprintf(stderr, "%s->%s\n", str1, str2, ##__VA_ARGS__)
        #   define POST_LOG(str1, str2, ...) fprintf(stderr, "%s<-%s\n", str1, str2, ##__VA_ARGS__)
        #   ifdef DEBUG_TOOL_AS_ERROR
        #       define LOG_IN()        fprintf ( stderr, " -> %s line %i of %s \n", __PRETTY_FUNCTION__, __LINE__ , __FILE__)
        #       define LOG_OUT()       fprintf ( stderr, "<-  %s\n", __PRETTY_FUNCTION__ )
        #       define TRACE(f, ...)   fprintf ( stderr, f "\n", ##__VA_ARGS__ )
        #   else /* NOT DEBUG_TOOL_AS_ERROR */
        #       define LOG_IN()        printf ( " -> %s\n", __func__ )
        #       define LOG_OUT()       printf ( "<-  %s\n", __func__ )
        #       define TRACE(f, ...)   printf ( f "\n", ##__VA_ARGS__ )
        #   endif /* DEBUG_TOOL_AS_ERROR */
        #else  /* NOT DEBUG_TOOL */
        #   define PRE_LOG(str1, str2)  (void)(0)
        #   define POST_LOG(str1, str2) (void)(0)
        #   define LOG_IN()             (void)(0)
        #   define LOG_OUT()            (void)(0)
        #   define TRACE(f, ...)        (void)(0)
        #endif /* DEBUG_TOOL */
        #define print_error(str, ...)  fprintf(stderr, (str "\n"), ##__VA_ARGS__)
    #endif
#endif /* _TOOLS_DEBUG_H */

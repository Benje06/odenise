/* ----------------------------------------------------------------------------
 * Logger / LogManager (autonome).
 * Singleton thread-safe a handlers multiples. Niveaux :
 *   0 = desactive, 1 = fichier seul, 2 = fichier + console.
 * Macros : LOG(msg) / LOG_ERR(msg).
 * ----------------------------------------------------------------------------
 */
#pragma once
#ifndef LOGGER_H
    #define LOGGER_H
    #if defined(_WIN32) || defined(__MINGW32__)
        #ifdef LOGGER_EXPORTS
            #define LOGGER __declspec(dllexport)
        #elif defined(LOGGER_IMPORTS)
            #define LOGGER __declspec(dllimport)
        #else
            #define LOGGER
        #endif
    #else
        #ifdef LOGGER_EXPORTS
            #define LOGGER __attribute__((visibility("default")))
        #else
            #define LOGGER
        #endif
    #endif

    #include "common.h"
    #include <fstream>
    #include <memory>
    #include <mutex>

    class Logger {
        private:
            std::mutex    log_mutex;
            std::ofstream log_file;
            unsigned int  log_level = 2;

        public:
            void set_log_level(unsigned int);

            Logger(const std::string& filename, unsigned int level)
                : log_file(filename, std::ios::app),
                  log_level(level){};
            void log(const std::string&);
            void log_error(const std::string&);
            void flush();
    };

    class LogManager {
    public:
        LOGGER static LogManager& instance();
        LOGGER void add_handler(std::shared_ptr<Logger>);
        LOGGER void log(const std::string&);
        LOGGER void log_error(const std::string&);
        LOGGER void set_log_level(unsigned int);
        LOGGER void add_debug_handler();
    private:
        bool debug_handler_active_ = false;
        std::string                          msg;
        std::mutex                           manager_mutex;
        std::vector<std::shared_ptr<Logger>> handlers;
        LogManager();
        ~LogManager();
        LogManager(const LogManager&)=delete;
        LogManager& operator=(const LogManager&)=delete;
    };

    #define LOG(message)     LogManager::instance().log(message)
    #define LOG_ERR(message) LogManager::instance().log_error(message)

#endif /* LOGGER_H */

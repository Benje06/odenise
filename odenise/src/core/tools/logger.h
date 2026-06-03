#pragma once
#ifndef LOGGER_H
    #define LOGGER_H
    #if defined(__WIN32) || defined(__MINGW32__)
        #ifdef GXLOG_EXPORTS
            #define GXLOG __declspec(dllexport)
        #elif defined(GXLOG_IMPORTS)
            #define GXLOG __declspec(dllimport)
        #else
            #define GXLOG
        #endif
    #else
        #ifdef GXLOG_EXPORTS
            #define GXLOG __attribute__((visibility("default")))
        #else
            #define GXLOG
        #endif
    #endif

    #include "common.h"
    #include <fstream>
    #include <memory>
    #include <mutex>
    class Logger {
        private:
            std::mutex log_mutex;
            std::ofstream log_file;
            unsigned int log_level=2;

        public:
            void set_log_level(unsigned int);

            Logger(const std::string& filename, unsigned int log_level) 
                : log_file(filename, std::ios::app),
                  log_level(log_level){};
            void log(const std::string&);
            void log_error(const std::string&);
            void flush();          
    };

    class GXLOG LogManager {
    public:
        GXLOG static LogManager& instance();
        GXLOG void add_handler(std::shared_ptr<Logger>);
        GXLOG void log(const std::string&);
        GXLOG void log_error(const std::string&);
        GXLOG void set_log_level(unsigned int);
    private:
        std::string msg;
        std::mutex manager_mutex;
        std::vector<std::shared_ptr<Logger>> handlers;
        LogManager();
        ~LogManager();
        LogManager(const LogManager&)=delete;
        LogManager& operator=(const LogManager&)=delete;
    };

#define LOG(message) LogManager::instance().log(message)
#define LOG_ERR(message) LogManager::instance().log_error(message)

#endif
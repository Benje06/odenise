#include "logger.h"

/*** LOGGER ***/
void Logger::set_log_level(unsigned int lvl){
    std::lock_guard<std::mutex> lock(log_mutex);
    log_level = lvl;
};
void Logger::log(const std::string& msg){
    std::lock_guard<std::mutex> lock(log_mutex);
    if( log_level == 0){
        return;
    }else if( log_level >= 1){    
        if( log_file.is_open() ){
            log_file << msg << std::endl;
        };
        if( log_level >= 2){
            std::cout << msg << std::endl;
        };
    }
};
void Logger::log_error(const std::string& err_msg){
    std::lock_guard<std::mutex> lock(log_mutex);
    if( log_file.is_open() ){
        log_file << "            " << std::endl;
        log_file << "*************************** " << _("ERROR") << " ***************************** " << std::endl;
        log_file << err_msg << std::endl;
        log_file << "*************************************************************** " << std::endl;
        log_file << "            " << std::endl;
    };
    if( log_level >= 2){
        std::cerr <<  "*** " << _("ERROR") << " *** " << err_msg << std::endl;
    };
};
void Logger::flush(){
    std::lock_guard<std::mutex> lock(log_mutex);
    if (log_file.is_open()){
        log_file.flush();
    };
    std::cout.flush();
};

/*** LOG MANAGER ***/
LogManager::LogManager() = default;
LogManager::~LogManager() = default;
LogManager& LogManager::instance(){
    static LogManager instance;
    return instance;
};
void LogManager::add_handler(std::shared_ptr<Logger> handler){
    std::lock_guard<std::mutex> lock(manager_mutex);
    handlers.push_back(handler);
};
void LogManager::set_log_level(unsigned int log_level){
    if( log_level <= 2){
        {
            std::lock_guard<std::mutex> lock(manager_mutex);
            msg = _("Switching to log level: ") + std::to_string(log_level);
            for (auto& handler : handlers) {
                handler->set_log_level(log_level);
                handler->log( msg );
            };
        };
    }else{
        std::string lvlmsg = _("Log Level: ") + std::to_string(log_level) + _(" is not a valid value, shoudl be between 0(disable), 1(log file) and 2(log file + console).");
        log_error( lvlmsg );
    };
};
void LogManager::log(const std::string& message){
    std::lock_guard<std::mutex> lock(manager_mutex);
    for (auto& handler : handlers) {
        handler->log(message);
        handler->flush();
    };
};
void LogManager::log_error(const std::string& message){
    std::lock_guard<std::mutex> lock(manager_mutex);
    for (auto& handler : handlers) {
        handler->log_error(message);
        handler->flush();
    };
};

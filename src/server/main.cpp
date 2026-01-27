#include "config/config.h"
#include "common/logger.h"

int main(){
    try{
        std::string config_path="configs/config.yaml";
        user_service::Config config=user_service::Config::LoadFromFile(config_path);
        user_service::Logger::Init(config.log.path,config.log.filename,config.log.level,config.log.max_size,config.log.max_files,config.log.console_output);
        LOG_DEBUG(config.ToString());
    }catch(const std::exception& e){
        LOG_ERROR(e.what());
        return 1;
    }
    return 0;
}
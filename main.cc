#include "log/logger.h"

using namespace logcpp;
int main()
{
    Options options;
    options.path = "/dev/shm/";
    options.log_file_name_prefix = "LOG_";
    options.max_byte_size = MAX_LOG_BYTE_SIZE;
    options.append_to_console = false;
    Logger::SetOptions(options);
    Logger::SetLevel(Level::INFO);
    LOG_INIT();
    string content = "Hello world!";
    uint64_t i = 0;
    while (true)
    {
        Status status = LOG_INFO(to_string(i) + content);
        if (status != Status::OK)
        {
            printf("log error,status=%d\n",status);
        }
        ++i;
        if(i>= 20000000){
            break;
        }
    }
    printf("finished!\n");
    return 0;
}
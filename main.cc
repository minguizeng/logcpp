#include "log/logger.h"

using namespace logcpp;
int main()
{
    Options options;
    options.path = "./";
    options.log_file_name_prefix = "LOG_";
    options.sync = false;
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
    }
    return 0;
}
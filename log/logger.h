#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <ctime>
#include <chrono>
#include <iostream>

#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>

#define LOG_DEBUG(content) logcpp::Logger::Debug(content, __FILE__, __LINE__);
#define LOG_INFO(content) logcpp::Logger::Info(content, __FILE__, __LINE__);
#define LOG_WARN(content) logcpp::Logger::Warn(content, __FILE__, __LINE__);
#define LOG_ERR(content) logcpp::Logger::Error(content, __FILE__, __LINE__);
#define LOG_INIT() logcpp::Logger::StartUp();
#define LOG_SHUTDOWN() logcpp::Logger::Shutdown();

namespace logcpp
{
#define MAX_LOG_BYTE_SIZE 1 * 1024 * 1024 * 1024
#define MIN_LOG_BYTE_SIZE 1 * 1024 * 1024
#define MAX_LOG_QUEUE_SIZE 1000000

    using namespace std;

    template <class T>
    class UnboundedQueue
    {
    private:
        deque<T> data_;
        mutex mtx_;
        condition_variable cv_;

    public:
        UnboundedQueue(){};
        UnboundedQueue(const UnboundedQueue &) = delete;
        UnboundedQueue &operator=(UnboundedQueue &) = delete;
        ~UnboundedQueue(){};

        bool PushBack(const T &item)
        {
            unique_lock<mutex> lock(mtx_);
            data_.emplace_back(item);
            cv_.notify_one();
            return true;
        }
        bool PushHead(const T &item)
        {
            unique_lock<mutex> lock(mtx_);
            data_.emplace_front(item);
            cv_.notify_one();
            return true;
        }
        T Take()
        {
            unique_lock<mutex> lock(mtx_);
            cv_.wait(lock, [this]
                     { return !data_.empty(); });
            auto item = data_.front();
            data_.pop_front();
            return item;
        }
        uint64_t Size()
        {
            unique_lock<mutex> lock(mtx_);
            return data_.size();
        }
        bool TakeAll(deque<T> &result)
        {
            unique_lock<mutex> lock(mtx_);
            cv_.wait(lock, [this]
                     { return !data_.empty(); });
            auto before = data_.size();
            data_.swap(result);
            return true;
        }
    };

    enum Level
    {
        DEBUG = -1,
        INFO = 0,
        WARN = 1,
        ERROR = 2
    };

    enum Status
    {
        OK = 0,
        LOG_FULL = 1,
        LOG_FILE_NOT_EXIST = 2,
        LOG_WRITE_ERROR = 3,
        LOG_BUSY = 4
    };

    struct Options
    {
        string path;
        string log_file_name_prefix;
        uint64_t max_byte_size;
        bool append_to_console;
    };

    class LoggerFile
    {
    private:
        FILE *file_;
        uint64_t max_byte_size_;
        uint64_t current_byte_size_;
        string file_name_;

    public:
        LoggerFile(const string &file_name, uint64_t max_byte_size)
            : file_name_(file_name), max_byte_size_(max_byte_size), current_byte_size_(0)
        {
            file_ = fopen(file_name_.c_str(), "a");
        }
        LoggerFile(const LoggerFile &) = delete;
        LoggerFile &operator=(const LoggerFile &) = delete;
        ~LoggerFile()
        {
            if (file_)
            {
                fflush(file_);
                fclose(file_);
            }
        }

        Status Append(const string &content)
        {
            uint64_t content_size = content.size();
            if (content_size + current_byte_size_ > max_byte_size_)
            {
                return Status::LOG_FULL;
            }
            if (!file_)
            {
                return Status::LOG_FILE_NOT_EXIST;
            }

            uint64_t write_size = fwrite(content.data(), 1, content_size, file_);
            if (write_size != content_size)
            {
                return Status::LOG_WRITE_ERROR;
            }
            current_byte_size_ += write_size;
            fflush(file_);
            return Status::OK;
        }
        string &Name()
        {
            return file_name_;
        }
    };

    class Logger
    {
    private:
        shared_ptr<LoggerFile> logger_file_;
        Options options_;
        Level level_;
        bool exit_;
        uint64_t sequence_number_;
        UnboundedQueue<string> log_queue_;
        thread write_thread_;

    public:
        Logger()
            : sequence_number_(0)
        {
            options_.log_file_name_prefix = "LOG_";
            options_.max_byte_size = MAX_LOG_BYTE_SIZE;
            options_.path = "./";
            options_.append_to_console = false;
            level_ = Level::INFO;
            exit_ = false;
        }
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;
        ~Logger()
        {
            exit_ = true;
            if (write_thread_.joinable())
            {
                write_thread_.join();
            }
        }

        static void SetLevel(Level level)
        {
            Logger::Instance()->SetLevel0(level);
        }
        static void SetOptions(Options &options)
        {
            Logger::Instance()->SetOptions0(options);
        }
        static void StartUp()
        {
            Logger::Instance()->StartUp0();
        }
        static void Shutdown()
        {
            Logger::Instance()->Shutdown0();
        }
        static Status Debug(const string &content, const char *file_name, int line)
        {
            return Logger::Instance()->Log(Level::DEBUG, content, file_name, line);
        }
        static Status Info(const string &content, const char *file_name, int line)
        {
            return Logger::Instance()->Log(Level::INFO, content, file_name, line);
        }
        static Status Warn(const string &content, const char *file_name, int line)
        {
            return Logger::Instance()->Log(Level::WARN, content, file_name, line);
        }
        static Status Error(const string &content, const char *file_name, int line)
        {
            return Logger::Instance()->Log(Level::ERROR, content, file_name, line);
        }

    private:
        static Logger *Instance()
        {
            static unique_ptr<Logger> logger;
            static mutex mtx;
            unique_lock<mutex> lock(mtx);
            if (logger.get() == nullptr)
            {
                logger.reset(new Logger());
            }
            return logger.get();
        }
        void WriteLog(const string &content)
        {
            if (logger_file_.get() == nullptr)
            {
                CreateLogFile();
            }
            int try_count = 3;
            while (true)
            {
                if (try_count < 1)
                {
                    break;
                }
                Status status = logger_file_->Append(content);
                if (status == Status::LOG_FULL || status == Status::LOG_FILE_NOT_EXIST)
                {
                    string new_file_name = logger_file_->Name() + "." + to_string(sequence_number_);
                    if (!rename(logger_file_->Name().c_str(), new_file_name.c_str()))
                    {
                        sequence_number_++;
                        CreateLogFile();
                        continue;
                    }
                    --try_count;
                }
                if (status != Status::OK)
                {
                    log_queue_.PushHead(content);
                }
                break;
            }
            if (options_.append_to_console)
            {
                cout << content;
            }
        }
        void CreateLogFile()
        {
            char buffer[256];
            auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
            time_t timer = std::chrono::system_clock::to_time_t(now);
            char time_buf[64];
            int time_str_len = strftime(time_buf, 64, "%Y%m%d-%H%M%S", localtime(&timer));
            string time(time_buf, time_str_len);

            int len = sprintf(buffer, "%s%s%d_%s.log", options_.path.c_str(), options_.log_file_name_prefix.c_str(), getpid(), time.c_str());
            string log_file_name(buffer, len);
            logger_file_ = make_shared<LoggerFile>(log_file_name, options_.max_byte_size);
        }
        void SetLevel0(Level level)
        {
            level_ = level;
        }
        void SetOptions0(Options &options)
        {
            options_.path = options.path == "" ? "./" : options.path;
            options_.log_file_name_prefix = options.log_file_name_prefix == "" ? "LOG_" : options.log_file_name_prefix;
            options_.max_byte_size = options.max_byte_size <= MIN_LOG_BYTE_SIZE ? MAX_LOG_BYTE_SIZE : options.max_byte_size;
            options_.append_to_console = options.append_to_console;
        }
        void StartUp0()
        {
            write_thread_ = thread([this]()
                                   {
            while(!exit_){
                deque<string> result;
                log_queue_.TakeAll(result);
                string content;
                for (auto item : result)
                {
                    content += item;
                }
                WriteLog(content);
                this_thread::sleep_for(chrono::milliseconds(10));
            } });
        }
        void Shutdown0()
        {
            exit_ = true;
        }
        Status Log(Level level, const string &content, const char *file_name, int line)
        {
            if (level < level_)
            {
                return Status::OK;
            }
            if (log_queue_.Size() > MAX_LOG_QUEUE_SIZE)
            {
                return Status::LOG_BUSY;
            }
            string buffer;
            buffer.resize(128 + content.size());
            auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
            int millisecond = now.time_since_epoch().count() % 1000;
            time_t timer = std::chrono::system_clock::to_time_t(now);
            char time_buf[64];
            int time_str_len = strftime(time_buf, 64, "%Y-%m-%d %H:%M:%S", localtime(&timer));
            std::sprintf(time_buf + time_str_len, ".%03u", millisecond);
            std::string real_name = "";
            std::string file_path(file_name);
            int pos = file_path.find_last_of('/');
            if (pos == file_path.npos)
            {
                real_name = file_name;
            }
            else
            {
                real_name = file_path.substr(pos + 1);
            }
            string level_str = "";
            switch (level)
            {
            case DEBUG:
                level_str = "DEBUG";
                break;
            case INFO:
                level_str = "INFO";
                break;
            case WARN:
                level_str = "WARN";
                break;
            case ERROR:
                level_str = "ERR";
                break;
            }
            thread_local static pid_t tid = syscall(SYS_gettid);
            int log_len = std::sprintf((char *)buffer.data(), "[%s] %ld %s %s:%d %s\n", time_buf, tid, level_str.c_str(), real_name.c_str(), line,
                                       content.c_str());
            buffer.resize(log_len);
            if (log_len <= 0 || !log_queue_.PushBack(buffer))
            {
                return Status::LOG_WRITE_ERROR;
            }
            return Status::OK;
        }
    };
};

#endif
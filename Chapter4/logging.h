#pragma once

#include <string>
#include <fstream>
#include <cstdio>

#include "types.h"
#include "macros.h"
#include "lf_queue.h"
#include "thread_utils.h"

namespace Common {
  class Logger final {
  public:
    auto flushQueue() noexcept {
      while (true) {
        auto next = queue_.getNext();
        while (next) {
          file_ << *next;
          queue_.pop();
          next = queue_.getNext();
        }

        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(1ms);
      }
    }

    explicit Logger(const std::string &file_name)
        : file_name_(file_name), queue_(LOG_QUEUE_SIZE) {
      file_.open(file_name);
      ASSERT(file_.is_open(), "Could not open log file:" + file_name);
      ASSERT(createAndStartThread(-1, "Common/Logger", [this]() { flushQueue(); }) != nullptr, "Failed to start Logger thread.");
    }

    ~Logger() {
      std::cerr << "Flushing and closing Logger for " << file_name_ << std::endl;

      while (!queue_.empty()) {
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(1s);
      }

      file_.close();
    }

    // note that this is overloading not specialization. gcc does not allow inline specializations.
    auto pushValue(const char value) noexcept {
      queue_.push(value);
    }

    // note that this is overloading not specialization. gcc does not allow inline specializations.
    auto pushValue(const char *value) noexcept {
      while (*value) {
        pushValue(*value);
        ++value;
      }
    }

    // note that this is overloading not specialization. gcc does not allow inline specializations.
    auto pushValue(const std::string &value) noexcept {
      pushValue(value.c_str());
    }

    template<typename T>
    auto pushValue(const T &value) noexcept {
      pushValue(std::to_string(value));
    }

    template<typename T, typename... Args>
    auto log(const char *s, T &value, Args... args) noexcept {
      while (*s) {
        if (*s == '%') {
          if (*(s + 1) == '%') {
            ++s;
          } else {
            pushValue(value);
            log(s + 1, args...); // call even when *s == 0 to detect extra arguments
            return;
          }
        }
        pushValue(*s++);
      }
      FATAL("extra arguments provided to log()");
    }

    // note that this is overloading not specialization. gcc does not allow inline specializations.
    auto log(const char *s) noexcept {
      while (*s) {
        if (*s == '%') {
          if (*(s + 1) == '%') {
            ++s;
          } else {
            FATAL("invalid format string: missing arguments in log()");
          }
        }
        pushValue(*s++);
      }
    }

    static void setDefaultLogger(Logger *logger) noexcept;

    static Logger &getDefaultLogger() noexcept;

  private:
    const std::string file_name_;
    std::ofstream file_;

    LFQueue<char> queue_;

    static Logger *default_logger_;
  };
}

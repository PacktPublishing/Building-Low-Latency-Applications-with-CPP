#include "common/logging.h"

namespace Common {
  Logger *Logger::default_logger_ = nullptr;

  void Logger::setDefaultLogger(Logger *logger) noexcept {
    default_logger_ = logger;
  }

  Logger &Logger::getDefaultLogger() noexcept {
    return *default_logger_;
  }
}
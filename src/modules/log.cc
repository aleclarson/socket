module; // global
#include <socket/platform.hh>
#include <iostream>
#include <string>

#include "../core/config.hh"
#include "../core/headers.hh"
#include "../core/ipc.hh"
#include "../core/javascript.hh"
#include "../core/json.hh"
#include "../core/string.hh"

/**
 * @module ssc.log
 * @description Logging functions for stdout/stderr
 * @example
 * import ssc.log;
 * namespace ssc {
 *   log::info("logged to stdout");
 *   log::error("logged to stderr");
 *   log::info(123);
 *   log::info(true);
 *   log::info(nullptr);
 *   log::info(JSON::Object::Entries {
 *     {"key", "value"}
 *   })
 * }
 */
export module ssc.log;

using namespace ssc::core;

using ssc::core::config::Config;
using ssc::core::headers::Headers;
using ssc::core::javascript::Script;
using ssc::core::string::String;
using ssc::core::string::StringStream;

export namespace ssc::log {
  using core::string::format;

  inline auto write (const String& str, bool isError) {
    #if defined(_WIN32)
      StringStream ss;
      ss << str << std::endl;
      auto lineStr = ss.str();

      auto handle = isError ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
      WriteConsoleA(GetStdHandle(handle), lineStr.c_str(), lineStr.size(), NULL, NULL);
    #else
      (isError ? std::cerr : std::cout) << str << std::endl;
    #endif
  }

  inline auto info (const String& string) {
    write(string, false);
  }

  inline auto info (std::nullptr_t _) {
    write(String("null"), false);
  }

  inline auto info (const uint64_t u64) {
    write(std::to_string(u64), false);
  }

  inline auto info (const int64_t i64) {
    write(std::to_string(i64), false);
  }

  inline auto info (const int32_t i32) {
    write(std::to_string(i32), false);
  }

  inline auto info (const double f64) {
    write(std::to_string(f64), false);
  }

  inline auto info (const float f32) {
    write(JSON::Number(f32).str(), false);
  }

  inline auto info (const char* string) {
    write(String(string), false);
  }

  inline auto info (const JSON::Any& json) {
    if (json.isString()) {
      info(json.as<JSON::String>().value());
    } else {
      info(json.str());
    }
  }

  inline auto info (const Config& config) {
    info(config.str());
  }

  inline auto info (const Script& script) {
    info(script.str());
  }

  inline auto info (const Headers& headers) {
    info(headers.str());
  }

  inline auto info (const Headers::Entries& entries) {
    info(Headers(entries).str());
  }

  inline auto info (const ipc::Result& result) {
    info(result.str());
  }

  inline auto info (const ipc::Message& message) {
    info(message.str());
  }

  inline auto info (const StringStream& stream) {
    info(stream.str());
  }

  inline auto info (bool boolean) {
    info(boolean ? "true" : "false");
  }

  template <typename ...Args> auto info (const String& fmt, Args... args) {
    info(format(fmt, args...));
  }

  inline auto error (const String& string) {
    write(string, true);
  }

  inline auto error (std::nullptr_t) {
    write(String("null"), true);
  }

  inline auto error (const int64_t i64) {
    write(std::to_string(i64), true);
  }

  inline auto error (const int32_t i32) {
    write(std::to_string(i32), true);
  }

  inline auto error (const double f64) {
    write(JSON::Number(f64).str(), true);
  }

  inline auto error (const float f32) {
    write(JSON::Number(f32).str(), true);
  }

  inline auto error (const char* string) {
    write(String(string), true);
  }

  inline auto error (const JSON::Any& json) {
    if (json.isString()) {
      error(json.as<JSON::String>().value());
    } else {
      error(json.str());
    }
  }

  inline auto error (const Config& config) {
    error(config.str());
  }

  inline auto error (const Script& script) {
    error(script.str());
  }

  inline auto error (const Headers& headers) {
    error(headers.str());
  }

  inline auto error (const Headers::Entries& entries) {
    error(Headers(entries).str());
  }

  inline auto error (const ipc::Result& result) {
    error(result.str());
  }

  inline auto error (const ipc::Message& message) {
    error(message.str());
  }

  inline auto error (const StringStream& stream) {
    info(stream.str());
  }

  inline auto error (bool boolean) {
    error(boolean ? "true" : "false");
  }

  template <typename ...Args> auto error (const String& fmt, Args... args) {
    info(format(fmt, args...));
  }
}

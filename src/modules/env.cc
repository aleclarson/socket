module;
#include <stddef.h>
#include <math.h>
#include <string>

/**
 * @module ssc.env
 * @description Set/get environment variables
 * @example
 * import ssc.env;
 * namespace ssc {
 *   auto USER = env::get("USER");
 *   auto HOME = env::get("HOME");
 *
 *   env::set("KEY=value");
 *   env::set("KEY", "value");
 * }
 * TODO
 */
export module ssc.env;
import ssc.string;

using String = ssc::string::String;

export namespace ssc::env {
  inline auto get (const char* variableName) {
    #if defined(_WIN32)
      char* variableValue = nullptr;
      std::size_t valueSize = 0;
      auto query = _dupenv_s(&variableValue, &valueSize, variableName);

      String result;
      if(query == 0 && variableValue != nullptr && valueSize > 0) {
        result.assign(variableValue, valueSize - 1);
        free(variableValue);
      }

      return result;
    #else
      auto v = getenv(variableName);

      if (v != nullptr) {
        return String(v);
      }

      return String("");
    #endif
  }

  inline auto get (const String& name) {
    return get(name.c_str());
  }

  inline auto set (const char* pair) {
    #if defined(_WIN32)
      return _putenv(pair);
    #else
      return putenv((char*) &pair[0]);
    #endif
  }

  inline auto set (const char* key, const char* value) {
    auto string = String(key) + String("=") + String(value);
    return set(string.c_str());
  }

  inline auto set (const String& key, const String& value) {
    return set(key.c_str(), value.c_str());
  }
}

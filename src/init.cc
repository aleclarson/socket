#include "common.hh"

// These rely on project-specific, compile-time variables.
namespace SSC {
  bool isDebugEnabled () {
    return DEBUG == 1;
  }

  const Map getSettingsSource () {
    #include "ini.hh" // NOLINT
    return parseConfig(hexToString(ini));
  }

  const char* getDevProtocol () {
    static const char* protocol = STR_VALUE(PROTOCOL);
    return protocol;
  }

  const char* getDevHost () {
    static const char* host = STR_VALUE(HOST);
    return host;
  }

  int getDevPort () {
    return PORT;
  }
}

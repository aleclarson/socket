#ifndef SSC_CORE_NETWORK_HH
#if !defined(SSC_INLINE_INCLUDE)
#define SSC_CORE_NETWORK_HH
#include "platform.hh"
#include "types.hh"
#endif

#if !defined(SSC_INLINE_INCLUDE)
namespace ssc::network {
  using namespace ssc::types;
#endif

  using NetworkStatusChangeCallback = std::function<void(
    const String& statusName,
    const String& statusMessage
  )>;

  class NetworkStatusObserver {
    NetworkStatusChangeCallback onNetworkStatusChangeCallback;
    void *internal = nullptr;
    public:
      NetworkStatusObserver () = default;
      NetworkStatusObserver (NetworkStatusChangeCallback callback);
      ~NetworkStatusObserver ();
      void onNetworkStatusChange (
        const String& statusName,
        const String& statusMessage
      ) const;
  };
#if !defined(SSC_INLINE_INCLUDE)
}
#endif
#endif

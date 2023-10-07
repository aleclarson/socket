cmake_minimum_required(VERSION 3.20)
project(socket)

execute_process(
  COMMAND ../bin/cflags.sh
  OUTPUT_VARIABLE CMAKE_CXX_FLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE)

set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -Wblock-capture-autoreleasing -Wbool-conversion -Wcomma -Wconstant-conversion -Wdeprecated-implementations -Werror=deprecated-objc-isa-usage -Wempty-body -Wenum-conversion -Winfinite-recursion -Wint-conversion -Wno-nullability-completeness -Wno-non-literal-null-conversion -Wno-nullable-to-nonnull-conversion -Wimplicit-retain-self -Wliteral-conversion -Werror=objc-root-class -Wquoted-include-in-framework-header -Wrange-loop-analysis -Wstrict-prototypes -Wunguarded-availability -Wunreachable-code -Werror=unused-result -Wundeclared-selector -Wuninitialized -Wunused-function -Wunused-variable -Wshorten-64-to-32"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS
    ON
    CACHE INTERNAL "")

file(GLOB CORE_SRC ${CMAKE_SOURCE_DIR}/core/*.cc)

file(GLOB APP_SRC ${CMAKE_SOURCE_DIR}/app/*.cc
     ${CMAKE_SOURCE_DIR}/extension/*.cc ${CMAKE_SOURCE_DIR}/ipc/*.cc
     ${CORE_SRC})

#
# macOS
#

file(GLOB DESKTOP_SRC ${CMAKE_SOURCE_DIR}/desktop/*.cc ${APP_SRC})

add_library(socket-mac SHARED window/apple.mm process/unix.cc ${DESKTOP_SRC})
target_compile_definitions(
  socket-mac
  PRIVATE
    CMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS} -DMACOS=1 -DOBJC_OLD_DISPATCH_PROTOTYPES=1 -framework UniformTypeIdentifiers -framework CoreBluetooth -framework CoreLocation -framework Network -framework UserNotifications -framework WebKit -framework Cocoa -framework OSLog"
)

#
# iPhone
#

file(GLOB IOS_SRC ${CMAKE_SOURCE_DIR}/ios/main.mm
     ${CMAKE_SOURCE_DIR}/window/apple.mm ${APP_SRC})

add_library(socket-ios SHARED ${IOS_SRC})
target_compile_definitions(
  socket-ios
  PRIVATE
    CMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS} -DTARGET_OS_IPHONE=1 -DOBJC_OLD_DISPATCH_PROTOTYPES=1"
)

#
# iPhone Simulator
#

add_library(socket-ios-simulator SHARED ${IOS_SRC})
target_compile_definitions(
  socket-ios-simulator
  PRIVATE
    CMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS} -DTARGET_OS_IPHONE=1 -DTARGET_IPHONE_SIMULATOR=1 -DOBJC_OLD_DISPATCH_PROTOTYPES=1"
)

#
# Socket CLI
#

file(GLOB CLI_SRC ${CMAKE_SOURCE_DIR}/cli/*.cc ${CMAKE_SOURCE_DIR}/process/*.cc
     ${CORE_SRC})

add_executable(socket-cli ${CLI_SRC})
target_compile_definitions(
  socket-cli PRIVATE CMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS} -DSSC_CLI=1")

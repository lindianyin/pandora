cmake_minimum_required(VERSION 3.20)
project(pandora-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pandora-server
  src/main.cpp
  src/common/config.cpp
  src/common/proto_wire.cpp
  src/net/frame.cpp
  src/net/http_api.cpp
  src/net/ws_server.cpp
  src/net/session_hub.cpp
  src/auth/auth_service.cpp
  src/store/memory_store.cpp
  src/store/mysql_client.cpp
  src/store/redis_client.cpp
  src/wallet/wallet_service.cpp
  src/pay/pay_service.cpp
  src/admin/admin_service.cpp
  src/activity/activity_service.cpp
  src/lobby/lobby_service.cpp
  src/match/match_service.cpp
  src/room/room_manager.cpp
  src/game/ddz_cards.cpp
)

target_include_directories(pandora-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(WIN32)
  target_compile_definitions(pandora-server PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(pandora-server PRIVATE ws2_32)
endif()

add_executable(ddz_cards_test
  tests/ddz_cards_test.cpp
  src/game/ddz_cards.cpp
)
target_include_directories(ddz_cards_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_custom_command(TARGET pandora-server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:pandora-server>/conf
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/conf/server.json
    $<TARGET_FILE_DIR:pandora-server>/conf/server.json
)


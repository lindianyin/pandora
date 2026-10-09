cmake_minimum_required(VERSION 3.20)
project(pandora-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(protobuf CONFIG REQUIRED)
find_package(unofficial-libmysql CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(redis++ CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(boost_asio CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED)

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../proto")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}")

set(PROTO_FILES
  "${PROTO_DIR}/common.proto"
  "${PROTO_DIR}/lobby.proto"
  "${PROTO_DIR}/game_ddz.proto"
  "${PROTO_DIR}/game_hzmj.proto"
  "${PROTO_DIR}/game_phz.proto"
  "${PROTO_DIR}/activity.proto"
  "${PROTO_DIR}/social.proto"
  "${PROTO_DIR}/bag.proto"
)

set(GENERATED_SRCS
  "${GENERATED_DIR}/common.pb.cc"
  "${GENERATED_DIR}/lobby.pb.cc"
  "${GENERATED_DIR}/game_ddz.pb.cc"
  "${GENERATED_DIR}/game_hzmj.pb.cc"
  "${GENERATED_DIR}/game_phz.pb.cc"
  "${GENERATED_DIR}/activity.pb.cc"
  "${GENERATED_DIR}/social.pb.cc"
  "${GENERATED_DIR}/bag.pb.cc"
)
set(GENERATED_HDRS
  "${GENERATED_DIR}/common.pb.h"
  "${GENERATED_DIR}/lobby.pb.h"
  "${GENERATED_DIR}/game_ddz.pb.h"
  "${GENERATED_DIR}/game_hzmj.pb.h"
  "${GENERATED_DIR}/game_phz.pb.h"
  "${GENERATED_DIR}/activity.pb.h"
  "${GENERATED_DIR}/social.pb.h"
  "${GENERATED_DIR}/bag.pb.h"
)

add_custom_command(
  OUTPUT ${GENERATED_SRCS} ${GENERATED_HDRS}
  COMMAND protobuf::protoc
    --proto_path=${PROTO_DIR}
    --cpp_out=${GENERATED_DIR}
    ${PROTO_FILES}
  DEPENDS ${PROTO_FILES}
  COMMENT "Generating protobuf C++ sources"
  VERBATIM
)

add_library(pandora_proto STATIC ${GENERATED_SRCS})
target_include_directories(pandora_proto PUBLIC ${GENERATED_DIR})
target_link_libraries(pandora_proto PUBLIC protobuf::libprotobuf)

add_executable(pandora-server
  src/main.cpp
  src/common/config.cpp
  src/common/proto_wire.cpp
  src/common/async_worker.cpp
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
  src/social/social_service.cpp
  src/bag/bag_service.cpp
  src/lobby/lobby_service.cpp
  src/match/match_service.cpp
  src/room/room_manager.cpp
  src/game/game_registry.cpp
  src/game/ddz/ddz_room_game.cpp
  src/game/hzmj/hzmj_room_game.cpp
  src/game/phz/phz_room_game.cpp
  src/game/ddz_cards.cpp
  src/game/hzmj/tiles.cpp
  src/game/hzmj/meld.cpp
  src/game/hzmj/hu.cpp
  src/game/hzmj/score.cpp
  src/game/hzmj/settle.cpp
  src/game/hzmj/table.cpp
  src/game/phz/tiles.cpp
  src/game/phz/config.cpp
  src/game/phz/meld.cpp
  src/game/phz/hu.cpp
  src/game/phz/settle.cpp
  src/game/phz/table.cpp
)

target_include_directories(pandora-server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${GENERATED_DIR}
)

target_link_libraries(pandora-server PRIVATE
  pandora_proto
  Boost::asio
  Boost::beast
  nlohmann_json::nlohmann_json
  unofficial::libmysql::libmysql
  hiredis::hiredis
  redis++::redis++
  spdlog::spdlog
  OpenSSL::SSL
  OpenSSL::Crypto
)

if(WIN32)
  target_compile_definitions(pandora-server PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(pandora-server PRIVATE ws2_32 mswsock)
endif()

add_executable(ddz_cards_test
  tests/ddz_cards_test.cpp
  src/game/ddz_cards.cpp
)
target_include_directories(ddz_cards_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(hzmj_rules_test
  tests/hzmj_rules_test.cpp
  src/game/hzmj/tiles.cpp
  src/game/hzmj/meld.cpp
  src/game/hzmj/hu.cpp
  src/game/hzmj/score.cpp
  src/game/hzmj/settle.cpp
)
target_include_directories(hzmj_rules_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(hzmj_table_test
  tests/hzmj_table_test.cpp
  src/game/hzmj/tiles.cpp
  src/game/hzmj/meld.cpp
  src/game/hzmj/hu.cpp
  src/game/hzmj/score.cpp
  src/game/hzmj/settle.cpp
  src/game/hzmj/table.cpp
)
target_include_directories(hzmj_table_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(phz_rules_test
  tests/phz_rules_test.cpp
  src/game/phz/tiles.cpp
  src/game/phz/config.cpp
  src/game/phz/meld.cpp
  src/game/phz/hu.cpp
  src/game/phz/settle.cpp
)
target_include_directories(phz_rules_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(phz_table_test
  tests/phz_table_test.cpp
  src/game/phz/tiles.cpp
  src/game/phz/config.cpp
  src/game/phz/meld.cpp
  src/game/phz/hu.cpp
  src/game/phz/settle.cpp
  src/game/phz/table.cpp
)
target_include_directories(phz_table_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_custom_command(TARGET pandora-server POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:pandora-server>/conf
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/conf/server.json
    $<TARGET_FILE_DIR:pandora-server>/conf/server.json
)

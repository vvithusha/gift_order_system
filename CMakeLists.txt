cmake_minimum_required(VERSION 3.15)
project(GiftShop C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(giftshop src/main.cpp third_party/sqlite3.c)
target_include_directories(giftshop PRIVATE src third_party)

if(MSVC)
  target_compile_options(giftshop PRIVATE /utf-8 /W3)   # /utf-8 is needed for the emoji in the source
  target_compile_definitions(giftshop PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(giftshop PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

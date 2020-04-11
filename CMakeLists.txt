cmake_minimum_required(VERSION 3.10 FATAL_ERROR) # because of c++17
project(jbus)

if (NOT MSVC)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(WIN32)
set(JBUS_PLAT_LIBS Ws2_32)
elseif(UNIX AND NOT APPLE)
set(JBUS_PLAT_LIBS pthread)
endif()

add_library(jbus
            lib/Socket.cpp include/jbus/Socket.hpp
            lib/Common.cpp include/jbus/Common.hpp
            lib/Endpoint.cpp include/jbus/Endpoint.hpp
            lib/Listener.cpp include/jbus/Listener.hpp)
target_link_libraries(jbus ${JBUS_PLAT_LIBS})
target_include_directories(jbus PUBLIC include)

add_executable(joyboot tools/joyboot.cpp)
target_link_libraries(joyboot jbus)

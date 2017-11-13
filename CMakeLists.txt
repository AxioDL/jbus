cmake_minimum_required(VERSION 3.1.0 FATAL_ERROR) # because of CMAKE_CXX_STANDARD
project(jbus)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
set(JBUS_PLAT_LIBS Ws2_32)
elseif(UNIX AND NOT APPLE)
set(JBUS_PLAT_LIBS pthread)
endif()

include_directories(include)
add_library(jbus
            include/jbus/optional.hpp
            include/jbus/Socket.hpp
            lib/Common.cpp include/jbus/Common.hpp
            lib/Endpoint.cpp include/jbus/Endpoint.hpp
            lib/Listener.cpp include/jbus/Listener.hpp)
target_link_libraries(jbus ${JBUS_PLAT_LIBS})

add_executable(joyboot tools/joyboot.cpp)
target_link_libraries(joyboot jbus)

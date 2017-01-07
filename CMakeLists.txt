cmake_minimum_required(VERSION 3.0)
project(jbus)

if(WIN32)
set(JBUS_PLAT_LIBS Ws2_32)
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

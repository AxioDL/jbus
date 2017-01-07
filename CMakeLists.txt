cmake_minimum_required(VERSION 3.0)
project(jbus)

include_directories(include)
add_library(jbus
            include/jbus/optional.hpp
            include/jbus/Socket.hpp
            src/Common.cpp include/jbus/Common.hpp
            src/Endpoint.cpp include/jbus/Endpoint.hpp
            src/Listener.cpp include/jbus/Listener.hpp)
add_executable(joyboot tools/joyboot.cpp)
target_link_libraries(joyboot jbus)

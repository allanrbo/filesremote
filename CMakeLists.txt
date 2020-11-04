cmake_minimum_required(VERSION 3.10)
project(sftpgui)

set(ENV{CMAKE_PREFIX_PATH} /home/allan/dev/wxWidgets-3.1.4)

find_package(wxWidgets COMPONENTS core base REQUIRED)
include("${wxWidgets_USE_FILE}")

add_executable(sftpgui main.cpp)
target_link_libraries(sftpgui PRIVATE ${wxWidgets_LIBRARIES})
target_link_libraries(sftpgui PRIVATE "/home/allan/dev/libssh2/bin/src/libssh2.a")
target_link_libraries(sftpgui PRIVATE crypto)
target_include_directories(sftpgui PRIVATE "/home/allan/dev/libssh2/include")

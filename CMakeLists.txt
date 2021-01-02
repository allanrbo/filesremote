cmake_minimum_required(VERSION 3.10)

project(filesremote VERSION 1.1 LANGUAGES CXX)

SET(CMAKE_CXX_STANDARD 17)

set(MSYS_ORIG ${MSYS})
set(MSYS 0)  # Because FindwxWidgets.cmake breaks due to some strange path handling when it knows its in MSYS.
find_package(wxWidgets COMPONENTS core base REQUIRED)
include("${wxWidgets_USE_FILE}")
set(MSYS ${MSYS_ORIG})

find_package(OpenSSL REQUIRED)

find_package(Libssh2 REQUIRED)

include_directories(${CMAKE_SOURCE_DIR})

add_subdirectory(src)

configure_file(version.h.in version.h)
target_include_directories(filesremote PUBLIC "${PROJECT_BINARY_DIR}")

if(MSYS)  # Windows
    message("Packaging for Windows")
    set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PROJECT_VERSION}-Windows-x86_64-static")
    install(
            TARGETS filesremote
            RUNTIME DESTINATION .
    )
elseif(APPLE)
    message("Packaging for macOS")
    set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PROJECT_VERSION}-macOS-x86_64")
    set_target_properties(filesremote PROPERTIES
            OUTPUT_NAME "FilesRemote"
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist)
    install(
            TARGETS filesremote
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            BUNDLE DESTINATION .
    )
elseif(UNIX)
    message("Packaging for Linux")
    set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${PROJECT_VERSION}-Linux-x86_64-static")
    set(CPACK_PACKAGE_VENDOR "Allan Boll")
    set(CPACK_PACKAGE_CONTACT "Allan Boll <allan@acoby.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "An SFTP file manager")
    set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
    install(
            TARGETS filesremote
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(FILES FilesRemote.desktop DESTINATION share/applications )
    install(FILES graphics/appicon/icon.svg DESTINATION share/icons/hicolor/scalable/apps RENAME FilesRemote.svg)
endif()

include(CPack)

add_executable(filesremote
        artprovider.cpp artprovider.h
        channel.h
        connectdialog.cpp connectdialog.h
        direntry.cpp direntry.h
        dirlistctrl.cpp dirlistctrl.h
        string.cpp string.h
        filemanagerframe.cpp filemanagerframe.h
        filesystem.osx.polyfills.h
        hostdesc.cpp hostdesc.h
        licensestrings.cpp licensestrings.h
        main.cpp
        passworddialog.cpp passworddialog.h
        paths.cpp paths.h
        preferencespanel.cpp preferencespanel.h
        sftpconnection.cpp sftpconnection.h
        sftpthread.cpp sftpthread.h

        resource.rc  # Icon and other resources for Windows.
        ${CMAKE_CURRENT_SOURCE_DIR}/../graphics/appicon/icon.icns  # Icon for macOS.
        )

target_link_libraries(filesremote PRIVATE ${wxWidgets_LIBRARIES} OpenSSL::Crypto Libssh2::libssh2)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../graphics/appicon/icon.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")

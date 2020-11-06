Sftpgui
=======

A no-nonsense SFTP file browser. Downloads and opens files in local editors and uploads when changes are detected.

Cross platform with fairly native feel (uses wxWidgets).

Lint
----

    cpplint --linelength=120 --filter=-whitespace/indent main.cpp

Linux build
-----------

Followed https://www.binarytides.com/install-wxwidgets-ubuntu/ .

Prereqs:

    #apt install libwxgtk3.0-gtk3-dev
    sudo apt install libssl-dev

    cd $HOME/dev
    wget https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.4/wxWidgets-3.1.4.tar.bz2
    tar -xjf wxWidgets-3.1.4.tar.bz2
    cd wxWidgets-3.1.4/
    # TODO reduce binary size by disabling more things? --disable-all-features
    ./configure --disable-shared --enable-unicode
    # --enable-debug
    make -j8

    cd $HOME/dev
    git clone https://github.com/libssh2/libssh2.git
    cd libssh2
    mkdir bin
    cd bin
    cmake ..
    cmake --build .

Compiling:

    g++ -std=c++17 main.cpp `../wxWidgets-3.1.4/wx-config --static=yes --cxxflags --libs core` -I../libssh2/include/ ../libssh2/bin/src/libssh2.a -lcrypto -o sftpgui



MacOS build
-----------

Prereqs:

    curl -L -O https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.4/wxWidgets-3.1.4.tar.bz2
    tar -v -xjf wxWidgets-3.1.4.tar.bz2
    cd wxWidgets-3.1.4/
    ./configure --disable-shared --enable-unicode --without-libjpeg --without-libtiff --with-macosx-version-min=10.12

    brew install wxmac --build-from-source

    brew install libssh2


Compiling:

    g++ -std=c++17 main.cpp `$HOME/dev/wxWidgets-3.1.4/wx-config --static=yes --cxxflags --libs core | sed "s/-ljpeg//g" | sed "s/-ltiff//g"` -lz /usr/local/lib/libssh2.a /usr/local/Cellar/openssl@1.1/1.1.1h/lib/libcrypto.a /usr/local/Cellar/openssl@1.1/1.1.1h/lib/libssl.a -o sftpgui


Windows build
-------------

Use msys2 (not the old msys).

Prereqs in msys2:

    C:\msys64\msys2_shell.cmd

        pacman -Syy     # Update database cache
        pacman -Syuu    # Upgrade all packages
        pacman -S base-devel
        #pacman -S gcc
        pacman -S mingw-w64-x86_64-toolchain

        cd
        wget https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.4/wxWidgets-3.1.4.tar.bz2
        tar -v -xjf wxWidgets-3.1.4.tar.bz2   # or just use 7zip if this hangs...
        cd wxWidgets-3.1.4/
        export PATH=/mingw64/bin:$PATH
        ./configure --disable-shared --enable-unicode --without-libjpeg --without-libtiff --build=x86_64-pc-mingw64
        # TODO try to compile a smaller lib, something like:   ./configure --disable-all-features --disable-shared --enable-unicode --enable-ole --enable-dataobj --enable-dynlib --enable-variant --enable-menus --enable-menubar --enable-dccache --build=x86_64-pc-mingw64
        make -j8

        cd
        wget https://www.libssh2.org/download/libssh2-1.9.0.tar.gz
        tar -xzf libssh2-1.9.0.tar.gz
        cd libssh2-1.9.0
        export PATH=/mingw64/bin:$PATH
        # TODO get this working: --with-crypto=wincng
        export CFLAGS=""
        export LIBS="-lws2_32"
        ./configure
        make -j8

        # To troubleshoot, show commands:  make SHELL='sh -x'

Compiling:

    export PATH=/mingw64/bin:$PATH

    cd /z/dev/sftpgui
    `$HOME/wxWidgets-3.1.4/wx-config --rescomp` resource.rc resource.o
    g++ -std=c++17 main.cpp resource.o --static `$HOME/wxWidgets-3.1.4/wx-config --static=yes --cxxflags --libs` -I$HOME/libssh2-1.9.0/include $HOME/libssh2-1.9.0/src/.libs/libssh2.a -lssl -lcrypto -lz -lws2_32 -o sftpgui.exe

    # to get console output...
    g++ -std=c++17 -mconsole -Wl,--subsystem,console main.cpp resource.o --static `$HOME/wxWidgets-3.1.4/wx-config --static=yes --cxxflags --libs` -I$HOME/libssh2-1.9.0/include $HOME/libssh2-1.9.0/src/.libs/libssh2.a -lssl -lcrypto -lz -lws2_32 -mconsole -Wl,--subsystem,console -o sftpgui.exe

    # TODO use wincng instead of openssl... -lbcrypt -lcrypt32

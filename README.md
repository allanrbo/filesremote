Sftpgui
=======

A no-nonsense SFTP file browser. Downloads and opens files in local editors and uploads when changes are detected.

Cross platform with fairly native feel (uses wxWidgets).

Using
----------


Developing
----------

### Lint

    cpplint --linelength=120 --filter=-whitespace/indent main.cpp

### Linux build

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


### MacOS build

Prereqs:

    curl -L -O https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.4/wxWidgets-3.1.4.tar.bz2
    tar -v -xjf wxWidgets-3.1.4.tar.bz2
    cd wxWidgets-3.1.4/
    ./configure --disable-shared --enable-unicode --without-libjpeg --without-libtiff --with-macosx-version-min=10.12

    brew install wxmac --build-from-source

    brew install libssh2


Compiling:

    g++ -std=c++17 main.cpp `$HOME/dev/wxWidgets-3.1.4/wx-config --static=yes --cxxflags --libs core | sed "s/-ljpeg//g" | sed "s/-ltiff//g"` -lz /usr/local/lib/libssh2.a /usr/local/Cellar/openssl@1.1/1.1.1h/lib/libcrypto.a /usr/local/Cellar/openssl@1.1/1.1.1h/lib/libssl.a -o sftpgui

    rm -fr Sftpgui.app
    mkdir -p Sftpgui.app/Contents/MacOS/
    mkdir -p Sftpgui.app/Contents/Resources/
    cp Info.plist Sftpgui.app/Contents/
    echo APPL???? > Sftpgui.app/Contents/PkgInfo
    cp sftpgui Sftpgui.app/Contents/MacOS/
    cp graphics/appicon/icon.icns Sftpgui.app/Contents/Resources/

    rm -fr /Applications/Sftpgui.app ; cp -r Sftpgui.app /Applications/Sftpgui.app

    hdiutil create -srcfolder Sftpgui.app \
      -volname Sftpgui \
      -fs HFS+ -fsargs "-c c=64,a=16,e=16" \
      -format UDZO -imagekey zlib-level=9 /tmp/Sftpgui.dmg
    cp /tmp/Sftpgui.dmg .
    rm /tmp/Sftpgui.dmg

    alias sftpgui="open -a Sftpgui --args $@"


### Windows build

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
    `$HOME/wxWidgets-3.1.4/wx-config --rescomp` --define wxUSE_DPI_AWARE_MANIFEST=1 resource.rc resource.o
    g++ -std=c++17 main.cpp resource.o --static `$HOME/wxWidgets-3.1.4/wx-config --static=yes --cxxflags --libs` -I$HOME/libssh2-1.9.0/include $HOME/libssh2-1.9.0/src/.libs/libssh2.a -lssl -lcrypto -lz -lws2_32 -o sftpgui.exe

    #  && cp sftpgui.exe /c/Users/Allan/Desktop/ && /c/Users/Allan/Desktop/sftpgui.exe orange

    # TODO use wincng instead of openssl... -lbcrypt -lcrypt32


### TODO

 * Executable flags should give executable icon
 * Button for back and fwd.
 * Sudo checkbox or button
 * Drag & drop file upload / download
 * Rename files
 * Delete files
 * Create files
 * Create directories
 * Ctrl-c / ctrl-v
 * Backspace should navigate back (keep stack of prev dirs).
 * Change dir or refresh when disconnected causes crash?
 * Add a FilterAway method to Channel that can be used to remove events that superseed other events, such as when enqueuing a dir change and there are already enqueued dir changes, or when returning from a file download and there were other file requests in queue for the same file.
 * Second navigation that happened before the first navigation returned. Or third or fourth. Handle nicely.
 * When a file path is pasted to the path bar, open the file directly and go to its directory.
 * Change permissions
 * Fully use cmake on all platforms
 * Split source code into files
 * Ensure use of unique_ptr everywhere possible, instead of raw pointers
 * A better host selection window. Get inspired by Finder's "Connect to Server" window.
 * Better control the config path. ".config/sftpgui"
 * Warn if thumbprint changed
 * What happens if we try to upload to a dir that has since been deleted...
 * More human friendly size numbers (KiB, MiB, etc.)
 * Potential unicode issue: unicode char in username (therefore temp dir) on windows
 * Clean up old files on startup

Error conditions to test:
 * Bad DNS name while connecting.
 * Bad IP while connecting.
 * Connecting to a port where nothing is listening.
 * Connecting to a valid TCP port but that is not SSH.
 * Connecting to a tarpit.
 * Disconnect cable and then refresh.
 * Disconnect cable and then modify locally opened file (auto upload).
 * Modify file which we do not have write permissions to (auto upload).
 * List directory which we do not have permissions to.
 * List directory which remotely is mounted to something which has lost connection (broken FUSE for example).

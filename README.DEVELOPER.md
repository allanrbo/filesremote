Development
-----------

### Building the prerequisites

Linux environment setup:

    sudo apt install libssl-dev libsecret-1-dev libgtk-3-dev cmake

MacOS environment setup:

    xcode-select --install

    # Manually download and install a CMake binary dmg from cmake.org.
    # After installing CMake from dmg, add it to PATH:
    sudo mkdir -p /usr/local/bin
    sudo /Applications/CMake.app/Contents/bin/cmake-gui --install=/usr/local/bin

Windows environment setup, using MSYS2 (C:\msys64\msys2_shell.cmd):

    pacman -Syy     # Update database cache
    pacman -Syuu    # Upgrade all packages
    pacman -S base-devel
    pacman -S mingw-w64-x86_64-toolchain
    pacman -S cmake
    pacman -S git
    export PATH="/mingw64/bin:$PATH"

Get and build the 3rd party dependencies:

    WORKDIR=$HOME/dev   # or whereever you prefer having your source trees.

    # WxWidgets
    cd $WORKDIR
    curl -L -O https://github.com/wxWidgets/wxWidgets/releases/download/v3.1.4/wxWidgets-3.1.4.tar.bz2
    tar -v -xjf wxWidgets-3.1.4.tar.bz2
    cd wxWidgets-3.1.4/
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # MacOS.
        ./configure --disable-shared --enable-unicode --without-libjpeg --without-libtiff --with-macosx-version-min=10.13
    elif [[ "$OSTYPE" == "msys" ]]; then
        # Windows with MSYS.
        ./configure --disable-shared --enable-unicode --without-libjpeg --without-libtiff --with-msw --build=x86-winnt-mingw32
    else
        # Linux.
        ./configure --disable-shared --enable-unicode --without-libjpeg --without-libtiff
        # Optional for step-debugging into wxWidgets funcs: --enable-debug
    fi
    make -j4

    # OpenSSL
    cd $WORKDIR
    git clone --depth 1 --no-checkout https://github.com/openssl/openssl.git
    cd openssl
    git fetch origin tag openssl-3.1.1
    git checkout tags/openssl-3.1.1
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # MacOS.
        export CFLAGS=-mmacosx-version-min=10.13
        export LDFLAGS=-mmacosx-version-min=10.13
    fi

    if [[ "$OSTYPE" == "msys" ]]; then
        # Windows with MSYS.
        ./configure mingw64
    else
        # Linux and MacOS.
        ./config
    fi
    make -j4

    # Libssh2
    cd $WORKDIR
    git clone https://github.com/libssh2/libssh2.git
    cd libssh2
    git checkout tags/libssh2-1.11.0
    mkdir mybuild
    cd mybuild
    export CMAKE_PREFIX_PATH="$WORKDIR/openssl"
    WIN32FLAG=
    if [[ "$OSTYPE" == "msys" ]]; then
        # Windows with MSYS.
        WIN32FLAG=-DWIN32=1
    fi
    cmake $WIN32FLAG -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13 -DBUILD_EXAMPLES=ON -DBUILD_TESTING=OFF -DOPENSSL_USE_STATIC_LIBS=TRUE -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF ..
    VERBOSE=1 cmake --build .
    # This library registers itself in the registry at ~/.cmake/packages/


### Building FilesRemote

    WORKDIR=$HOME/dev   # or whereever you prefer having your source trees.
    SRCDIR=$WORKDIR/filesremote
    BUILDDIR=$SRCDIR/build$OSTYPE

    # Prepare output directory.
    echo $BUILDDIR
    rm -fr $BUILDDIR ; mkdir -p $BUILDDIR ; cd $BUILDDIR

    # Let CMake know where it can find our 3rd party dependencies.
    export CMAKE_PREFIX_PATH="$WORKDIR/wxWidgets-3.1.4:$WORKDIR/libssh2/mybuild/src:$WORKDIR/openssl-1.1.1h"

    if [[ "$OSTYPE" == "msys" ]]; then
        # Windows with MSYS.
        export LDFLAGS=-static
        export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH:/mingw64/x86_64-w64-mingw32/lib"
    fi

    # Generate makefiles.
    cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13 -DOPENSSL_USE_STATIC_LIBS=TRUE -DCMAKE_BUILD_TYPE=Release $SRCDIR

    # Build.
    VERBOSE=1 cmake --build .

    # Package.
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # MacOS.
        cpack -D CPACK_GENERATOR=DragNDrop
    elif [[ "$OSTYPE" == "msys" ]]; then
        # Windows with MSYS.
        cpack -D CPACK_GENERATOR=ZIP
    else
        # Linux.
        cpack -D CPACK_GENERATOR=DEB
        cpack -D CPACK_GENERATOR=TGZ
    fi


### Troubleshooting build issues

Error "Could NOT find wxWidgets": edit /usr/share/cmake-3.18.4/Modules/FindwxWidgets.cmake to comment in the lines after `macro(DBG_MSG _MSG)`.

MacOS error "Killed: 9" error when starting binary on different machine via a Samba volume: Possibly something with the kernel having cached the binary, and detecting a mismatch. Try reboot, or use a different output binary name.


### Validate that the linking is as you intended (static or dynamic)

    # Linux and Windows+MSYS2
    ldd filesremote
    ldd filesremote.exe

    # macOS
    otool -L filesremote


### Lint

    cpplint --linelength=120 --filter=-whitespace/indent --recursive src/*

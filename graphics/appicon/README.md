Icon was drawn in Inkscape and exported manually in each resolution.


Windows ICO file
================

Run on Linux:

    convert icon_256x256.png icon_128x128.png icon_96x96.png icon_64x64.png icon_48x48.png icon_32x32.png icon_16x16.png icon.ico


Linux XPM file
==============

Run on Linux:

    convert icon_48x48.png icon_48x48.xpm
    sed -i 's/^static char /const static char/g' icon_48x48.xpm



MacOS 
==============

Run on MacOS:

    mkdir icon.iconset

    cp icon_16x16.png icon.iconset/icon_16x16.png
    cp icon_32x32.png icon.iconset/icon_16x16@2x.png

    cp icon_32x32.png icon.iconset/icon_32x32.png
    cp icon_64x64.png icon.iconset/icon_32x32@2x.png

    cp icon_128x128.png icon.iconset/icon_128x128.png
    cp icon_256x256.png icon.iconset/icon_128x128@2x.png

    cp icon_256x256.png icon.iconset/icon_256x256.png
    cp icon_512x512.png icon.iconset/icon_256x256@2x.png

    cp icon_512x512.png icon.iconset/icon_512x512.png
    cp icon_1024x1024.png icon.iconset/icon_512x512@2x.png

    iconutil --convert icns icon.iconset

    rm -fr icon.iconset

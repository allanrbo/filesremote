#!/bin/bash

# VSCode icons

INKSCAPE="flatpak run org.inkscape.Inkscape"
PNG2C="$HOME/dev/wxWidgets-3.1.4/misc/scripts/png2c.py"
HFILE=uiicons.data.h
TANGO_ROOT=tango-icon-theme-0.8.90
VSCODE_ROOT=vscode-icons

rm $HFILE

function convertIconAtSize() {
    NAME=$1
    FILENAME=$2
    SIZE=$3
    COLOR=$4

    $INKSCAPE --export-area-page --export-width=$SIZE --export-area-snap --export-filename=${NAME}_${SIZE}_${COLOR}.png $VSCODE_ROOT/icons/$COLOR/$FILENAME.svg

    python3 $PNG2C ${NAME}_${SIZE}_${COLOR}.png >> $HFILE

    rm ${NAME}_${SIZE}_${COLOR}.png
}

function convertIcon() {
    NAME=$1
    FILENAME=$2
    convertIconAtSize $NAME $FILENAME 16 light
    convertIconAtSize $NAME $FILENAME 16 dark
    convertIconAtSize $NAME $FILENAME 24 light
    convertIconAtSize $NAME $FILENAME 24 dark
    convertIconAtSize $NAME $FILENAME 32 light
    convertIconAtSize $NAME $FILENAME 32 dark
    convertIconAtSize $NAME $FILENAME 48 light
    convertIconAtSize $NAME $FILENAME 48 dark
    convertIconAtSize $NAME $FILENAME 64 light
    convertIconAtSize $NAME $FILENAME 64 dark
}

convertIcon _parent_dir arrow-up
convertIcon _nav_back arrow-left
convertIcon _nav_fwd arrow-right
convertIcon _refresh refresh
convertIcon _open_file edit
convertIcon _new_file new-file
convertIcon _new_dir new-folder
convertIcon _rename rename
convertIcon _delete trash
convertIcon _upload cloud-upload
convertIcon _download cloud-download
convertIcon _sudo shield
#convertIcon _directory folder
#convertIcon _file file




# Tango icons

function convertIconAtSize() {
    NAME=$1
    SIZE=$2
    COLOR=$3
    FILEPATH=$4

    echo "Converting $FILEPATH to ${SIZE}x${SIZE}"

    $INKSCAPE --export-area-page --export-width=$SIZE --export-area-snap --export-filename=${NAME}_${SIZE}_${COLOR}.png $FILEPATH
    python3 $PNG2C ${NAME}_${SIZE}_${COLOR}.png >> $HFILE
    rm ${NAME}_${SIZE}_${COLOR}.png
}

function copyIconAtSize() {
    NAME=$1
    SIZE=$2
    COLOR=$3
    FILEPATH=$4

    echo "Copying $FILEPATH"

    cp $FILEPATH ${NAME}_${SIZE}_${COLOR}.png
    python3 $PNG2C ${NAME}_${SIZE}_${COLOR}.png >> $HFILE
    rm ${NAME}_${SIZE}_${COLOR}.png
}

function findAndConvertIcon() {
    NAME=$1
    CATEGORY=$2
    FILENAME=$3
    SIZE=$4
    COLOR=$5

    echo "Getting $CATEGORY / $NAME $COLOR to ${SIZE}x${SIZE}"

    color_dir=
    if [ "$COLOR" == "dark" ]; then
        color_dir=Dark
    fi

    if [ $SIZE -eq 32 ] && [ -f "$TANGO_ROOT/32x32/$CATEGORY/${FILENAME}.png" ]; then
        copyIconAtSize $NAME $SIZE $COLOR "$TANGO_ROOT/32x32/$CATEGORY/${FILENAME}.png"
    elif [ $SIZE -eq 22 ] && [ -f "$TANGO_ROOT/22x22/$CATEGORY/${FILENAME}.png" ]; then
        copyIconAtSize $NAME $SIZE $COLOR "$TANGO_ROOT/22x22/$CATEGORY/${FILENAME}.png"
    elif [ $SIZE -eq 16 ] && [ -f "$TANGO_ROOT/16x16/$CATEGORY/${FILENAME}.png" ]; then
        copyIconAtSize $NAME $SIZE $COLOR "$TANGO_ROOT/16x16/$CATEGORY/${FILENAME}.png"
    elif [ -f "$TANGO_ROOT/scalable/$CATEGORY/${FILENAME}.svg" ]; then
        convertIconAtSize $NAME $SIZE $COLOR "$TANGO_ROOT/scalable/$CATEGORY/${FILENAME}.svg"
    fi
}

function convertIcon() {
    NAME=$1
    CATEGORY=$2
    FILENAME=$3
    findAndConvertIcon $NAME $CATEGORY $FILENAME 16 light
    findAndConvertIcon $NAME $CATEGORY $FILENAME 16 dark
    findAndConvertIcon $NAME $CATEGORY $FILENAME 24 light
    findAndConvertIcon $NAME $CATEGORY $FILENAME 24 dark
    findAndConvertIcon $NAME $CATEGORY $FILENAME 32 light
    findAndConvertIcon $NAME $CATEGORY $FILENAME 32 dark
    findAndConvertIcon $NAME $CATEGORY $FILENAME 48 light
    findAndConvertIcon $NAME $CATEGORY $FILENAME 48 dark
    findAndConvertIcon $NAME $CATEGORY $FILENAME 64 light
    findAndConvertIcon $NAME $CATEGORY $FILENAME 64 dark
}

#convertIcon _parent_dir actions go-up
#convertIcon _nav_back actions go-previous
#convertIcon _nav_fwd actions go-next
#convertIcon _refresh actions view-refresh
#convertIcon _open_file apps accessories-text-editor
#convertIcon _new_file actions document-new
#convertIcon _new_dir actions folder-new
#convertIcon _rename actions format-text-underline
#convertIcon _delete actions edit-delete
convertIcon _directory places folder
convertIcon _file mimetypes text-x-generic
convertIcon _file_exec mimetypes application-x-executable
convertIcon _symlink emblems emblem-symbolic-link
convertIcon _file_picture mimetypes image-x-generic
convertIcon _package mimetypes package-x-generic

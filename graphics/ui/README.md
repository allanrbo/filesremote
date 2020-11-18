    # VSCode icons

    INKSCAPE="flatpak run org.inkscape.Inkscape"

    function convertIconAtSize() {
        NAME=$1
        FILENAME=$2
        SIZE=$3
        COLOR=$4

        INKSCAPE="flatpak run org.inkscape.Inkscape"

        #--export-background=000000 --export-background-opacity=1 
        $INKSCAPE --export-area-page --export-width=$SIZE --export-area-snap --export-filename=${NAME}_${SIZE}_${COLOR}.png vscode-icons/icons/$COLOR/$FILENAME.svg

        python3 $HOME/dev/wxWidgets-3.1.4/misc/scripts/png2c.py ${NAME}_${SIZE}_${COLOR}.png >> ui_icons.h
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

    rm ui_icons.h
    convertIcon _parent_dir arrow-up
    convertIcon _nav_back arrow-left
    convertIcon _nav_fwd arrow-right
    convertIcon _refresh refresh
    convertIcon _open_file edit
    convertIcon _new_file new-file
    convertIcon _new_dir new-folder
    convertIcon _rename rename
    convertIcon _delete trash
    #convertIcon _directory folder
    #convertIcon _file file


    # Tango icons

    INKSCAPE="flatpak run org.inkscape.Inkscape"
    TANGO_ROOT=tango-icon-theme-0.8.90

    function convertIconAtSize() {
        NAME=$1
        SIZE=$2
        COLOR=$3
        FILEPATH=$4

        echo "Converting $FILEPATH to ${SIZE}x${SIZE}"

        $INKSCAPE --export-area-page --export-width=$SIZE --export-area-snap --export-filename=${NAME}_${SIZE}_${COLOR}.png $FILEPATH
        python3 $HOME/dev/wxWidgets-3.1.4/misc/scripts/png2c.py ${NAME}_${SIZE}_${COLOR}.png >> ui_icons.h
        rm ${NAME}_${SIZE}_${COLOR}.png
    }

    function copyIconAtSize() {
        NAME=$1
        SIZE=$2
        COLOR=$3
        FILEPATH=$4

        echo "Copying $FILEPATH"

        cp $FILEPATH ${NAME}_${SIZE}_${COLOR}.png
        python3 $HOME/dev/wxWidgets-3.1.4/misc/scripts/png2c.py ${NAME}_${SIZE}_${COLOR}.png >> ui_icons.h
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

    #rm ui_icons.h
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









    # Antu icons

    INKSCAPE="flatpak run org.inkscape.Inkscape"
    ANTU_ROOT=$HOME/dev/antu-classic

    function convertIconAtSize() {
        NAME=$1
        SIZE=$2
        COLOR=$3
        FILEPATH=$4

        echo "Converting $FILEPATH to ${SIZE}x${SIZE}"

        $INKSCAPE --export-area-page --export-width=$SIZE --export-area-snap --export-filename=${NAME}_${SIZE}_${COLOR}.png $FILEPATH
        python3 $HOME/dev/wxWidgets-3.1.4/misc/scripts/png2c.py ${NAME}_${SIZE}_${COLOR}.png >> ui_icons.h
        rm ${NAME}_${SIZE}_${COLOR}.png
    }

    function findAndConvertIcon() {
        NAME=$1
        CATEGORY=$2
        FILENAME=$3
        SIZE=$4
        COLOR=$5

        echo "Converting $CATEGORY / $NAME $COLOR to ${SIZE}x${SIZE}"

        color_dir=
        if [ "$COLOR" == "dark" ]; then
            color_dir=Dark
        fi

        if [ $SIZE -ge 64 ] && [ -f "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/64/${FILENAME}.svg" ]; then
            convertIconAtSize $NAME $SIZE $COLOR "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/64/${FILENAME}.svg"
        elif [ $SIZE -ge 48 ] && [ -f "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/48/${FILENAME}.svg" ]; then
            convertIconAtSize $NAME $SIZE $COLOR "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/48/${FILENAME}.svg"
        elif [ $SIZE -ge 32 ] && [ -f "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/32/${FILENAME}.svg" ]; then
            convertIconAtSize $NAME $SIZE $COLOR "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/32/${FILENAME}.svg"
        elif [ $SIZE -ge 24 ] && [ -f "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/24/${FILENAME}.svg" ]; then
            convertIconAtSize $NAME $SIZE $COLOR "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/24/${FILENAME}.svg"
        elif [ $SIZE -ge 22 ] && [ -f "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/22/${FILENAME}.svg" ]; then
            convertIconAtSize $NAME $SIZE $COLOR "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/22/${FILENAME}.svg"
        elif [ $SIZE -ge 16 ] && [ -f "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/16/${FILENAME}.svg" ]; then
            convertIconAtSize $NAME $SIZE $COLOR "$ANTU_ROOT/Antu${color_dir}/$CATEGORY/16/${FILENAME}.svg"
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

    #rm ui_icons.h
    #convertIcon _parent_dir actions go-parent-folder
    #convertIcon _nav_back actions arrow-left
    #convertIcon _nav_fwd actions arrow-right
    #convertIcon _refresh actions view-refresh
    #convertIcon _open_file actions document-open
    #convertIcon _new_file actions document-new
    #convertIcon _new_dir actions folder-new
    #convertIcon _rename actions edit-rename
    #convertIcon _delete actions trash-empty
    #convertIcon _directory places folder
    #convertIcon _file actions x-shape-text




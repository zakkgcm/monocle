#_Monocle_ views images.
no silly editing tools no background setting things just view images.
possible image management features (delete, organize, etc).
use GTK for user interface for consistency.

## Goals
* Asynchronosity, no gooey freezing up when loading or scaling big hueg image
* "Just Works"
* Lean as in Pocket

## Dependencies
* Gtk+ 2

## License(s)
* see LICENSE for monocle code license
* see md5.c for the license to the code that does the md5summing

## Compiling
* lignucks
    * `make`
    * there is no step 2
* windowez
    * get mingw
    * edit config.mk to point to the proper mingw dir
    * `make`

## Usage
    monocle [options] [file or folder] ...
    options are as follows:
        -s (scale)  | sets image scale
        -R          | recursively loads folder if specified
        -v          | version info
        -h          | prints help message

files are added to a list of images to be viewed as oppose to viewing a folder or bunch at a time
delete removes images from the list, shift delete clears it
plus and minus to zoom

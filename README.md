#_Monocle_ views images.
No silly editing tools no background setting things just view images.  
Possible image management features (delete, organize, etc).  
Use GTK for user interface for consistency.

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

## Installing
* lignucks
    * `sudo make install`
* windowez
    * copy that exe

## Usage
    monocle [options] [file or folder] ...
    options are as follows:
        -s (scale)  | sets image scale
        -R          | recursively loads folder if specified
        -v          | version info
        -h          | prints help message
    
example: `monocle -R ~/Photos/ ~/Downloads/coolimage1.jpg`

Files and folders are added to a list of images to be viewed as oppose to viewing only one folder at a time.

### Keyboard Shortcuts
* Ctrl + O - Open (Add) Image(s)
* Ctrl + Alt + O - Open (Add) Folder

* Ctrl + Right/Left - Next/Previous folder
* Ctrl + Shift + Up - View All images (together in one list)

* Delete - remove image(s) from list
* Ctrl + Delete - remove current folder from list
* Shift + Delete - clear list

* Plus/Minus - Zoom

* Ctrl + Q - Quit

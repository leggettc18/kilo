# Kilo - My Pet Text Editor

This is a pet text editor I wrote in C. Nothing fancy, but it
will be a functioning text editor

At this point, it is functional for basic editing. You can open a file by
supplying the filename as an argument, you can navigate with the arrow keys,
insert text by typing, backspace and delete to remove characters, and insert
new lines with enter. And you can save your work with Ctrl + S. It is very basic,
there is no auto-indent or syntax highlighting or anything of the sort, but it
does function. You can also open up the editor with no filename argument to 
create a new file and pick a filename on the first save.
## Instructions
If you wish to build it for yourself, just install clang and make, clone the
repo, `cd` into it, and run `make` It will output an executable file called
`kilo`. Provide a text file as an argument to open it.
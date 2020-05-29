# Description
This program performs the functionalities of a disk defragmenter. Given a file representing a disk image,
the program will produce an output image that represents a defragmented disk image.

The program provides support for the following error conditions:
    - Invalid number of command-line arguments.
    - Error obtaining file information from stat() system call.
    - Error performing fopen() operation on the file given as a command-line arugment.
    - Invalid number of disk-sized members read in from the disk image file.

This program was really, really time-consuming, but seeing it all come together was quite rewarding.
I'm particularly proud of the defrag function, which is recursively implemented. I really like
writing recursive code because I feel like it can often be quite elegant, and simpler
to understand. All of my code is thoroughly commented/documented, another feat of which I am proud.
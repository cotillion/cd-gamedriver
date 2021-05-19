
# This is the CD gamedriver

It is a compiler and interpreter for the LPC language. 
It is developed to run the Genesis MUD running on https://www.genesismud.org/  

# Building

The driver should build on most modern linux distribution using glibc.
It needs bison and libjson-c to compile.

### On Debian
```
sudo apt-get install bison libjson-c-dev
```

### Build

Review the setting of MUD_LIB and BINDIR in Makefile and then build
using `make`


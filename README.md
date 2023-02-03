
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

# Docker Container

### Build

In project directory
```
docker build .
```

### To run
```
docker run -p <exposed ip address>:<exposed port>:<container port> -v <path to mud root containing mudlib, data and default domain>:/mud/lib <docker image id>
```

podman works as well for those who want do not want a management daemon running as root.

### Example
```
docker run -p 127.0.0.1:3011:3011 -v /path/to/mudlib/mud:/mud/lib 0123456789
```
or
```
podman run -p 127.0.0.1:3011:3011 -v /path/to/mudlib/mud:/mud/lib 0123456789
```

/path/to/mudlib should be the directory that contains the /d/ domain directory and mudlib

# CD Mudlib

A copy of the CD mudlib circa 1995 adapted to the modern game driver can be found here:

https://github.com/skohlsmith/cdlib

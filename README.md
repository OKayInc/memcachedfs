# memcachefs
Inspired by the original work of Tsukasa Hamano, [memcachefs](https://memcachefs.sourceforge.net/), OKay INC proudly presents the Memcached FUSE Filesystem.

## Installing
memcachedfs uses `CMake`.

### From source

    git clone https://github.com/OKayInc/memcachedfs
    cd memcachedfs
    mkdir build
    cd buid
    cmake ..
    make
    make install

#### Requirements
* libmemcached 1.0
* libfuse 2.9+

## Known Issues
* memcachedfs uses libmemcached to interact with the memcached daemon. libmemcached's memcached_dump function doesn't always return all the object keys.
* the df command doesn't report the exact used space.

## TODO
* Linux mount wrappers.
* Better internal structures to handle memcached information.
* Configurable expiration time.

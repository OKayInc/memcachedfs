# memcachefs
Inspired by the original work of Tsukasa Hamano, [memcachefs](https://memcachefs.sourceforge.net/), OKay INC proudly presents the Memcached FUSE Filesystem.

## Installing
memcachedfs uses `CMake`.

### From source

    git clone https://github.com/OKayInc/memcachefs
    cd memca
    mkdir build-libmemcached
    cd $_
    cmake ../libmemcached
    make
    sudo make install

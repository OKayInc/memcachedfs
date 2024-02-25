cmake_minimum_required(VERSION 3.0)

project(memcachedfs LANGUAGES C)
add_executable(memcachedfs main.c)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FILE_OFFSET_BITS=64")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBMEMCACHED REQUIRED IMPORTED_TARGET libmemcached)
if(${LIBMEMCACHED_FOUND})
#    MESSAGE(${PKG_CONFIG_EXECUTABLE})
#    MESSAGE(${LIBMEMCACHED_LIBRARIES})
#    MESSAGE(${LIBMEMCACHED_LINK_LIBRARIES})
endif()
target_link_libraries(memcachedfs PUBLIC ${LIBMEMCACHED_LIBRARIES})

pkg_check_modules(LIBFUSE REQUIRED IMPORTED_TARGET fuse)
if(${LIBFUSE_FOUND})
#    MESSAGE(${LIBFUSE_LIBRARIES})
endif()

target_link_libraries(memcachedfs PUBLIC ${LIBFUSE_LIBRARIES})

install(FILES mount.memcachedfs RUNTIME DESTINATION sbin)
install(TARGETS memcachedfs RUNTIME DESTINATION bin)

project(zlib)

find_package(ZLIB REQUIRED) # not upstream: the system zlib (zlib1g-dev); Qt, libpng and FreeType load it too, a bundled z.so took over their calls
add_library(zlib INTERFACE) # not upstream: the name the consumers link, as upstream's target
target_link_libraries(zlib INTERFACE ZLIB::ZLIB) # not upstream: works whether or not ZLIB was found before (an ALIAS of a directory-scoped imported target would not)
# FetchContent of zlib 1.2.11, its output folders and install(TARGETS zlib) left out: nothing is built or bundled

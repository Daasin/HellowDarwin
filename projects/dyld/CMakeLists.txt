include(libdyld.cmake)

add_library(dyld_headers INTERFACE)
target_include_directories(dyld_headers INTERFACE include)
target_link_libraries(dyld_headers INTERFACE Libc_headers xnu_headers)

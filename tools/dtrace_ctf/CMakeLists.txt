project(dtrace_ctf_host)

add_compile_options(-w)

add_subdirectory(libctf)
add_subdirectory(libdwarf)
add_subdirectory(libelf)
add_subdirectory(tools)

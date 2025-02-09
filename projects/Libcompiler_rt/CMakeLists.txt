set(COMPILER_RT_SOURCES
    lib/absvdi2.c
    lib/absvsi2.c
    lib/absvti2.c
    lib/adddf3.c
    lib/addsf3.c
    lib/addtf3.c
    lib/addvdi3.c
    lib/addvsi3.c
    lib/addvti3.c
    lib/apple_versioning.c
    lib/ashldi3.c
    lib/ashlti3.c
    lib/ashrdi3.c
    lib/ashrti3.c
    lib/atomic.c
    lib/atomic_flag_clear.c
    lib/atomic_flag_clear_explicit.c
    lib/atomic_flag_test_and_set.c
    lib/atomic_flag_test_and_set_explicit.c
    lib/atomic_signal_fence.c
    lib/atomic_thread_fence.c
    lib/bswapdi2.c
    lib/bswapsi2.c
    lib/clear_cache.c
    lib/clzdi2.c
    lib/clzsi2.c
    lib/clzti2.c
    lib/cmpdi2.c
    lib/cmpti2.c
    lib/comparedf2.c
    lib/comparesf2.c
    lib/comparetf2.c
    lib/cpu_model.c
    lib/ctzdi2.c
    lib/ctzsi2.c
    lib/ctzti2.c
    lib/divdc3.c
    lib/divdf3.c
    lib/divdi3.c
    lib/divmoddi4.c
    lib/divmodsi4.c
    lib/divsc3.c
    lib/divsf3.c
    lib/divsi3.c
    lib/divtc3.c
    lib/divtf3.c
    lib/divti3.c
    lib/divxc3.c
    lib/emutls.c
    lib/enable_execute_stack.c
    lib/eprintf.c
    lib/extenddftf2.c
    lib/extendhfsf2.c
    lib/extendsfdf2.c
    lib/extendsftf2.c
    lib/ffsdi2.c
    lib/ffssi2.c
    lib/ffsti2.c
    lib/fixdfdi.c
    lib/fixdfsi.c
    lib/fixdfti.c
    lib/fixsfdi.c
    lib/fixsfsi.c
    lib/fixsfti.c
    lib/fixtfdi.c
    lib/fixtfsi.c
    lib/fixtfti.c
    lib/fixunsdfdi.c
    lib/fixunsdfsi.c
    lib/fixunsdfti.c
    lib/fixunssfdi.c
    lib/fixunssfsi.c
    lib/fixunssfti.c
    lib/fixunstfdi.c
    lib/fixunstfsi.c
    lib/fixunstfti.c
    lib/fixunsxfdi.c
    lib/fixunsxfsi.c
    lib/fixunsxfti.c
    lib/fixxfdi.c
    lib/fixxfti.c
    lib/floatdidf.c
    lib/floatdisf.c
    lib/floatditf.c
    lib/floatdixf.c
    lib/floatsidf.c
    lib/floatsisf.c
    lib/floatsitf.c
    lib/floattidf.c
    lib/floattisf.c
    lib/floattitf.c
    lib/floattixf.c
    lib/floatundidf.c
    lib/floatundisf.c
    lib/floatunditf.c
    lib/floatundixf.c
    lib/floatunsidf.c
    lib/floatunsisf.c
    lib/floatunsitf.c
    lib/floatuntidf.c
    lib/floatuntisf.c
    lib/floatuntitf.c
    lib/floatuntixf.c
    lib/gcc_personality_v0.c
    lib/int_util.c
    lib/lshrdi3.c
    lib/lshrti3.c
    lib/mingw_fixfloat.c
    lib/moddi3.c
    lib/modsi3.c
    lib/modti3.c
    lib/muldc3.c
    lib/muldf3.c
    lib/muldi3.c
    lib/mulodi4.c
    lib/mulosi4.c
    lib/muloti4.c
    lib/mulsc3.c
    lib/mulsf3.c
    lib/multc3.c
    lib/multf3.c
    lib/multi3.c
    lib/mulvdi3.c
    lib/mulvsi3.c
    lib/mulvti3.c
    lib/mulxc3.c
    lib/negdf2.c
    lib/negdi2.c
    lib/negsf2.c
    lib/negti2.c
    lib/negvdi2.c
    lib/negvsi2.c
    lib/negvti2.c
    # lib/os_version_check.c # disabled - CoreFoundation header dependency
    lib/paritydi2.c
    lib/paritysi2.c
    lib/parityti2.c
    lib/popcountdi2.c
    lib/popcountsi2.c
    lib/popcountti2.c
    lib/powidf2.c
    lib/powisf2.c
    lib/powitf2.c
    lib/powixf2.c
    lib/subdf3.c
    lib/subsf3.c
    lib/subtf3.c
    lib/subvdi3.c
    lib/subvsi3.c
    lib/subvti3.c
    lib/trampoline_setup.c
    lib/truncdfhf2.c
    lib/truncdfsf2.c
    lib/truncsfhf2.c
    lib/trunctfdf2.c
    lib/trunctfsf2.c
    lib/ucmpdi2.c
    lib/ucmpti2.c
    lib/udivdi3.c
    lib/udivmoddi4.c
    lib/udivmodsi4.c
    lib/udivmodti4.c
    lib/udivsi3.c
    lib/udivti3.c
    lib/umoddi3.c
    lib/umodsi3.c
    lib/umodti3.c
)


add_darwin_static_library(libcompiler_rt_dyld)
target_sources(libcompiler_rt_dyld PRIVATE ${COMPILER_RT_SOURCES})
target_link_libraries(libcompiler_rt_dyld PRIVATE libplatform_headers pthread_common_headers xnu_headers AvailabilityHeaders Libc_headers)


add_darwin_shared_library(libsystem_compiler_rt NO_STANDARD_LIBRARIES INSTALL_NAME_DIR /usr/lib/system)
set_property(TARGET libsystem_compiler_rt PROPERTY PREFIX lib)
set_property(TARGET libsystem_compiler_rt PROPERTY OUTPUT_NAME compiler_rt)
target_sources(libsystem_compiler_rt PRIVATE ${COMPILER_RT_SOURCES})
target_link_libraries(libsystem_compiler_rt PRIVATE libplatform pthread_common_headers xnu_headers AvailabilityHeaders Libc_headers libunwind)
target_link_options(libsystem_compiler_rt PRIVATE -compatibility_version 1 -umbrella System)

#add_firstpass_library(libsystem_compiler_rt)
#target_link_firstpass_siblings(libsystem_compiler_rt UPWARD libc libm libunwind libplatform libdyld)
#target_link_firstpass_libraries(libsystem_compiler_rt libsystem_kernel)

install(TARGETS libsystem_compiler_rt DESTINATION /usr/lib/system COMPONENT BaseSystem)

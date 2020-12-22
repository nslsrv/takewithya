LIBRARY()

LICENSE(
    MIT
    BSD
)



# Check MUSL before NO_PLATFORM() disables it.
IF (MUSL)
    # We use C headers despite NO_PLATFORM, but we do not propagate
    # them with ADDINCL GLOBAL because we do not have an API, and we
    # can not propagate them because libcxx has to put its own
    # includes before musl includes for its include_next to work.
    ADDINCL(
        contrib/libs/musl/arch/x86_64
        contrib/libs/musl/arch/generic
        contrib/libs/musl/include
        contrib/libs/musl/extra
    )
ENDIF()

NO_UTIL()
NO_RUNTIME()
NO_PLATFORM()
NO_COMPILER_WARNINGS()

IF (GCC OR CLANG)
    # Clang (maybe GCC too) LTO code generator leaves the builtin calls unresolved
    # even if they are available. After the code generation pass is done
    # a linker is forced to select original object files from this library again
    # as they contain unresolved symbols. But code generation is already done,
    # object files actually are not ELFs but an LLVM bytecode and we get
    # "member at xxxxx is not an ELF object" errors from the linker.

    # Just generate native code from the beginning.
    DISABLE(USE_LTO)
ENDIF()

SRCS(
    addtf3.c
    ashlti3.c
    clzti2.c
    comparetf2.c
    divdc3.c
    divsc3.c
    divtf3.c
    divti3.c
    divxc3.c
    extenddftf2.c
    extendsftf2.c
    fixdfti.c
    fixsfti.c
    fixtfdi.c
    fixtfsi.c
    fixunsdfti.c
    fixunssfti.c
    fixunstfdi.c
    fixunstfsi.c
    fixunsxfti.c
    floatditf.c
    floatsitf.c
    floattidf.c
    floattisf.c
    floatunditf.c
    floatunsitf.c
    floatuntidf.c
    floatuntisf.c
    gcc_personality_v0.c
    int_util.c
    lshrti3.c
    modti3.c
    muldc3.c
    muloti4.c
    mulsc3.c
    multf3.c
    mulxc3.c
    popcountdi2.c
    subtf3.c
    trunctfdf2.c
    trunctfsf2.c
    udivmodti4.c
    udivti3.c
    umodti3.c
)

IF (OS_DARWIN)
    SRCS(os_version_check.c)
ENDIF()

IF (ARCH_ARM)
    SRCS(
        clear_cache.c
        multc3.c
    )
ENDIF()

END()
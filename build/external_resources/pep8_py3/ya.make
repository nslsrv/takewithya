RESOURCES_LIBRARY()



IF (HOST_OS_DARWIN AND HOST_ARCH_X86_64 OR
    HOST_OS_LINUX AND HOST_ARCH_PPC64LE OR
    HOST_OS_LINUX AND HOST_ARCH_X86_64 OR
    HOST_OS_WINDOWS AND HOST_ARCH_X86_64)
ELSE()
    MESSAGE(FATAL_ERROR Unsupported host platform for PEP8_PY3)
ENDIF()

DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE(
    PEP8_PY3
    sbr:1387597678 FOR DARWIN
    sbr:1589288410 FOR LINUX-PPC64LE
    sbr:1387598201 FOR LINUX
    sbr:1387597915 FOR WIN32
)

END()
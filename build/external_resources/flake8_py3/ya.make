RESOURCES_LIBRARY()



IF (HOST_OS_DARWIN AND HOST_ARCH_X86_64 OR
    HOST_OS_LINUX AND HOST_ARCH_PPC64LE OR
    HOST_OS_LINUX AND HOST_ARCH_X86_64 OR
    HOST_OS_WINDOWS AND HOST_ARCH_X86_64)
ELSE()
    MESSAGE(FATAL_ERROR Unsupported host platform for FLAKE8_PY3)
ENDIF()

DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE(
    FLAKE8_PY3
    sbr:1648592411 FOR DARWIN
    sbr:1648592117 FOR LINUX-PPC64LE
    sbr:1648593284 FOR LINUX
    sbr:1648592802 FOR WIN32
)

END()
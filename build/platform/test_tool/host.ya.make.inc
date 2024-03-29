IF (HOST_OS_DARWIN AND HOST_ARCH_X86_64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:1896752304)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:1896630446)
ELSEIF (HOST_OS_LINUX AND HOST_ARCH_PPC64LE)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:1896751875)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:1896629941)
ELSEIF (HOST_OS_LINUX AND HOST_ARCH_X86_64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:1896752616)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:1896631241)
ELSEIF (HOST_OS_WINDOWS AND HOST_ARCH_X86_64)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL_HOST sbr:1896751387)
    DECLARE_EXTERNAL_RESOURCE(TEST_TOOL3_HOST sbr:1896629161)
ENDIF()

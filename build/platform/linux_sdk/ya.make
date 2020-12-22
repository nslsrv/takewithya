RESOURCES_LIBRARY()



NO_PLATFORM_RESOURCES()

SET(NEED_PLATFORM_PEERDIRS no)

IF (OS_SDK STREQUAL "local")
    # Implementation is in $S/build/ymake.core.conf
ELSEIF (ARCH_X86_64)
    IF (OS_SDK STREQUAL "ubuntu-10")
        DECLARE_EXTERNAL_RESOURCE(OS_SDK_ROOT sbr:244388930)
    ELSEIF (OS_SDK STREQUAL "ubuntu-12")
        DECLARE_EXTERNAL_RESOURCE(OS_SDK_ROOT sbr:244387436)
    ELSEIF (OS_SDK STREQUAL "ubuntu-14")
        DECLARE_EXTERNAL_RESOURCE(OS_SDK_ROOT sbr:1679152626)
    ELSEIF (OS_SDK STREQUAL "ubuntu-16")
        DECLARE_EXTERNAL_RESOURCE(OS_SDK_ROOT sbr:243881345)
    ELSEIF (OS_SDK STREQUAL "ubuntu-18")
        DECLARE_EXTERNAL_RESOURCE(OS_SDK_ROOT sbr:617908641)
    ELSEIF (OS_SDK STREQUAL "ubuntu-20")
        DECLARE_EXTERNAL_RESOURCE(OS_SDK_ROOT sbr:1889744010)
    ELSE()
        MESSAGE(FATAL_ERROR "There is no ${OS_SDK} SDK for x86-64")
    ENDIF()
ELSEIF (ARCH_AARCH64)
    IF (OS_SDK STREQUAL "ubuntu-16")
        DECLARE_EXTERNAL_RESOURCE(OS_SDK_ROOT sbr:309054781)
    ELSE()
        MESSAGE(FATAL_ERROR "There is no ${OS_SDK} SDK for aarch64/armv8 64 bit")
    ENDIF()
ELSEIF (ARCH_PPC64LE)
    IF (OS_SDK STREQUAL "ubuntu-14")
        IF (HOST_ARCH_PPC64LE)
            DECLARE_EXTERNAL_RESOURCE(OS_SDK_ROOT sbr:1570528338)
        ELSE()
            DECLARE_EXTERNAL_RESOURCE(OS_SDK_ROOT sbr:233217651)
        ENDIF()
    ELSE()
        MESSAGE(FATAL_ERROR "There is no ${OS_SDK} SDK for PPC64LE")
    ENDIF()
ELSEIF (ARCH_ARM7)
    IF (OS_SDK STREQUAL "ubuntu-16")
        DECLARE_EXTERNAL_RESOURCE(OS_SDK_ROOT sbr:1323200692)
    ELSE()
        MESSAGE(FATAL_ERROR "There is no ${OS_SDK} SDK for ")
    ENDIF()
ELSE()
    MESSAGE(FATAL_ERROR "Unexpected OS_SDK value: ${OS_SDK}")
ENDIF()

END()
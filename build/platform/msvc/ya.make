RESOURCES_LIBRARY()



IF (WINDOWS_KITS_VERSION STREQUAL "10.0.10586.0")
    DECLARE_EXTERNAL_RESOURCE(WINDOWS_KITS sbr:544779014)
ELSEIF (WINDOWS_KITS_VERSION STREQUAL "10.0.16299.0")
    DECLARE_EXTERNAL_RESOURCE(WINDOWS_KITS sbr:1379398385)
ELSE()
    MESSAGE(FATAL_ERROR "We have no Windows Kits version ${WINDOWS_KITS_VERSION}")
ENDIF()

IF (CLANG_CL)
    DECLARE_EXTERNAL_RESOURCE(MSVC_FOR_CLANG sbr:1383387533)  # Microsoft Visual C++ 2017 14.16.27023 (15.9.5)
ENDIF()

END()
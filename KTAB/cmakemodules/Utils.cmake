## set CMAKE_REQUIRED_INCLUDES variable before calling this macro
## ex: set(CMAKE_REQUIRED_INCLUDES ${LOGGER_INCLUDE_DIR})
macro(CHECK_INCLUDE_FILE_CXX_ERROR INCLUDE_FILE)
    include(CheckIncludeFileCXX)
    UNSET(HAVE_FILE CACHE)
    CHECK_INCLUDE_FILE_CXX(${INCLUDE_FILE} HAVE_FILE)
    IF(NOT HAVE_FILE)
        message( SEND_ERROR "${INCLUDE_FILE} not found at ${CMAKE_REQUIRED_INCLUDES}")
    ENDIF()
endmacro()
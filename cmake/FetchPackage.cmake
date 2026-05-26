# cmake_policy(SET CMP0135 NEW)

include(FetchContent)

# Use FetchContent to download and populate a package without calling add_subdirectory().
# This is used for downloading prebuilt external binaries.
macro(FetchPackage name)
    cmake_parse_arguments(FETCH "" "URL;URL_HASH" "" ${ARGN})
    if(NOT FETCH_URL)
        message(FATAL_ERROR "FetchPackage(${name}) requires URL")
    endif()
    set(FETCH_URL_HASH_ARG "")
    if(FETCH_URL_HASH)
        if(NOT FETCH_URL_HASH MATCHES "^SHA256=")
            message(FATAL_ERROR "FetchPackage(${name}) URL_HASH must use SHA256=<digest>")
        endif()
        string(REGEX REPLACE "^SHA256=" "" FETCH_URL_HASH_DIGEST "${FETCH_URL_HASH}")
        string(LENGTH "${FETCH_URL_HASH_DIGEST}" FETCH_URL_HASH_DIGEST_LENGTH)
        if(NOT FETCH_URL_HASH_DIGEST_LENGTH EQUAL 64 OR NOT FETCH_URL_HASH_DIGEST MATCHES "^[0-9a-fA-F]+$")
            message(FATAL_ERROR "FetchPackage(${name}) URL_HASH must be a 64-character SHA-256 digest")
        endif()
        set(FETCH_URL_HASH_ARG URL_HASH "${FETCH_URL_HASH}")
    endif()
    # Only attach the GitHub token to requests targeting github.com (or its subdomains)
    # so the credential is not sent to third-party hosts (nuget.org, developer.nvidia.com,
    # developer.apple.com, etc.) for FetchPackage downloads.
    set(FETCH_HTTP_HEADER_ARG "")
    if(SLANG_GITHUB_TOKEN)
        string(REGEX MATCH "^https?://([^/]+)/" _fetch_url_host_match "${FETCH_URL}")
        set(FETCH_URL_HOST "${CMAKE_MATCH_1}")
        if(FETCH_URL_HOST MATCHES "(^|\\.)github\\.com$")
            set(FETCH_HTTP_HEADER_ARG HTTP_HEADER "Authorization: token ${SLANG_GITHUB_TOKEN}")
        endif()
    endif()
    FetchContent_Declare(
        ${name}
        URL "${FETCH_URL}"
        ${FETCH_URL_HASH_ARG}
        SOURCE_SUBDIR _does_not_exist_ # avoid adding contained CMakeLists.txt
        ${FETCH_HTTP_HEADER_ARG}
    )
    FetchContent_GetProperties(${name})
    if(NOT ${name}_POPULATED)
        FetchContent_MakeAvailable(${name})
    endif()
endmacro()

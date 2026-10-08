cmake_minimum_required (VERSION 3.10)

option(BUILD_TESTS "Build the test suite (unit + integration)" ON)
option(ENABLE_CLIENT "Enable client code to be built" ON)
option(LINK_ABSL "explicitely link abseil components" ON)

project (oatpp-socketio)

# this is overriden with the git revision further down the line..
set (oatpp-socketio_VERSION_MAJOR 0)
set (oatpp-socketio_VERSION_MINOR 1)
set (oatpp-socketio_VERSION_REVISION 1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(CMAKE_COLOR_MAKEFILE ON)
SET(CMAKE_VERBOSE_MAKEFILE OFF)


################################################################################
# Cmake module definitions
################################################################################

set(${PROJECT}_LIBRARIES ${PROJECT})

################################################################################
# Cmake modules
################################################################################

#Including local extra cmake modules
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

#Function for generation of export macros for libraries
include(GenerateExportHeader) 

# Include module with fuction 'write_basic_package_version_file'
include(CMakePackageConfigHelpers)

# Build dir for generated config, export and package files
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated/include")


################################################################################
# Install options
################################################################################
MESSAGE(STATUS "CMAKE_INSTALL_PREFIX: " ${CMAKE_INSTALL_PREFIX})

set(INSTALL_LIB_DIR lib/ CACHE PATH "Installation directory for libraries")
set(INSTALL_BIN_DIR bin/ CACHE PATH "Installation directory for executables")
set(INSTALL_INCLUDE_DIR include/${PROJECT_NAME} CACHE PATH "Installation directory for header files")
if(WIN32 AND NOT CYGWIN)
  set(DEF_INSTALL_CMAKE_DIR cmake)
else()
  set(DEF_INSTALL_CMAKE_DIR lib/cmake/${PROJECT_NAME})
endif()
set(INSTALL_CMAKE_DIR ${DEF_INSTALL_CMAKE_DIR} CACHE PATH "Installation directory for CMake files")

################################################################################
#Version stuff
################################################################################

# Make a version file containing the current version from git.
include(GetGitRevisionDescription)
git_describe(VERSION --tag --long)

##parse the version information into pieces.
string(REGEX REPLACE "^v([0-9]+)\\..*" "\\1" PROJECT_VERSION_MAJOR "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.([0-9]+).*" "\\1" PROJECT_VERSION_MINOR "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" PROJECT_VERSION_PATCH "${VERSION}")
string(REGEX REPLACE "^v[0-9]+\\.[0-9]+\\.[0-9]+(.*)" "\\1" PROJECT_VERSION_TWEAK "${VERSION}")
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(PROJECT_DESCRIPTION "OAT++ socket.io library")

set(PROJECT_VERSION_FULL "${PROJECT_VERSION}${PROJECT_VERSION_TWEAK}")

message (STATUS "version: " ${VERSION} )
message (STATUS ${PROJECT_NAME}" version: " ${PROJECT_VERSION_FULL} )

# Generate Config file y_config.h
# set alternative variable instead of the project name containting a -
set(PROJ_NAME OATPP_SOCKETIO)
set(config_h_in "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}_config.h.in")
set(config_h "${generated_dir}/include/${PROJECT_NAME}/${PROJECT_NAME}_config.h")
configure_file("${config_h_in}" "${config_h}")

list(APPEND headers ${config_h})
install(
   FILES ${config_h}
   DESTINATION "${INSTALL_INCLUDE_DIR}/common"
)

################################################################################
# Install Stuff
################################################################################

#######################################
# Config cmake
#######################################
set(version_config "${generated_dir}/${PROJECT_NAME}ConfigVersion.cmake") #output
set(project_config "${generated_dir}/${PROJECT_NAME}Config.cmake") #output

#Targets name
#   * ${INSTALL_CMAKE_DIR}/${targets_export_name}.cmake
set(targets_export_name "${PROJECT_NAME}Targets")
set(namespace "${PROJECT_NAME}::")

install(
    EXPORT "${targets_export_name}"
    NAMESPACE "${namespace}"
    DESTINATION "${INSTALL_CMAKE_DIR}/"
)

# Set version required to config
write_basic_package_version_file(
    "${version_config}"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Configure '<PROJECT-NAME>Config.cmake'
configure_package_config_file("cmake/Config.cmake.in"
    "${project_config}"
    INSTALL_DESTINATION "${INSTALL_CMAKE_DIR}"
)

# Install config
install(
    FILES "${project_config}" "${version_config}"
    DESTINATION "${INSTALL_CMAKE_DIR}"
)

install(
    EXPORT "${targets_export_name}"
    NAMESPACE "${namespace}"
    DESTINATION "${INSTALL_CMAKE_DIR}/"
)


INSTALL( PROGRAMS ${binaries}
    DESTINATION ${INSTALL_BIN_DIR}
)

################################################################################
#
################################################################################

#
# needed for architecture-specific extensions like gpio trigger etc.
#
if(UNIX AND NOT APPLE)
    set(LINUX TRUE)
    add_definitions(-DLINUX=1 )
endif()

# OAT++
find_package(oatpp          1.4.0 REQUIRED)
find_package(oatpp-swagger  1.4.0 REQUIRED)
find_package(oatpp-websocket  1.4.0 REQUIRED)

include_directories("${oatpp-swagger_INCLUDE_DIRS}" ${oatpp_INCLUDE_DIRS})
link_directories(${oatpp_LIBRARIES_DIRS})
link_directories(${oatpp-swagger_LIBRARIES_DIRS})
link_directories(${oatpp-websocket_LIBRARIES_DIRS})

list(APPEND LIBS oatpp::oatpp 
    oatpp::oatpp-test 
    oatpp::oatpp-swagger 
    oatpp::oatpp-websocket
    )

message("OAT INC " ${oatpp_INCLUDE_DIRS})
message("OAT INC " ${oatpp-swagger_INCLUDE_DIRS})

add_definitions(
        ## define path to swagger-ui static resources folder
        -DOATPP_SWAGGER_RES_PATH="${oatpp-swagger_INCLUDE_DIRS}/../bin/oatpp-swagger/res"
)

################################################################################

list(APPEND DEFINITIONS -DEIGEN_DONT_VECTORIZE -DEIGEN_DISABLE_UNALIGNED_ARRAY_ASSERT -DEIGEN_SUPPORT_STAGE=99)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/api ${CMAKE_CURRENT_SOURCE_DIR}/include )

if(CMAKE_BUILD_TYPE MATCHES Release)
    message("RELEASE BUILD")
    add_definitions(-O2 -funroll-loops )
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
    message("DEBUG BUILD")
    add_definitions(-Wall -ggdb -O0 )
    # add_definitions( -Weffc++ )
endif()


add_subdirectory(src)

add_subdirectory(app)

################################################################################
# tests
################################################################################

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

install(DIRECTORY "include/" DESTINATION ${INSTALL_INCLUDE_DIR})

install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${targets_export_name} 
    LIBRARY
        DESTINATION "${INSTALL_LIB_DIR}" COMPONENT library
#        DESTINATION "${INSTALL_LIB_DIR}" COMPONENT library INCLUDES NAMELINK_SKIP
    ARCHIVE
        DESTINATION lib
    RUNTIME
        DESTINATION "${INSTALL_BIN_DIR}" COMPONENT bin
    INCLUDES
        DESTINATION "${INSTALL_INCLUDE_DIR}"
    #We create only the include folder. Our headers have a specfic folder hierarchy
    PUBLIC_HEADER
        DESTINATION "${INSTALL_INCLUDE_DIR}" COMPONENT headers
)

# This file is based off of the Platform/Darwin.cmake and Platform/UnixPaths.cmake
# files which are included with CMake 2.8.4
# It has been altered for iOS development

# Options:
#
# IOS_PLATFORM = OS (default) or SIMULATOR
#   This decides if SDKS will be selected from the iPhoneOS.platform or iPhoneSimulator.platform folders
#   OS - the default, used to build for iPhone and iPad physical devices, which have an arm arch.
#   SIMULATOR - used to build for the Simulator platforms, which have an x86 arch.
#
# CMAKE_IOS_DEVELOPER_ROOT = automatic(default) or /path/to/platform/Developer folder
#   By default this location is automatcially chosen based on the IOS_PLATFORM value above.
#   If set manually, it will override the default location and force the user of a particular Developer Platform
#
# CMAKE_IOS_SDK_ROOT = automatic(default) or /path/to/platform/Developer/SDKs/SDK folder
#   By default this location is automatcially chosen based on the CMAKE_IOS_DEVELOPER_ROOT value.
#   In this case it will always be the most up-to-date SDK found in the CMAKE_IOS_DEVELOPER_ROOT path.
#   If set manually, this will force the use of a specific SDK version

# Macros:
#
# set_xcode_property (TARGET XCODE_PROPERTY XCODE_VALUE)
#  A convenience macro for setting xcode specific properties on targets
#  example: set_xcode_property (myioslib IPHONEOS_DEPLOYMENT_TARGET "3.1")
#
# find_host_package (PROGRAM ARGS)
#  A macro used to find executable programs on the host system, not within the iOS environment.
#  Thanks to the android-cmake project for providing the command

# Standard settings
set (CMAKE_SYSTEM_NAME Darwin)
set (CMAKE_SYSTEM_VERSION 1)
set (UNIX True)
set (APPLE True)
set (IOS True)
set (APPLE_IOS True)

# make sure all executables are bundles otherwise try compiles will fail
set (CMAKE_MACOSX_BUNDLE True)
set (CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer" CACHE STRING "how to sign executables")

set( CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD gnu++0x )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY libc++ )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_BOOL_CONVERSION YES )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_CONSTANT_CONVERSION YES )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_DIRECT_OBJC_ISA_USAGE YES_ERROR )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_EMPTY_BODY YES )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_ENUM_CONVERSION YES )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_INT_CONVERSION YES )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_OBJC_ROOT_CLASS YES_ERROR )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_WARN_UNREACHABLE_CODE YES )
set( CMAKE_XCODE_ATTRIBUTE_CLANG_WARN__DUPLICATE_METHOD_MATCH YES )
set( CMAKE_XCODE_ATTRIBUTE_ENABLE_STRICT_OBJC_MSGSEND YES )
set( CMAKE_XCODE_ATTRIBUTE_ENABLE_TESTABILITY YES )
set( CMAKE_XCODE_ATTRIBUTE_GCC_C_LANGUAGE_STANDARD gnu99 )
set( CMAKE_XCODE_ATTRIBUTE_GCC_DYNAMIC_NO_PIC NO )
set( CMAKE_XCODE_ATTRIBUTE_GCC_NO_COMMON_BLOCKS YES )
set( CMAKE_XCODE_ATTRIBUTE_GCC_WARN_64_TO_32_BIT_CONVERSION YES )
set( CMAKE_XCODE_ATTRIBUTE_GCC_WARN_ABOUT_RETURN_TYPE YES_ERROR )
set( CMAKE_XCODE_ATTRIBUTE_GCC_WARN_UNDECLARED_SELECTOR YES )
set( CMAKE_XCODE_ATTRIBUTE_GCC_WARN_UNINITIALIZED_AUTOS YES_AGGRESSIVE )
set( CMAKE_XCODE_ATTRIBUTE_GCC_WARN_UNUSED_FUNCTION YES )
set( CMAKE_XCODE_ATTRIBUTE_GCC_WARN_UNUSED_VARIABLE YES )
set( CMAKE_XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2" )

# Required as of cmake 2.8.10
set (CMAKE_OSX_DEPLOYMENT_TARGET "" CACHE STRING "Force unset of the deployment target for iOS" FORCE)

# Determine the cmake host system version so we know where to find the iOS SDKs
find_program (CMAKE_UNAME uname /bin /usr/bin /usr/local/bin)
if (CMAKE_UNAME)
	execute_process(
		COMMAND uname -r
		OUTPUT_VARIABLE CMAKE_HOST_SYSTEM_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
  string (REGEX REPLACE "^([0-9]+)\\.([0-9]+).*$" "\\1" DARWIN_MAJOR_VERSION "${CMAKE_HOST_SYSTEM_VERSION}")
endif ()

# Force the compilers to gcc for iOS
include (CMakeForceCompiler)
set (CMAKE_C_COMPILER /usr/bin/clang)
set (CMAKE_CXX_COMPILER /usr/bin/clang++)
set(CMAKE_AR ar CACHE FILEPATH "" FORCE)

# Skip the platform compiler checks for cross compiling
#set (CMAKE_CXX_COMPILER_WORKS TRUE)
#set (CMAKE_C_COMPILER_WORKS TRUE)

# All iOS/Darwin specific settings - some may be redundant
set (CMAKE_SHARED_LIBRARY_PREFIX "lib")
set (CMAKE_SHARED_LIBRARY_SUFFIX ".dylib")
set (CMAKE_SHARED_MODULE_PREFIX "lib")
set (CMAKE_SHARED_MODULE_SUFFIX ".so")
set (CMAKE_MODULE_EXISTS 1)
set (CMAKE_DL_LIBS "")

set (CMAKE_C_OSX_COMPATIBILITY_VERSION_FLAG "-compatibility_version ")
set (CMAKE_C_OSX_CURRENT_VERSION_FLAG "-current_version ")
set (CMAKE_CXX_OSX_COMPATIBILITY_VERSION_FLAG "${CMAKE_C_OSX_COMPATIBILITY_VERSION_FLAG}")
set (CMAKE_CXX_OSX_CURRENT_VERSION_FLAG "${CMAKE_C_OSX_CURRENT_VERSION_FLAG}")

# Hidden visibilty is required for cxx on iOS
set (CMAKE_C_FLAGS_INIT "")
# use of CMAKE_OSX_SYSROOT is fine here even though it is set later on because this string
# is evaluated after at the end of the generate step where is has been set
#set (CMAKE_CXX_FLAGS_INIT "-fvisibility=hidden -fvisibility-inlines-hidden -isysroot ${CMAKE_OSX_SYSROOT}")
set (CMAKE_CXX_FLAGS_INIT "-fvisibility=hidden -fvisibility-inlines-hidden")

set (CMAKE_C_LINK_FLAGS "-Wl,-search_paths_first ${CMAKE_C_LINK_FLAGS}")
set (CMAKE_CXX_LINK_FLAGS "-Wl,-search_paths_first ${CMAKE_CXX_LINK_FLAGS}")

set (CMAKE_PLATFORM_HAS_INSTALLNAME 1)
set (CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "-dynamiclib -headerpad_max_install_names")
set (CMAKE_SHARED_MODULE_CREATE_C_FLAGS "-bundle -headerpad_max_install_names")
set (CMAKE_SHARED_MODULE_LOADER_C_FLAG "-Wl,-bundle_loader,")
set (CMAKE_SHARED_MODULE_LOADER_CXX_FLAG "-Wl,-bundle_loader,")
set (CMAKE_FIND_LIBRARY_SUFFIXES ".dylib" ".so" ".a")

# hack: if a new cmake (which uses CMAKE_INSTALL_NAME_TOOL) runs on an old build tree
# (where install_name_tool was hardcoded) and where CMAKE_INSTALL_NAME_TOOL isn't in the cache
# and still cmake didn't fail in CMakeFindBinUtils.cmake (because it isn't rerun)
# hardcode CMAKE_INSTALL_NAME_TOOL here to install_name_tool, so it behaves as it did before, Alex
if (NOT DEFINED CMAKE_INSTALL_NAME_TOOL)
  find_program(CMAKE_INSTALL_NAME_TOOL install_name_tool)
endif ()

# Setup iOS platform unless specified manually with IOS_PLATFORM
if (NOT DEFINED IOS_PLATFORM)
  set (IOS_PLATFORM "OS")
endif ()
set (IOS_PLATFORM ${IOS_PLATFORM} CACHE STRING "Type of iOS Platform")

# Check the platform selection and setup for developer root
if (${IOS_PLATFORM} STREQUAL "OS")
  set (IOS_PLATFORM_LOCATION "iPhoneOS.platform")

  # This causes the installers to properly locate the output libraries
  set (CMAKE_XCODE_EFFECTIVE_PLATFORMS "-iphoneos")
elseif (${IOS_PLATFORM} STREQUAL "SIMULATOR")
  set (SIMULATOR true)
  set (IOS_PLATFORM_LOCATION "iPhoneSimulator.platform")

  # This causes the installers to properly locate the output libraries
  set (CMAKE_XCODE_EFFECTIVE_PLATFORMS "-iphonesimulator")
elseif (${IOS_PLATFORM} STREQUAL "SIMULATOR64")
  set (SIMULATOR true)
  set (IOS_PLATFORM_LOCATION "iPhoneSimulator.platform")

  # This causes the installers to properly locate the output libraries
  set (CMAKE_XCODE_EFFECTIVE_PLATFORMS "-iphonesimulator")
else ()
  message (FATAL_ERROR "Unsupported IOS_PLATFORM value selected. Please choose OS or SIMULATOR")
endif ()

# Setup iOS developer location unless specified manually with CMAKE_IOS_DEVELOPER_ROOT
# Note Xcode 4.3 changed the installation location, choose the most recent one available
#exec_program(/usr/bin/xcode-select ARGS -print-path OUTPUT_VARIABLE CMAKE_XCODE_DEVELOPER_DIR)
#set (XCODE_POST_43_ROOT "${CMAKE_XCODE_DEVELOPER_DIR}/Platforms/${IOS_PLATFORM_LOCATION}/Developer")
set (XCODE_POST_43_ROOT "/Applications/Xcode.app/Contents/Developer/Platforms/${IOS_PLATFORM_LOCATION}/Developer")
set (XCODE_PRE_43_ROOT "/Developer/Platforms/${IOS_PLATFORM_LOCATION}/Developer")
if (NOT DEFINED CMAKE_IOS_DEVELOPER_ROOT)
  if (EXISTS ${XCODE_POST_43_ROOT})
    set (CMAKE_IOS_DEVELOPER_ROOT ${XCODE_POST_43_ROOT})
  elseif(EXISTS ${XCODE_PRE_43_ROOT})
    set (CMAKE_IOS_DEVELOPER_ROOT ${XCODE_PRE_43_ROOT})
  endif ()
endif ()
set (CMAKE_IOS_DEVELOPER_ROOT ${CMAKE_IOS_DEVELOPER_ROOT} CACHE PATH "Location of iOS Platform")

# Find and use the most recent iOS sdk unless specified manually with CMAKE_IOS_SDK_ROOT
if (NOT DEFINED CMAKE_IOS_SDK_ROOT)
  file (GLOB _CMAKE_IOS_SDKS "${CMAKE_IOS_DEVELOPER_ROOT}/SDKs/*")
  if (_CMAKE_IOS_SDKS)
    list (SORT _CMAKE_IOS_SDKS)
    list (REVERSE _CMAKE_IOS_SDKS)
    list (GET _CMAKE_IOS_SDKS 0 CMAKE_IOS_SDK_ROOT)
  else ()
    message (FATAL_ERROR "No iOS SDK's found in default search path ${CMAKE_IOS_DEVELOPER_ROOT}. Manually set CMAKE_IOS_SDK_ROOT or install the iOS SDK.")
  endif ()
  message (STATUS "Toolchain using default iOS SDK: ${CMAKE_IOS_SDK_ROOT}")
endif ()
set (CMAKE_IOS_SDK_ROOT ${CMAKE_IOS_SDK_ROOT} CACHE PATH "Location of the selected iOS SDK")

# Set the sysroot default to the most recent SDK
#set (CMAKE_OSX_SYSROOT ${CMAKE_IOS_SDK_ROOT} CACHE PATH "Sysroot used for iOS support")
set (CMAKE_OSX_SYSROOT iphoneos)

# set the architecture for iOS
# NOTE: Currently both ARCHS_STANDARD_32_BIT and ARCHS_UNIVERSAL_IPHONE_OS set armv7 only, so set both manually
if (${IOS_PLATFORM} STREQUAL "OS")
  set (IOS_ARCH armv7 armv7s arm64)
elseif (${IOS_PLATFORM} STREQUAL "SIMULATOR")
  set (IOS_ARCH i386)
elseif (${IOS_PLATFORM} STREQUAL "SIMULATOR64")
  set (IOS_ARCH x86_64)
endif ()

#set (CMAKE_OSX_ARCHITECTURES ${IOS_ARCH} CACHE STRING "Build architecture for iOS")

# Set the find root to the iOS developer roots and to user defined paths
set (CMAKE_FIND_ROOT_PATH ${CMAKE_IOS_DEVELOPER_ROOT} ${CMAKE_IOS_SDK_ROOT} ${CMAKE_PREFIX_PATH} CACHE STRING "iOS find search path root")

# default to searching for frameworks first
set (CMAKE_FIND_FRAMEWORK FIRST)

# set up the default search directories for frameworks
set (CMAKE_SYSTEM_FRAMEWORK_PATH
  ${CMAKE_IOS_SDK_ROOT}/System/Library/Frameworks
  ${CMAKE_IOS_SDK_ROOT}/System/Library/PrivateFrameworks
  ${CMAKE_IOS_SDK_ROOT}/Developer/Library/Frameworks
)

# only search the iOS sdks, not the remainder of the host filesystem
set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)


# This little macro lets you set any XCode specific property
macro (set_xcode_property TARGET XCODE_PROPERTY XCODE_VALUE)
  set_property (TARGET ${TARGET} PROPERTY XCODE_ATTRIBUTE_${XCODE_PROPERTY} ${XCODE_VALUE})
endmacro ()


# This macro lets you find executable programs on the host system
macro (find_host_package)
  set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
  set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
  set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)
  set (IOS FALSE)

  find_package(${ARGN})

  set (IOS TRUE)
  set (CMAKE_FIND_ROOT_PATH_MODE_PROGRAM ONLY)
  set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
  set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
endmacro ()

cmake_minimum_required(VERSION 2.8.5)

project(Xdmf)


if(WIN32)
  ADD_DEFINITIONS(-D_CRT_NONSTDC_NO_DEPRECATE)
  ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS)
endif()

set(XDMF_LIBNAME "Xdmf" CACHE STRING "Name for this xdmf library to avoid collision")

set(XDMF_MAJOR_VERSION 3)
set(XDMF_MINOR_VERSION 0)
set(XDMF_PATCH_VERSION 0)
set(XDMF_VERSION "${XDMF_MAJOR_VERSION}.${XDMF_MINOR_VERSION}.${XDMF_PATCH_VERSION}")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_INSTALL_BINDIR}")

if(BUILD_SHARED_LIBS)
  set(BUILD_SHARED 1)
endif()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/XdmfConfig.hpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/XdmfConfig.hpp)

# Enable CMake testing
option(XDMF_BUILD_TESTING "Build Tests" OFF)
if(XDMF_BUILD_TESTING)
  enable_testing()
  include(CTest)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
list(APPEND CMAKE_MODULE_PATH
  ${CMAKE_CURRENT_SOURCE_DIR}/CMake
  ${CMAKE_CURRENT_SOURCE_DIR}/CMake/VersionSuite)
if(XDMF_BUILD_TESTING)
  list(APPEND CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/CMake/TestingSuite)
endif()

include(XdmfFunctions)

# Should we build with documentation
option(XDMF_BUILD_DOCUMENTATION OFF)
if(XDMF_BUILD_DOCUMENTATION)
  add_subdirectory(doc)
endif()

# Wrapper Setup
option(XDMF_WRAP_PYTHON OFF)
option(XDMF_WRAP_JAVA OFF)

#check for TIFF
find_package(TIFF)
if (TIFF_FOUND)
  add_definitions(-DXDMF_BUILD_TIFF)
  include_directories(${TIFF_INCLUDE_DIR})
endif (TIFF_FOUND)

# Test for DSM
option(XDMF_BUILD_DSM OFF)
mark_as_advanced(XDMF_BUILD_DSM)
if(XDMF_BUILD_DSM)
  find_package(MPI REQUIRED)
  if(MPI_FOUND)
    include_directories(${MPI_INCLUDE_PATH})
    # The Intel compiler requires an extra tag for CXX_FLAGS in order to properly build.
    if (CMAKE_CXX_COMPILER_ID MATCHES "Intel")
      add_definitions(-DMPICH_IGNORE_CXX_SEEK)
    endif ()
  endif()
  option(XDMF_BUILD_DSM_THREADS ON)
  if (XDMF_WRAP_PYTHON)
    find_path(PYTHON_INCLUDE_MPI4PY_DIR
      NAMES mpi4py/mpi4py.i
      DOC   "Directory containing the mpi4py/mpi4py.i file")
  endif ()
  add_definitions(-DXDMF_BUILD_DSM)
endif()

# If we are wrapping either, we need swig
if(XDMF_WRAP_PYTHON OR XDMF_WRAP_JAVA)
  find_package(SWIG 2.0.0 REQUIRED)
  include(${SWIG_USE_FILE})
endif()

# If we are wrapping python, let's include it in the top level
if(XDMF_WRAP_PYTHON)
  find_package(PythonInterp ${REQUESTED_PYTHON_VERSION} REQUIRED)
  find_package(PythonLibs ${REQUESTED_PYTHON_VERSION} REQUIRED)
  if (XDMF_BUILD_DSM)
    find_package (MPI4PY REQUIRED)
    if (MPI4PY_FOUND)
      if ("${PYTHON_INCLUDE_MPI4PY_DIR}" STREQUAL "")
        set(PYTHON_INCLUDE_MPI4PY_DIR ${MPI4PY_INCLUDE_DIR})
      endif ()
    endif ()
    include_directories(${PYTHON_INCLUDE_DIRS} ${MPI_CXX_INCLUDE_PATH} ${PYTHON_INCLUDE_MPI4PY_DIR})
  else ()
    include_directories(${PYTHON_INCLUDE_DIRS})
  endif ()

  # A macro to swig and create the python files
  # Since we essentually do the same thing inside and outside core, it
  # would be easier as a macro
  # Parameters:
  #     python_name = the name of the intended target to be wrapped
  #     ${ARGN} = any dependencies needed by the target
  # Output:
  #     Target Name = the output target name will have ${python_name} as its name
  macro(XDMF_SWIG_PYTHON python_name)
    set(CMAKE_SWIG_OUTDIR ${CMAKE_BINARY_DIR})
    # optimize swig generation --- these are all flags corresponding to -O
    # except -fvirtual which breaks visitor operation
    set(CMAKE_SWIG_FLAGS ${XDMF_SWIG_FLAGS} -modern -fastdispatch -nosafecstrings -noproxydel -fastproxy -fastinit -fastunpack -fastquery -modernargs -nobuildnone)
    #Enables DSM
    if (XDMF_BUILD_DSM)
      set(CMAKE_SWIG_FLAGS ${CMAKE_SWIG_FLAGS} -DXDMF_BUILD_DSM)
      if (XDMF_BUILD_DSM_THREADS)
        set(CMAKE_SWIG_FLAGS ${CMAKE_SWIG_FLAGS} -DXDMF_BUILD_DSM_THREADS)
      endif ()
    endif ()
    if (TIFF_FOUND)
      set(CMAKE_SWIG_FLAGS ${CMAKE_SWIG_FLAGS} -DXDMF_BUILD_TIFF)
    endif ()
    set_source_files_properties(${python_name}.i PROPERTIES CPLUSPLUS ON)
    set(swig_extra_generated_files "")
    swig_add_module(${python_name}Python python ${python_name}.i)

    if (NOT APPLE)
      swig_link_libraries(${python_name}Python ${python_name} ${PYTHON_LIBRARIES})
    else ()
      swig_link_libraries(${python_name}Python ${python_name})
    endif ()

    set_property(DIRECTORY APPEND PROPERTY
      ADDITIONAL_MAKE_CLEAN_FILES
      ${CMAKE_CURRENT_BINARY_DIR}/${python_name}.pyc
    )

    if("${ARGN}" STRGREATER "")
      add_dependencies(${python_name} "${ARGN}")
    endif()

    if(APPLE)
      set_target_properties(${SWIG_MODULE_${python_name}Python_REAL_NAME}
        PROPERTIES
        LINK_FLAGS "-undefined dynamic_lookup")
    endif()

    set_target_properties(${SWIG_MODULE_${python_name}Python_REAL_NAME}
      PROPERTIES
      OUTPUT_NAME "_${python_name}")

    set(
      PYTHON_INSTALL_DIR
			${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/python${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}/xdmf
      )

    string(TOUPPER ${python_name} python_name_upper)
    set(${python_name_upper}_PYTHON
      ${PYTHON_INSTALL_DIR}/${python_name}.py)
    get_directory_property(${python_name}Parent PARENT_DIRECTORY)
    if(NOT "${${python_name}Parent}" STREQUAL "")
      set(${python_name_upper}_PYTHON ${${python_name_upper}_PYTHON}
        PARENT_SCOPE)
    endif()

    install(FILES ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${python_name}.py
      DESTINATION ${PYTHON_INSTALL_DIR})
    install(TARGETS ${SWIG_MODULE_${python_name}Python_REAL_NAME}
      DESTINATION ${PYTHON_INSTALL_DIR})
  endmacro()
endif()

# If we are wrapping java, let's include it in the top level
if(XDMF_WRAP_JAVA)
  find_package(Java REQUIRED)
  find_package(JNI REQUIRED)
  include_directories(${CMAKE_CURRENT_SOURCE_DIR}
    ${JAVA_INCLUDE_PATH}
    ${JAVA_INCLUDE_PATH2})

  # A macro to swig and create the jar files
  # Since we essentually do the same thing inside and outside core, it
  # would be easier as a macro
  # Parameters:
  #     java_name = the name of the intended target to be wrapped
  #     ${ARGN} = any dependencies needed by the target
  # Output:
  #     Target Name     = the output target name will have ${java_name}Java as its name
  #     Target Jar      = the output target jar will have ${java_name}Java.jar as its name
  macro(XDMF_SWIG_JAVA java_name)
    set(${java_name}_JAVA_JAR ${CMAKE_BINARY_DIR}/${java_name}.jar)
    set(XDMF_JAVA_PACKAGE_DIR mil/army/arl/xdmf)
    set(XDMF_JAVA_DIR ${CMAKE_BINARY_DIR}/${XDMF_JAVA_PACKAGE_DIR})
    set(CMAKE_SWIG_OUTDIR ${XDMF_JAVA_DIR})
    set(CMAKE_SWIG_FLAGS ${XDMF_SWIG_FLAGS} -v -make_default -package mil.army.arl.xdmf)
    set_source_files_properties(${java_name}.i PROPERTIES CPLUSPLUS ON)
    set(swig_extra_generated_files "")
    swig_add_module(${java_name}Java java ${java_name}.i)
    swig_link_libraries(${java_name}Java ${java_name})

    if(WIN32)
      set(java_files *.java)
      set(java_dest "bin")
      set(separator "\;")
    else()
      set(java_files ${XDMF_JAVA_DIR}/*.java)
      set(java_dest "${CMAKE_INSTALL_LIBDIR}/java")
      set(separator ":")
    endif()

    add_custom_target(${java_name}_Java_Dir ALL DEPENDS ${XDMF_JAVA_DIR})
    add_custom_command(OUTPUT ${XDMF_JAVA_DIR}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${XDMF_JAVA_DIR})
    add_custom_target(Compiled_${java_name}_Jar ALL DEPENDS
      ${${java_name}_JAVA_JAR})
    add_custom_command(OUTPUT ${${java_name}_JAVA_JAR}
      COMMAND ${CMAKE_COMMAND} -E chdir ${XDMF_JAVA_PACKAGE_DIR}
              ${JAVA_COMPILE} -cp \".${java_cp_jars}\" ${java_files}
      COMMAND ${JAVA_ARCHIVE} -cvf ${${java_name}_JAVA_JAR}
             "${XDMF_JAVA_PACKAGE_DIR}/*.class")
    add_dependencies(${java_name}Java ${java_name}_Java_Dir)
    add_dependencies(Compiled_${java_name}_Jar ${java_name}Java)

    if(NOT ${ARGN} EQUAL "")
      add_dependencies(Compiled_${java_name}_Jar "${ARGN}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY
      ADDITIONAL_MAKE_CLEAN_FILES
      ${CMAKE_CURRENT_BINARY_DIR}/${java_name}.jar
      ${XDMF_JAVA_DIR})

    set(java_cp_jars "${java_cp_jars}${separator}${${java_name}_JAVA_JAR}")
    string(TOUPPER ${java_name} java_name_upper)
    set(${java_name_upper}_JAVA_JAR
      ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/java/${java_name}.jar)
    get_directory_property(${java_name}Parent PARENT_DIRECTORY)
    if(NOT "${${java_name}Parent}" STREQUAL "")
      set(${java_name_upper}_JAVA_JAR ${${java_name_upper}_JAVA_JAR}
        PARENT_SCOPE)
      set(java_cp_jars "${java_cp_jars}" PARENT_SCOPE)
    endif()

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${java_name}.jar
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/java)
    install(TARGETS ${SWIG_MODULE_${java_name}Java_REAL_NAME}
      DESTINATION ${java_dest})
  endmacro()
endif()

include(GNUInstallDirs)

add_subdirectory(core)
include_directories(${XdmfCore_INCLUDE_DIRS} ${XdmfDSM_INCLUDE_DIRS})

option(XDMF_BUILD_CORE_ONLY OFF)
mark_as_advanced(XDMF_BUILD_CORE_ONLY)
if(NOT XDMF_BUILD_CORE_ONLY)
  set(XdmfSources
    XdmfAggregate
    XdmfAttribute
    XdmfAttributeCenter
    XdmfAttributeType
    XdmfCurvilinearGrid
    XdmfDomain
    XdmfGeometry
    XdmfGeometryType
    XdmfGraph
    XdmfGrid
    XdmfGridCollection
    XdmfGridCollectionType
    XdmfGridController
    XdmfGridTemplate
    XdmfItemFactory
    XdmfMap
    XdmfReader
    XdmfRectilinearGrid
    XdmfRegularGrid
    XdmfSet
    XdmfSetType
    XdmfTemplate
    XdmfTime
    XdmfTopology
    XdmfTopologyType
    XdmfUnstructuredGrid)

  add_library(${XDMF_LIBNAME} ${XdmfSources})
  SET_TARGET_PROPERTIES(
    ${XDMF_LIBNAME} PROPERTIES
    VERSION ${XDMF_VERSION}
    SOVERSION ${XDMF_MAJOR_VERSION}
    )
  if(XDMF_BUILD_DSM)
    target_link_libraries(${XDMF_LIBNAME} XdmfCore XdmfDSM)
  else(XDMF_BUILD_DSM)
    target_link_libraries(${XDMF_LIBNAME} XdmfCore)
  endif(XDMF_BUILD_DSM)

  if(WIN32)
    if (BUILD_SHARED_LIBS)
      set_target_properties(${XDMF_LIBNAME} PROPERTIES
        DEFINE_SYMBOL XDMF_EXPORTS)
    endif ()
  endif()

  if(XDMF_WRAP_JAVA)
    XDMF_SWIG_JAVA(${XDMF_LIBNAME} Compiled_XdmfCore_Jar)
  endif()

  if(XDMF_WRAP_PYTHON)
    if (NOT BUILD_SHARED_LIBS)
      message(FATAL_ERROR "Python Wrappers do not function"
                          " properly without shared libraries")
    endif (NOT BUILD_SHARED_LIBS)
    if (XDMF_BUILD_DSM)
      XDMF_SWIG_PYTHON(${XDMF_LIBNAME} XdmfCore XdmfDSM)
    else()
      XDMF_SWIG_PYTHON(${XDMF_LIBNAME} XdmfCore)
    endif()
    set(
      XDMF_PYTHON_INSTALL_DIR
      ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/python${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}/xdmf
      )
  endif()

  if(XDMF_BUILD_TESTING)
    add_subdirectory(tests)
  endif()

  option(XDMF_BUILD_UTILS OFF)
  if(XDMF_BUILD_UTILS)
    add_subdirectory(utils)
  endif()

  file(GLOB XdmfHeaders
    "*.hpp"
    "*.tpp"
    "*.i"
    "${CMAKE_CURRENT_BINARY_DIR}/*.hpp"
  )
  install(FILES ${XdmfHeaders} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(TARGETS ${XDMF_LIBNAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

if(WIN32 AND NOT CYGWIN)
  set(DEF_INSTALL_CMAKE_DIR CMake)
else()
  set(DEF_INSTALL_CMAKE_DIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
endif()
xdmf_create_config_file(${PROJECT_NAME})
install(FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
  DESTINATION ${DEF_INSTALL_CMAKE_DIR})

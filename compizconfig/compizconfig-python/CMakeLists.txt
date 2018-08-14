# Hack
set (CMAKE_PROJECT_NAME compizconfig-python)

# Get arch
execute_process (COMMAND uname -p OUTPUT_VARIABLE PROC_ARCH)
string (REPLACE "\n" "" PROC_ARCH ${PROC_ARCH})

find_package (PythonInterp 3.0 REQUIRED)
find_package (PythonLibs 3.0 REQUIRED)

set (COMPIZCONFIG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/compizconfig/libcompizconfig/include)

set (PY_VERSION "${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}")
set (PY_BUILD_DIR lib.linux-${PROC_ARCH}-${PY_VERSION})
set (PY_SCRIPTS_DIR scripts-${PY_VERSION})
set (PY_COMPIZCONFIG_SRC ${CMAKE_CURRENT_BINARY_DIR}/compizconfig.c)
set (PY_COMPIZCONFIG_CYTHON ${CMAKE_CURRENT_SOURCE_DIR}/src/compizconfig.pyx)

find_program(CYTHON_BIN cython3)
if(NOT CYTHON_BIN)
  message(FATAL_ERROR "Can not build because cython binary not found.")
endif()

add_custom_command (OUTPUT ${PY_COMPIZCONFIG_SRC}
		    COMMAND ${CYTHON_BIN} -o ${PY_COMPIZCONFIG_SRC} ${PY_COMPIZCONFIG_CYTHON}
		    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		    DEPENDS ${PY_COMPIZCONFIG_CYTHON})

file (READ ${CMAKE_SOURCE_DIR}/VERSION COMPIZ_RELEASE_VERSION LIMIT 12 OFFSET 0)
string (STRIP ${COMPIZ_RELEASE_VERSION} COMPIZ_RELEASE_VERSION)
set (VERSION ${COMPIZ_RELEASE_VERSION})

include_directories (${PYTHON_INCLUDE_DIRS}
		     ${COMPIZCONFIG_INCLUDE_DIRS})

# cython generates code that does type-punning, this trips
# up -Werror
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-strict-aliasing")

# We need to remove the "lib"
set (CMAKE_SHARED_LIBRARY_PREFIX "")

# We must build the python module using CMake instead of distutils.
#
# The only way in which the python module can be built using distutils
# is by using add_custom_command to "generate" it into existence.
# Unfortunately this has drawbacks. Namely, it is not possible for
# add_custom_command to depend on library targets. Because of that, there's
# no way that we can guarantee that libcompizconfig.so will be linked
# before compizconfig.so (which is required). The best that we can do
# there is to use add_custom_target, and make that target depend on the
# file, 
add_library (compizconfig_python_module SHARED
	     ${PY_COMPIZCONFIG_SRC})

set_target_properties (compizconfig_python_module
		       PROPERTIES
		       OUTPUT_NAME
		       compizconfig)

target_link_libraries (compizconfig_python_module
		       ${PYTHON_LIBRARIES}
		       compizconfig)

set (UNINSTALL_COMMAND "${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/setup.py uninstall --prefix=${CMAKE_INSTALL_PREFIX} --version=${VERSION}")

compiz_add_code_to_uninstall_target (${UNINSTALL_COMMAND} ${CMAKE_CURRENT_BINARY_DIR})

# This is probably not very portable, but we can't install using distutils
# as we cannot build with it safely

if ($ENV{COMPIZ_DEB_BUILD})
    set (INSTALL_LAYOUT_DIR dist-packages)
else ($ENV{COMPIZ_DEB_BUILD})
    set (INSTALL_LAYOUT_DIR site-packages)
endif ($ENV{COMPIZ_DEB_BUILD})

set (PYTHON_MODULE_DIR ${CMAKE_INSTALL_PREFIX}/lib/python${PY_VERSION}/${INSTALL_LAYOUT_DIR})

install (TARGETS compizconfig_python_module
	 DESTINATION ${PYTHON_MODULE_DIR})

install (CODE
	 "message (\"Installing python files\")
	  execute_process (COMMAND cmake -DPY_EXECUTABLE=${PYTHON_EXECUTABLE} -DSETUP=${CMAKE_CURRENT_SOURCE_DIR}/setup.py -DPREFIX=${CMAKE_INSTALL_PREFIX} -DWDIR=${CMAKE_CURRENT_BINARY_DIR} -DVERSION=${VERSION} -P ${compiz_SOURCE_DIR}/compizconfig/cmake/exec_setup_py_with_destdir.cmake)")

if (COMPIZ_BUILD_TESTING)

        find_program (BASH_EXECUTABLE bash)
	mark_as_advanced (FORCE PYTHON_EXECUTABLE)

	file (GLOB PYTHON_TESTS "tests/test_*.py")

        if (PYTHON_EXECUTABLE AND
            BASH_EXECUTABLE)
		set (MOCK_XML_SRC ${CMAKE_CURRENT_SOURCE_DIR}/tests/mock.xml)
		set (MOCK_XML_FILE ${CMAKE_CURRENT_BINARY_DIR}/mock.xml)

		add_custom_command (OUTPUT ${MOCK_XML_FILE}
				    COMMAND cp ${MOCK_XML_SRC}
					    ${MOCK_XML_FILE}
				    COMMENT "Copying mock.xml file into testing directory"
				    DEPENDS ${MOCK_XML_SRC})

		add_custom_target (compizconfig_python_test_mock_xml_file ALL DEPENDS
				   ${MOCK_XML_FILE})

		# For testing purposes we need the ini backend
		add_dependencies (compizconfig_python_module ini compizconfig_python_test_mock_xml_file)

		foreach (PYTHON_TEST_FILE ${PYTHON_TESTS})
			get_filename_component (PYTHON_TEST ${PYTHON_TEST_FILE} NAME_WE)
			message (STATUS "Adding test " ${PYTHON_TEST})
                        add_test (NAME CompizConfigPython.${PYTHON_TEST}
                                  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                                  COMMAND ${BASH_EXECUTABLE}
                                  ${CMAKE_SOURCE_DIR}/tests/util/run_with_ld_library_path.sh
                                  ${CMAKE_BINARY_DIR}/compizconfig/libcompizconfig/src
                                  ${PYTHON_EXECUTABLE} ${PYTHON_TEST_FILE})
		endforeach (PYTHON_TEST_FILE)
        endif (PYTHON_EXECUTABLE AND
               BASH_EXECUTABLE)

endif (COMPIZ_BUILD_TESTING)

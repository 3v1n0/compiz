set (_config_file
     config)

configure_file (${CMAKE_CURRENT_SOURCE_DIR}/config_test
		${CMAKE_CURRENT_BINARY_DIR}/compiz-1/compizconfig/config)

install (FILES ${_config_file} DESTINATION ${COMPIZCONFIG_CONFIG_DIR})

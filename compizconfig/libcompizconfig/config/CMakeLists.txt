set (_config_file
     config)

configure_file (${CMAKE_CURRENT_SOURCE_DIR}/config_test
		${CMAKE_CURRENT_BINARY_DIR}/compiz-1/compizconfig/config)

install (FILES ${_config_file} DESTINATION ${COMPIZ_SYSCONFDIR}/compizconfig)

add_custom_target (
    config_install
    ${CMAKE_COMMAND} -E make_directory ${CMAKE_ROOT}/Modules &&
    ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/config/${_config_file} ${COMPIZ_SYSCONFDIR}/compizconfig/${_config_file}
    )

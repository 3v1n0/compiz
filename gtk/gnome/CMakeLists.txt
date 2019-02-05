if (BUILD_GNOME)
    set (desktop_file ${CMAKE_CURRENT_BINARY_DIR}/compiz.desktop)
    compiz_translate_desktop_file (
        ${CMAKE_CURRENT_SOURCE_DIR}/compiz.desktop.in
        ${desktop_file}
    )
    install (
	FILES ${desktop_file}
	DESTINATION ${datadir}/applications
    )

    set (_keybindings_files
	 50-compiz-navigation.xml
	 50-compiz-windows.xml)

    set (_keybindings_files_translated "")

    foreach (_keybinding_file ${_keybindings_files})
	compiz_translate_xml (${CMAKE_CURRENT_SOURCE_DIR}/${_keybinding_file}.in
			      ${CMAKE_CURRENT_BINARY_DIR}/${_keybinding_file} NOTRANSLATIONS)

	list (APPEND _keybindings_files_translated ${CMAKE_CURRENT_BINARY_DIR}/${_keybinding_file})
    endforeach (_keybinding_file ${keybinding_files})

    add_custom_target (compiz-gnome-keybindings ALL DEPENDS
		       ${_keybindings_files_translated})

    install (
	FILES ${_keybindings_files_translated}
	DESTINATION ${datadir}/gnome-control-center/keybindings
    )

    add_custom_target (compiz.desktop-file ALL DEPENDS ${desktop_file})
endif (BUILD_GNOME)

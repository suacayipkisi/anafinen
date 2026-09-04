include(GNUInstallDirs)

install(TARGETS anafinen
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/assets"
    )
    install(FILES "${ANAFINEN_GENERATED_ASSETS_DIR}/icons/anafinen.png"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/assets/icons"
    )
    install(FILES "${GMSH_DLL}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    
    if(CMAKE_CROSSCOMPILING)
        file(GLOB MINGW_EXTRA_DLLS
            "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgomp-*.dll"
            "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libwinpthread-*.dll"
            "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libpng*.dll"
            "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/zlib*.dll"
        )
        install(FILES ${MINGW_EXTRA_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
else()
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/anafinen/assets"
    )
    install(FILES "${ANAFINEN_GENERATED_ASSETS_DIR}/icons/anafinen.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/anafinen/assets/icons"
    )
endif()

if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/anafinen.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/anafinen.svg"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
    install(FILES "${ANAFINEN_GENERATED_ASSETS_DIR}/icons/anafinen.png"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
    )
endif()

set(CPACK_PACKAGE_NAME "anafinen")
set(CPACK_PACKAGE_VENDOR "Ufuk Deniz Konuk")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_RELEASE "1")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "3D FEM Dynamic and Modal Analysis Engine")
set(CPACK_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-alpha")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-windows-${CMAKE_SYSTEM_PROCESSOR}-alpha")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "RPM;TGZ")
    set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
    set(CPACK_RPM_PACKAGE_RELEASE "1.alpha")
    set(CPACK_RPM_PACKAGE_LICENSE "GPLv3+")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
    set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}~alpha1")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)

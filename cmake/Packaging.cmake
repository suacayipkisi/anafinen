include(GNUInstallDirs)

if(WIN32)
    install(TARGETS anafinen
        RUNTIME DESTINATION .
    )

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION "assets"
    )
    if(ANAFINEN_ICON_PNG)
        install(FILES "${ANAFINEN_ICON_PNG}"
            DESTINATION "assets/icons"
        )
    endif()
    install(FILES "${GMSH_DLL}"
        DESTINATION .
    )

    # GPLv3 sections 4-6: every copy ships the license text and the third-party notices
    # (the ZIP also redistributes GPL components such as the Gmsh DLL).
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.md"
        DESTINATION .
    )

    # use MinGW DLL if croscompile on linux
    if(CMAKE_CROSSCOMPILING)
        install(FILES
            "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgomp-1.dll"
            "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libwinpthread-1.dll"
            "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libpng16-16.dll"
            "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/zlib1.dll"
            "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libgcc_s_seh-1.dll"
            "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libstdc++-6.dll"
            DESTINATION .
        )
    endif()
else()
    install(TARGETS anafinen
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    # The desktop entry is installed once, under applications/ (see below).
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/anafinen/assets"
        PATTERN "anafinen.desktop" EXCLUDE
    )
    if(ANAFINEN_ICON_PNG)
        install(FILES "${ANAFINEN_ICON_PNG}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/anafinen/assets/icons"
        )
    endif()
endif()

if(UNIX AND NOT APPLE)
    # GPLv3 sections 4-6: license text and third-party notices in /usr/share/doc/anafinen.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.md"
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/anafinen.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/anafinen.svg"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
    if(ANAFINEN_ICON_PNG)
        install(FILES "${ANAFINEN_ICON_PNG}"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
        )
    endif()
endif()

set(CPACK_PACKAGE_NAME "anafinen")
set(CPACK_PACKAGE_VENDOR "Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_RELEASE "1")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "3D FEM Analysis Engine")
set(CPACK_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CONTACT "Abdurrahman Konuk (Ufuk Deniz Konuk) <konuki8523@gmail.com>") # required by the DEB generator
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
    if(ANAFINEN_HAS_CHOLMOD)
        set(CPACK_RPM_PACKAGE_REQUIRES "suitesparse")
    endif()
    set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}~alpha1")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    # File > Import / Export needs a native dialog helper at run time.
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "zenity | kdialog")
    set(CPACK_RPM_PACKAGE_SUGGESTS "zenity")
endif()

# Packages must run on any x86-64 CPU. A build tree configured with
# ANAFINEN_NATIVE_OPTIMIZATIONS=ON (-march=native / /arch:AVX2) is refused by cpack.
if(ANAFINEN_NATIVE_OPTIMIZATIONS)
    set(ANAFINEN_CPACK_NATIVE_GUARD "${CMAKE_CURRENT_BINARY_DIR}/cpackRejectNativeBuild.cmake")
    file(WRITE "${ANAFINEN_CPACK_NATIVE_GUARD}"
        "message(FATAL_ERROR \"This build uses ANAFINEN_NATIVE_OPTIMIZATIONS=ON (CPU-specific code). \"\n"
        "  \"Reconfigure with -DANAFINEN_NATIVE_OPTIMIZATIONS=OFF before packaging.\")\n")
    set(CPACK_PRE_BUILD_SCRIPTS "${ANAFINEN_CPACK_NATIVE_GUARD}")
    message(WARNING "ANAFINEN_NATIVE_OPTIMIZATIONS=ON: the binary only runs on CPUs like this one; cpack is disabled for this build tree.")
endif()

include(CPack)

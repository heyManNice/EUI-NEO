# DevTools panel module.
#
# The panel implementation is compiled where the tooling seam exists: in debug
# configurations, or wherever EUI_TOOLING is turned on. A build without the seam
# links a module with no DevTools code in it. Applications opt in by keeping a
# modules::devtools::Session alive; see README.md.

# The panel sources follow the tooling seam, not the configuration: the same switch
# that gives the core its seam builds the tool that uses it.
set(EUI_DEVTOOLS_PANEL "$<OR:$<CONFIG:Debug>,$<BOOL:${EUI_TOOLING}>>")

add_library(eui_neo_module_devtools
    devtools.h
    devtools.cpp
    devtools_theme.h
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_host.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_host.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_ui.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_ui.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_elements.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_elements.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_properties.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_properties.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_performance.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_performance.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_preview.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_preview.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_tree.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_tree.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_fields.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/devtools_fields.h>
)
add_library(eui::module_devtools ALIAS eui_neo_module_devtools)
set_target_properties(eui_neo_module_devtools PROPERTIES EXPORT_NAME module_devtools)

target_link_libraries(eui_neo_module_devtools PUBLIC eui::neo)
eui_apply_compile_options(eui_neo_module_devtools)

if(EUI_ENABLE_INSTALL)
    install(TARGETS eui_neo_module_devtools
        EXPORT EuiNeoTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

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
    theme.h
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/host.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/host.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/ui.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/ui.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/elements.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/elements.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/properties.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/properties.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/performance.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/performance.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/scale.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/scale.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/state.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/state.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/input.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/input.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/mcp.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/mcp.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/mcp_vision.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/mcp_vision.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/mcp_semantic.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/mcp_semantic.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/mcp_action.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/mcp_action.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/mcp_server.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/mcp_server.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/preview.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/preview.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/tree.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/tree.h>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/fields.cpp>
    $<${EUI_DEVTOOLS_PANEL}:${CMAKE_CURRENT_LIST_DIR}/fields.h>
)
add_library(eui::module_devtools ALIAS eui_neo_module_devtools)
set_target_properties(eui_neo_module_devtools PROPERTIES EXPORT_NAME module_devtools)

target_link_libraries(eui_neo_module_devtools PUBLIC eui::neo)
if(TARGET png_static)
    target_link_libraries(eui_neo_module_devtools PRIVATE png_static)
endif()
eui_apply_compile_options(eui_neo_module_devtools)

if(EUI_ENABLE_INSTALL)
    install(TARGETS eui_neo_module_devtools
        EXPORT EuiNeoTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

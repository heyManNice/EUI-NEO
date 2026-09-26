# DevTools panel module.
#
# The panel implementation is compiled for Debug configurations only, so a
# Release build links a module with no DevTools code in it. Applications opt in
# by keeping a modules::devtools::Session alive; see README.md.

add_library(eui_neo_module_devtools
    devtools.h
    devtools.cpp
    devtools_theme.h
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_LIST_DIR}/devtools_host.cpp>
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_LIST_DIR}/devtools_host.h>
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_LIST_DIR}/devtools_ui.cpp>
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_LIST_DIR}/devtools_ui.h>
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_LIST_DIR}/devtools_elements.cpp>
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_LIST_DIR}/devtools_elements.h>
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_LIST_DIR}/devtools_performance.cpp>
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_LIST_DIR}/devtools_performance.h>
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

if(EUI_BUILD_TEST_FIXTURES)
    add_executable(devtools_viewer fixtures/devtools_viewer.cpp)
    eui_neo_configure_app(devtools_viewer)
    target_link_libraries(devtools_viewer PRIVATE eui::module_devtools)

    add_executable(devtools_host tests/devtools_host.cpp)
    target_link_libraries(devtools_host PRIVATE eui::module_devtools)
    eui_apply_compile_options(devtools_host)
    add_test(NAME devtools_host COMMAND devtools_host)
    set_tests_properties(devtools_host PROPERTIES LABELS "unit")
endif()

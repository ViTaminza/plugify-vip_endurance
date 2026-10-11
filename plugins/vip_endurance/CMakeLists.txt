# vip_endurance — Plugify C++ plugin: port of the Pisex VIP module "[VIP] Endurance".
#
#   plugify-vip_endurance  the plugin shared library (entry "bin/vip_endurance" -> bin/libvip_endurance.so)

set(VIP_ENDURANCE_VERSION "1.0.0")

# Binding globals the plugify module fills from the plugins declared as dependencies:
#   __s2sdk_* / __hudmenu_* (s2sdk_bindings, hudmenu_bindings - both come in through
#   pisexcompat's PUBLIC link) and __vip_* (vip_bindings, the generated consumer bindings of
#   plugins/vip).
if(NOT TARGET vip_bindings)
    message(FATAL_ERROR "vip_endurance needs the generated vip consumer bindings (plugins/vip/CMakeLists.txt, target vip_bindings)")
endif()

add_library(plugify-vip_endurance SHARED
        src/plugin.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
        $<TARGET_OBJECTS:vip_bindings>
)
target_include_directories(plugify-vip_endurance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated vip/s2sdk/plg/pisexcompat headers must not fail -Werror.
target_include_directories(plugify-vip_endurance SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/plugins/vip/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/sdk/include"
        "${CMAKE_SOURCE_DIR}/libs/pisexcompat/include")
target_link_libraries(plugify-vip_endurance PRIVATE
        pisexcompat
        -static-libstdc++ -static-libgcc
)
target_compile_options(plugify-vip_endurance PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(plugify-vip_endurance PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="vip_endurance"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(plugify-vip_endurance PROPERTIES OUTPUT_NAME vip_endurance)

if(UNIX AND NOT APPLE)
    target_link_options(plugify-vip_endurance PRIVATE
            "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/sym/version_script.lds")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/vip_endurance.pplugin.in"
        "${CMAKE_CURRENT_BINARY_DIR}/vip_endurance.pplugin"
)

# --- tests --------------------------------------------------------------------
# Engine-free logic tests; run by ./test.sh (build/tests).
add_executable(vip_endurance_tests tests/vip_endurance_tests.cpp)
target_include_directories(vip_endurance_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(vip_endurance_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(vip_endurance_tests PRIVATE _GLIBCXX_USE_CXX11_ABI=1)
target_link_libraries(vip_endurance_tests PRIVATE -static-libstdc++ -static-libgcc)
set_target_properties(vip_endurance_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

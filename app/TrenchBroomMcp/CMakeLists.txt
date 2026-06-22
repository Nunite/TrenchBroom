add_executable(trenchbroom-mcp)

target_sources(trenchbroom-mcp PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp
)

target_link_libraries(trenchbroom-mcp
  PRIVATE
    CompilerConfig
    PrecompileQtHeaders
    PrecompileStdHeaders
    Qt6::Core
    Qt6::Network
    TbMcpLib
)

set_target_properties(trenchbroom-mcp PROPERTIES AUTOMOC TRUE)

if(WIN32)
    if(NOT TB_SKIP_WINDEPLOYQT)
        message(STATUS "windeployqt requested")

        get_target_property(TB_QMAKE_PATH Qt6::qmake IMPORTED_LOCATION)
        string(REPLACE "qmake" "windeployqt" TB_WINDEPLOYQT_PATH "${TB_QMAKE_PATH}")
        message(STATUS "windeployqt found: ${TB_WINDEPLOYQT_PATH}")

        add_custom_command(TARGET trenchbroom-mcp POST_BUILD
                COMMAND "${TB_WINDEPLOYQT_PATH}"
                        --no-compiler-runtime
                        --no-translations
                        "$<TARGET_FILE_DIR:trenchbroom-mcp>")
    else()
        message(STATUS "windeployqt skipped")
    endif()
endif()

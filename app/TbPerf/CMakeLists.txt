add_executable(TbPerf)

target_sources(TbPerf PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp
)

target_link_libraries(TbPerf
  PRIVATE
    CompilerConfig
    PrecompileStdHeaders
    Qt6::Core
    TbMcpLib
)

if(WIN32)
    if(NOT TB_SKIP_WINDEPLOYQT)
        message(STATUS "windeployqt requested")

        get_target_property(TB_QMAKE_PATH Qt6::qmake IMPORTED_LOCATION)
        string(REPLACE "qmake" "windeployqt" TB_WINDEPLOYQT_PATH "${TB_QMAKE_PATH}")
        message(STATUS "windeployqt found: ${TB_WINDEPLOYQT_PATH}")

        add_custom_command(TARGET TbPerf POST_BUILD
                COMMAND "${TB_WINDEPLOYQT_PATH}"
                        --no-compiler-runtime
                        --no-translations
                        "$<TARGET_FILE_DIR:TbPerf>")
    else()
        message(STATUS "windeployqt skipped")
    endif()
endif()

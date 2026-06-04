add_executable(TbRenderLibTest)

target_sources(TbRenderLibTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_AllocationTracker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_BrushOutlineColor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tst_SkyRenderer.cpp
)

add_compile_definitions(CATCH_CONFIG_ENABLE_ALL_STRINGMAKERS=1)

target_link_libraries(TbRenderLibTest
  PRIVATE
    CompilerConfig
    PrecompileStdHeaders
    Catch2::Catch2WithMain
    TbMdlTestUtilsLib
    TbRenderLib
  )

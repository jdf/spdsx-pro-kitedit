install(
    TARGETS spdsx-patchedit_exe
    RUNTIME COMPONENT spdsx-patchedit_Runtime
)

if(PROJECT_IS_TOP_LEVEL)
  include(CPack)
endif()

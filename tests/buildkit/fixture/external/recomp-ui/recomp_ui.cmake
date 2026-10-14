# Stub recomp-ui: the two commands the host build probes for.
function(recomp_target_launcher_ui tgt)
    target_sources(${tgt} PRIVATE
        "${RECOMP_UI_DIR}/src/runtime_ui_imgui.cpp"
        "${RECOMP_UI_DIR}/src/common/recomp_runtime_settings.c")
endfunction()
if(FIXTURE_STACK_COMMAND)
    function(target_link_gbarecomp_runtime_stack tgt)
        target_link_libraries(${tgt} PRIVATE gbarecomp_gba gbarecomp_runtime)
    endfunction()
endif()

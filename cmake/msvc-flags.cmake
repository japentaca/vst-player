# Common MSVC flags for project targets (not applied to JUCE internals beyond what JUCE sets).
function(vstplayer_set_msvc_flags target)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
endfunction()

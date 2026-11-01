# Fetches JUCE (pinned tag) via FetchContent and makes juce_add_gui_app etc. available.
include(FetchContent)

set(VSTPLAYER_JUCE_TAG "8.0.15" CACHE STRING "JUCE git tag to fetch")

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${VSTPLAYER_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

FetchContent_MakeAvailable(JUCE)

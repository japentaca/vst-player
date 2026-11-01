#pragma once

#include <juce_core/juce_core.h>

namespace vstplayer
{

// "Recientes" list (most recent first, max. 10), stored in <user data dir>\recent.json:
//   { "version": 1, "recent": [ "C:\\...\\a.vstproj.json", ... ] }
// With an empty file path the list lives only in memory (self-tests without a data dir).
class RecentProjects
{
public:
    explicit RecentProjects (juce::File storageFile);

    const juce::StringArray& getPaths() const noexcept { return paths; }
    juce::File getFile (int index) const;
    int size() const noexcept { return paths.size(); }

    void add (const juce::File& projectFile); // moves it to the top
    void remove (const juce::File& projectFile);
    void clear();

    juce::File getStorageFile() const { return storage; }

    static constexpr int kMaxEntries = 10;

private:
    void load();
    void save() const;

    juce::File storage;
    juce::StringArray paths;
};

} // namespace vstplayer

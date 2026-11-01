#include "project/RecentProjects.h"

namespace vstplayer
{

RecentProjects::RecentProjects (juce::File storageFile)
    : storage (std::move (storageFile))
{
    load();
}

juce::File RecentProjects::getFile (int index) const
{
    return juce::isPositiveAndBelow (index, paths.size()) ? juce::File (paths[index]) : juce::File();
}

void RecentProjects::add (const juce::File& projectFile)
{
    const auto path = projectFile.getFullPathName();
    paths.removeString (path, true);
    paths.insert (0, path);

    while (paths.size() > kMaxEntries)
        paths.remove (paths.size() - 1);

    save();
}

void RecentProjects::remove (const juce::File& projectFile)
{
    paths.removeString (projectFile.getFullPathName(), true);
    save();
}

void RecentProjects::clear()
{
    paths.clear();
    save();
}

void RecentProjects::load()
{
    paths.clear();

    if (storage == juce::File() || ! storage.existsAsFile())
        return;

    const auto json = juce::JSON::parse (storage.loadFileAsString());

    if (auto* list = json["recent"].getArray())
        for (auto& entry : *list)
            if (entry.isString() && entry.toString().isNotEmpty() && ! paths.contains (entry.toString(), true)
                && paths.size() < kMaxEntries)
                paths.add (entry.toString());
}

void RecentProjects::save() const
{
    if (storage == juce::File())
        return;

    auto root = juce::DynamicObject::Ptr (new juce::DynamicObject());
    root->setProperty ("version", 1);

    juce::Array<juce::var> list;
    for (auto& p : paths)
        list.add (p);

    root->setProperty ("recent", list);

    storage.getParentDirectory().createDirectory();
    const auto text = juce::JSON::toString (juce::var (root.get()), juce::JSON::FormatOptions {}.withSpacing (juce::JSON::Spacing::multiLine));

    juce::TemporaryFile temp (storage);

    if (temp.getFile().replaceWithText (text + "\n", false, false, "\n"))
        temp.overwriteTargetFileWithTemporary();
}

} // namespace vstplayer

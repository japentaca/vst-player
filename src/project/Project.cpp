#include "project/Project.h"

namespace vstplayer::project
{

const InstanceData* Project::findInstance (const juce::String& id) const
{
    for (auto& instance : instances)
        if (instance.id == id)
            return &instance;

    return nullptr;
}

juce::uint16 channelsToMask (const std::vector<int>& channels)
{
    juce::uint16 mask = 0;

    for (auto ch : channels)
        if (ch >= 1 && ch <= 16)
            mask = (juce::uint16) (mask | (1u << (ch - 1)));

    return mask;
}

std::vector<int> maskToChannels (juce::uint16 mask)
{
    std::vector<int> channels;

    for (int ch = 1; ch <= 16; ++ch)
        if ((mask & (1u << (ch - 1))) != 0)
            channels.push_back (ch);

    return channels;
}

juce::String nameFromFile (const juce::File& file)
{
    auto name = file.getFileName();

    if (name.endsWithIgnoreCase (kFileSuffix))
        return name.dropLastCharacters ((int) juce::String (kFileSuffix).length());

    return file.getFileNameWithoutExtension();
}

juce::File withProjectSuffix (const juce::File& file)
{
    if (file.getFileName().endsWithIgnoreCase (kFileSuffix))
        return file;

    // "foo.json" -> "foo.vstproj.json"; "foo" -> "foo.vstproj.json".
    auto base = file.getFileName();

    if (base.endsWithIgnoreCase (".json"))
        base = base.dropLastCharacters (5);

    return file.getSiblingFile (base + kFileSuffix);
}

} // namespace vstplayer::project

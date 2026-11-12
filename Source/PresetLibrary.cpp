#include "PresetLibrary.h"
#include "PluginProcessor.h"

namespace rb
{
namespace
{
    const juce::Identifier kRoot { "RoomBreakerPreset" };
    const juce::Identifier kUserProperty { "presetUser" }, kNameProperty { "presetName" };

    juce::String makeHaystack (const PresetEntry& e)
    {
        return (e.name + " " + e.category + " " + e.tags.joinIntoString (" ") + " " + e.description).toLowerCase();
    }
}

PresetLibrary::PresetLibrary (RoomBreakerAudioProcessor& p)
    : processor (p), userFolder (defaultUserFolder())
{
    rescan();
}

juce::File PresetLibrary::defaultUserFolder()
{
    // Application Support rather than Documents: macOS asks the user's permission before a
    // plug-in may touch ~/Documents, and hosts show that prompt in awkward places.
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support/RoomBreaker/Presets");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("RoomBreaker/Presets");
   #endif
}

void PresetLibrary::setUserFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (lock);
        userFolder = folder;
    }
    rescan();
}

juce::File PresetLibrary::getUserFolder() const
{
    const juce::ScopedLock sl (lock);
    return userFolder;
}

void PresetLibrary::rescan()
{
    std::vector<PresetEntry> list;
    const auto& factory = getFactoryPresets();
    for (size_t i = 0; i < factory.size(); ++i)
    {
        PresetEntry e;
        e.name = factory[i].name;
        e.category = factory[i].category;
        e.description = factory[i].description;
        e.tags = juce::StringArray::fromTokens (factory[i].tags, ",", {});
        e.tags.trim();
        e.tags.removeEmptyStrings();
        e.factoryIndex = (int) i;
        e.haystack = makeHaystack (e);
        list.push_back (std::move (e));
    }

    const auto folder = getUserFolder();
    std::vector<PresetEntry> user;
    if (folder.isDirectory())
    {
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension))
        {
            PresetEntry e;
            Values unused {};
            if (readUserFile (f, e, unused))
                user.push_back (std::move (e));
        }
        std::sort (user.begin(), user.end(), [] (const PresetEntry& a, const PresetEntry& b)
                   { return a.name.compareNatural (b.name) < 0; });
    }
    for (auto& e : user)
        list.push_back (std::move (e));

    const juce::ScopedLock sl (lock);
    // Keep the current preset pointing at the same thing after the list changes.
    juce::File currentFile;
    int currentFactory = -1;
    if (currentIndex >= 0 && currentIndex < (int) entries.size())
    {
        currentFile = entries[(size_t) currentIndex].file;
        currentFactory = entries[(size_t) currentIndex].factoryIndex;
    }
    entries = std::move (list);
    currentIndex = -1;
    for (size_t i = 0; i < entries.size(); ++i)
        if ((currentFactory >= 0 && entries[i].factoryIndex == currentFactory)
            || (currentFactory < 0 && currentFile != juce::File() && entries[i].file == currentFile))
            currentIndex = (int) i;
}

int PresetLibrary::size() const
{
    const juce::ScopedLock sl (lock);
    return (int) entries.size();
}

PresetEntry PresetLibrary::entry (int index) const
{
    const juce::ScopedLock sl (lock);
    return juce::isPositiveAndBelow (index, (int) entries.size()) ? entries[(size_t) index] : PresetEntry {};
}

std::vector<int> PresetLibrary::search (const juce::String& text, const juce::StringArray& tags, const juce::String& category) const
{
    auto words = juce::StringArray::fromTokens (text.toLowerCase(), " \t", "\"");
    words.removeEmptyStrings();

    const juce::ScopedLock sl (lock);
    std::vector<int> byName, byOther;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        if (category.isNotEmpty() && e.category != category)
            continue;
        bool ok = true;
        for (const auto& t : tags)
            ok = ok && e.tags.contains (t);
        bool inName = ! words.isEmpty();
        for (const auto& w : words)
        {
            ok = ok && e.haystack.contains (w);
            inName = inName && e.name.toLowerCase().contains (w);
        }
        if (ok)
            (inName ? byName : byOther).push_back ((int) i);
    }
    byName.insert (byName.end(), byOther.begin(), byOther.end());
    return byName;
}

juce::StringArray PresetLibrary::categories() const
{
    const juce::ScopedLock sl (lock);
    juce::StringArray result;
    for (const auto& e : entries)
        result.addIfNotAlreadyThere (e.category);
    return result;
}

//==============================================================================
bool PresetLibrary::load (int index)
{
    const auto e = entry (index);
    if (e.isFactory())
    {
        processor.setCurrentProgram (e.factoryIndex);   // calls noteFactoryLoaded
        processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withProgramChanged (true));
        return true;
    }

    PresetEntry read;
    Values plain {};
    if (e.file == juce::File() || ! readUserFile (e.file, read, plain))
        return false;

    processor.applyPresetValues (plain, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f });
    const juce::ScopedLock sl (lock);
    currentIndex = index;
    orphanName.clear();
    remember (plain);
    return true;
}

bool PresetLibrary::saveUser (const juce::String& rawName, const juce::String& description, const juce::StringArray& tags)
{
    const auto name = rawName.trim();
    if (name.isEmpty())
        return false;

    const auto folder = getUserFolder();
    if (! folder.isDirectory() && ! folder.createDirectory().wasOk())
        return false;

    juce::ValueTree tree (kRoot);
    tree.setProperty ("version", 1, nullptr);
    tree.setProperty ("name", name, nullptr);
    tree.setProperty ("description", description.trim(), nullptr);
    tree.setProperty ("tags", tags.joinIntoString (","), nullptr);
    Values plain {};
    for (int i = 0; i < kNumPresetParams; ++i)
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (kPresetParamIds[i])))
        {
            plain[(size_t) i] = p->convertFrom0to1 (p->getValue());
            juce::ValueTree v ("param");
            v.setProperty ("id", kPresetParamIds[i], nullptr);
            v.setProperty ("value", plain[(size_t) i], nullptr);
            tree.appendChild (v, nullptr);
        }

    const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + kExtension);
    const auto xml = tree.createXml();
    if (xml == nullptr || ! xml->writeTo (file))
        return false;

    rescan();
    const juce::ScopedLock sl (lock);
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].file == file)
            currentIndex = (int) i;
    orphanName.clear();
    remember (plain);
    return true;
}

bool PresetLibrary::deleteUser (int index)
{
    const auto e = entry (index);
    if (e.isFactory() || ! e.file.existsAsFile())
        return false;
    if (! e.file.moveToTrash() && ! e.file.deleteFile())
        return false;

    bool wasCurrent;
    {
        const juce::ScopedLock sl (lock);
        wasCurrent = (index == currentIndex);
    }
    rescan();
    if (wasCurrent)
    {
        const juce::ScopedLock sl (lock);
        currentIndex = -1;
        orphanName = e.name;
    }
    return true;
}

//==============================================================================
int PresetLibrary::current() const
{
    const juce::ScopedLock sl (lock);
    return currentIndex;
}

juce::String PresetLibrary::currentName() const
{
    const juce::ScopedLock sl (lock);
    if (juce::isPositiveAndBelow (currentIndex, (int) entries.size()))
        return entries[(size_t) currentIndex].name;
    return orphanName.isNotEmpty() ? orphanName : juce::String ("Init");
}

bool PresetLibrary::isModified() const
{
    const juce::ScopedLock sl (lock);
    if (! haveLoaded)
        return false;
    for (int i = 0; i < kNumPresetParams; ++i)
        if (auto* p = processor.apvts.getParameter (kPresetParamIds[i]))
            if (std::abs (p->getValue() - loadedNorm[(size_t) i]) > 0.002f)
                return true;
    return false;
}

void PresetLibrary::noteFactoryLoaded (int program)
{
    const auto& factory = getFactoryPresets();
    if (! juce::isPositiveAndBelow (program, (int) factory.size()))
        return;
    const juce::ScopedLock sl (lock);
    currentIndex = indexOfFactory (program);
    orphanName.clear();
    remember (plainValues (factory[(size_t) program]));
}

void PresetLibrary::writeState (juce::ValueTree& state) const
{
    const juce::ScopedLock sl (lock);
    state.setProperty (kNameProperty, currentName(), nullptr);
    if (juce::isPositiveAndBelow (currentIndex, (int) entries.size()) && ! entries[(size_t) currentIndex].isFactory())
        state.setProperty (kUserProperty, entries[(size_t) currentIndex].file.getFileName(), nullptr);
    else
        state.removeProperty (kUserProperty, nullptr);
}

void PresetLibrary::readState (const juce::ValueTree& state, int factoryProgram)
{
    const auto userFile = state.getProperty (kUserProperty).toString();
    if (userFile.isEmpty())
    {
        noteFactoryLoaded (factoryProgram);
        return;
    }

    const auto file = getUserFolder().getChildFile (userFile);
    PresetEntry e;
    Values plain {};
    const bool ok = file.existsAsFile() && readUserFile (file, e, plain);
    const juce::ScopedLock sl (lock);
    currentIndex = -1;
    if (ok)
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].file == file)
                currentIndex = (int) i;
    if (currentIndex >= 0)
    {
        orphanName.clear();
        remember (plain);
    }
    else
    {
        orphanName = state.getProperty (kNameProperty, "Init").toString();
        haveLoaded = false;
    }
}

//==============================================================================
bool PresetLibrary::readUserFile (const juce::File& file, PresetEntry& e, Values& plain) const
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (kRoot.toString()))
        return false;
    const auto tree = juce::ValueTree::fromXml (*xml);

    e.name = tree.getProperty ("name", file.getFileNameWithoutExtension()).toString();
    e.category = kUserCategory;
    e.description = tree.getProperty ("description").toString();
    e.tags = juce::StringArray::fromTokens (tree.getProperty ("tags").toString(), ",", {});
    e.tags.trim();
    e.tags.removeEmptyStrings();
    e.factoryIndex = -1;
    e.file = file;
    e.haystack = makeHaystack (e);

    // Anything the file does not name keeps its default, so older files still load.
    for (int i = 0; i < kNumPresetParams; ++i)
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (kPresetParamIds[i]));
        plain[(size_t) i] = p != nullptr ? p->convertFrom0to1 (p->getDefaultValue()) : 0.0f;
        const auto v = tree.getChildWithProperty ("id", juce::String (kPresetParamIds[i]));
        if (v.isValid() && p != nullptr)
            plain[(size_t) i] = p->getNormalisableRange().snapToLegalValue ((float) v.getProperty ("value"));
    }
    return true;
}

void PresetLibrary::remember (const Values& plain)
{
    for (int i = 0; i < kNumPresetParams; ++i)
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (kPresetParamIds[i])))
            loadedNorm[(size_t) i] = p->convertTo0to1 (plain[(size_t) i]);
    haveLoaded = true;
}

int PresetLibrary::indexOfFactory (int program) const
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].factoryIndex == program)
            return (int) i;
    return -1;
}
} // namespace rb

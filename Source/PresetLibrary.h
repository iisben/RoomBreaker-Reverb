#pragma once
//==============================================================================
// Every preset the browser can show: the 28 factory rooms (which are also the
// host's programs) and the user's own, saved as small XML files.
//
//   macOS    ~/Library/Application Support/RoomBreaker/Presets/*.rbpreset
//   Windows  %APPDATA%\RoomBreaker\Presets\*.rbpreset
//
// A preset holds only the sound parameters (kPresetParamIds), never the gains,
// oversampling, limiter, stress source or bypass, so browsing never jumps the
// level or the CPU load. The library remembers the values it loaded, so the
// editor can mark the preset as modified when any of them has moved since.
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include "Presets.h"

class RoomBreakerAudioProcessor;

namespace rb
{
struct PresetEntry
{
    juce::String name, category, description;
    juce::StringArray tags;
    int factoryIndex = -1;      // >= 0 for a factory preset: its host program number
    juce::File file;            // a user preset's file
    juce::String haystack;      // lower case name, category, tags and description, for search

    bool isFactory() const noexcept { return factoryIndex >= 0; }
};

class PresetLibrary
{
public:
    explicit PresetLibrary (RoomBreakerAudioProcessor&);

    static juce::File defaultUserFolder();
    static constexpr const char* kUserCategory = "User";
    static constexpr const char* kExtension = ".rbpreset";

    /** Tests point this at a temporary folder. Rescans. */
    void setUserFolder (const juce::File&);
    juce::File getUserFolder() const;
    void rescan();

    int size() const;
    PresetEntry entry (int index) const;
    /** Indices of the presets that match every word of `text` (name, tags, category or description),
        carry every tag in `tags`, and sit in `category` (empty = any). Name matches come first. */
    std::vector<int> search (const juce::String& text, const juce::StringArray& tags, const juce::String& category) const;
    /** Factory categories in order, then User when there are user presets. */
    juce::StringArray categories() const;

    bool load (int index);
    /** Saves the current sound as a user preset (replacing one of the same name) and makes it current. */
    bool saveUser (const juce::String& name, const juce::String& description, const juce::StringArray& tags);
    /** Moves a user preset's file to the trash. Factory presets cannot be deleted. */
    bool deleteUser (int index);

    int current() const;
    juce::String currentName() const;
    /** True once any sound parameter differs from the preset as it was loaded. */
    bool isModified() const;

    /** Called by the processor whenever a factory program is applied (by the host or the browser). */
    void noteFactoryLoaded (int program);
    void writeState (juce::ValueTree&) const;
    /** After the parameters have been restored: finds the preset the session was using. */
    void readState (const juce::ValueTree&, int factoryProgram);

private:
    using Values = std::array<float, kNumPresetParams>;
    bool readUserFile (const juce::File&, PresetEntry&, Values&) const;
    void remember (const Values& plain);
    int indexOfFactory (int program) const;

    RoomBreakerAudioProcessor& processor;
    mutable juce::CriticalSection lock;
    std::vector<PresetEntry> entries;
    juce::File userFolder;
    int currentIndex = -1;
    juce::String orphanName;    // a session's user preset whose file has gone
    Values loadedNorm {};
    bool haveLoaded = false;
};
} // namespace rb

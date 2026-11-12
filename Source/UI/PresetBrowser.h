#pragma once
//==============================================================================
// The preset browser. It opens over the display and the controls (the meters stay
// visible) when you click the preset name, and closes with Escape, the close
// button or a double-click on a preset.
//
//   +--------------------------------------------------------------+
//   | [/] search name, tag, description...                     [x] |
//   +-------------+------------------------------------------------+
//   | CATEGORY    | NAME                  TAGS          CATEGORY   |
//   |  All     29 |  Default Bunker       Drums ...     MIX        |
//   |  Mix      3 |> Glass Rain Collapse  Sound FX ...  DARK CIN.  |
//   |  ...        |  ...                                           |
//   | TAGS        |                                                |
//   | [Vocals][Drums][Bass] ...                                    |
//   +-------------+------------------------------------------------+
//   | Glass Rain Collapse  A glass atrium that shatters...  [SAVE AS] [DEL] [folder] |
//   +--------------------------------------------------------------+
//
// Typing goes straight to the search field. Up / Down audition presets (each one
// loads as it is selected), Return keeps it and closes, Escape clears the search
// and then closes. Tags combine (AND); the category narrows further.
//==============================================================================
#include "Widgets.h"
#include "../PresetLibrary.h"

namespace rb::ui
{
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::KeyListener
{
public:
    explicit PresetBrowser (PresetLibrary&);
    ~PresetBrowser() override;

    void open();
    void close();
    std::function<void()> onClose, onPresetLoaded;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // for tests and the editor
    void setSearchText (const juce::String&);
    void setCategory (const juce::String&);
    void setTagSelected (const juce::String&, bool);
    int getNumResults() const noexcept { return (int) results.size(); }
    juce::String getResultName (int row) const;
    void startSave();
    bool isSaving() const noexcept { return saving; }

private:
    class CategoryList;

    // ListBoxModel
    int getNumRows() override { return (int) results.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void deleteKeyPressed (int) override {}

    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    void refresh();
    void loadRow (int row);
    void selectCurrent();
    void finishSave (bool apply);
    void updateButtons();

    PresetLibrary& library;
    std::vector<int> results;
    juce::String category;
    juce::StringArray tags;
    bool suppressLoad = false, saving = false;

    juce::TextEditor search;
    IconButton closeButton { Icon::close, "Close" };
    std::unique_ptr<CategoryList> categories;
    juce::OwnedArray<juce::TextButton> tagChips;
    juce::ListBox list { "Presets", this };

    juce::TextButton saveButton { "Save as..." }, deleteButton { "Delete" }, cancelButton { "Cancel" }, confirmButton { "Save" };
    IconButton folderButton { Icon::folder, "Show presets folder" };
    juce::TextEditor saveName, saveTags;

    juce::Rectangle<float> listHeader, detail, sidebar;
};
} // namespace rb::ui

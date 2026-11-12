#include "PresetBrowser.h"

namespace rb::ui
{
namespace
{
    constexpr int kRowH = 30, kCategoryRowH = 24, kChipH = 24;
    // list columns, as proportions of the list width
    constexpr float kTagsX = 0.44f, kCategoryX = 0.80f;
}

//==============================================================================
class PresetBrowser::CategoryList : public juce::Component
{
public:
    std::function<void (const juce::String&)> onSelect;

    void set (juce::StringArray n, juce::Array<int> c, const juce::String& selectedName)
    {
        names = std::move (n);
        counts = std::move (c);
        selected = juce::jmax (0, names.indexOf (selectedName));
        repaint();
    }
    int preferredHeight() const noexcept { return names.size() * kCategoryRowH; }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < names.size(); ++i)
        {
            const auto r = juce::Rectangle<float> (0.0f, (float) (i * kCategoryRowH), (float) getWidth(), (float) kCategoryRowH);
            if (i == selected || i == hovered)
            {
                g.setColour (i == selected ? C::bg3 : C::bg2);
                g.fillRoundedRectangle (r.reduced (0.0f, 1.0f), 3.0f);
            }
            if (i == selected)
            {
                g.setColour (C::accent);
                g.fillRect (r.reduced (0.0f, 6.0f).withWidth (2.0f));
            }
            const int n = counts[i];
            g.setFont (type::text());
            g.setColour (i == selected ? C::text0 : n == 0 ? C::text2 : C::text1);
            g.drawText (names[i], r.withTrimmedLeft (12.0f), juce::Justification::centredLeft, true);
            g.setFont (type::mono (type::S));
            g.setColour (i == selected ? C::text1 : C::text2);
            g.drawText (juce::String (n), r.withTrimmedRight (8.0f), juce::Justification::centredRight, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = (int) e.position.y / kCategoryRowH;
        if (juce::isPositiveAndBelow (i, names.size()) && onSelect)
            onSelect (i == 0 ? juce::String() : names[i]);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int i = (int) e.position.y / kCategoryRowH;
        if (i != hovered) { hovered = i; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }

private:
    juce::StringArray names;
    juce::Array<int> counts;
    int selected = 0, hovered = -1;
};

//==============================================================================
PresetBrowser::PresetBrowser (PresetLibrary& lib) : library (lib)
{
    setInfo (*this, "preset");
    setWantsKeyboardFocus (true);

    search.setFont (type::text());
    search.setTextToShowWhenEmpty ("Search names, tags and descriptions", C::text2);
    search.setIndents (32, 8);
    search.setEscapeAndReturnKeysConsumed (false);
    search.onTextChange = [this] { refresh(); };
    search.onFocusLost = [this] { repaint (search.getBounds()); };
    search.addKeyListener (this);
    addAndMakeVisible (search);

    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);

    categories = std::make_unique<CategoryList>();
    categories->onSelect = [this] (const juce::String& c) { category = c; refresh(); };
    addAndMakeVisible (*categories);

    for (const auto& t : presetTagVocabulary())
    {
        auto* chip = tagChips.add (new juce::TextButton (t));
        chip->setClickingTogglesState (true);
        chip->setWantsKeyboardFocus (false);
        chip->onClick = [this, chip, t]
        {
            if (chip->getToggleState()) tags.addIfNotAlreadyThere (t);
            else                        tags.removeString (t);
            refresh();
        };
        addAndMakeVisible (chip);
    }

    list.setRowHeight (kRowH);
    list.setOutlineThickness (0);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.addKeyListener (this);
    addAndMakeVisible (list);

    saveButton.onClick = [this] { startSave(); };
    deleteButton.onClick = [this]
    {
        const int row = list.getSelectedRow();
        if (juce::isPositiveAndBelow (row, (int) results.size()) && library.deleteUser (results[(size_t) row]))
        {
            refresh();
            if (onPresetLoaded) onPresetLoaded();   // the header shows the name as it is now
        }
    };
    folderButton.onClick = [this]
    {
        const auto folder = library.getUserFolder();
        if (folder.isDirectory() || folder.createDirectory().wasOk())
            folder.revealToUser();
    };
    cancelButton.onClick = [this] { finishSave (false); };
    confirmButton.onClick = [this] { finishSave (true); };
    for (auto* b : { &saveButton, &deleteButton, &cancelButton, &confirmButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    addAndMakeVisible (folderButton);
    setInfo (folderButton, "presetFolder");

    for (auto* te : { &saveName, &saveTags })
    {
        te->setFont (type::text());
        te->setIndents (10, 8);
        te->onReturnKey = [this] { finishSave (true); };
        te->onEscapeKey = [this] { finishSave (false); };
        addChildComponent (*te);
    }
    saveName.setTextToShowWhenEmpty ("Preset name", C::text2);
    saveTags.setTextToShowWhenEmpty ("Tags, separated by commas", C::text2);
    cancelButton.setVisible (false);
    confirmButton.setVisible (false);

    refresh();
}

PresetBrowser::~PresetBrowser()
{
    search.removeKeyListener (this);
    list.removeKeyListener (this);
    list.setModel (nullptr);
}

//==============================================================================
void PresetBrowser::open()
{
    library.rescan();   // pick up presets saved by another instance
    setVisible (true);
    toFront (false);
    refresh();
    selectCurrent();
    search.grabKeyboardFocus();
}

void PresetBrowser::close()
{
    if (saving)
        finishSave (false);
    // Give the keyboard back, so the host's own shortcuts (space for play) work again.
    if (auto* fc = juce::Component::getCurrentlyFocusedComponent(); fc != nullptr && isParentOf (fc))
        fc->giveAwayKeyboardFocus();
    setVisible (false);
    if (onClose)
        onClose();
}

void PresetBrowser::setSearchText (const juce::String& t)
{
    search.setText (t, false);   // the field's own change message is asynchronous; refresh now
    refresh();
}
void PresetBrowser::setCategory (const juce::String& c)   { category = c; refresh(); }

void PresetBrowser::setTagSelected (const juce::String& t, bool on)
{
    for (auto* chip : tagChips)
        if (chip->getButtonText() == t)
            chip->setToggleState (on, juce::sendNotificationSync);
}

juce::String PresetBrowser::getResultName (int row) const
{
    return juce::isPositiveAndBelow (row, (int) results.size()) ? library.entry (results[(size_t) row]).name : juce::String();
}

void PresetBrowser::refresh()
{
    const auto text = search.getText();
    results = library.search (text, tags, category);

    juce::StringArray names { "All" };
    juce::Array<int> counts { (int) library.search (text, tags, {}).size() };
    for (const auto& c : library.categories())
    {
        names.add (c);
        counts.add ((int) library.search (text, tags, c).size());
    }
    categories->set (names, counts, category.isEmpty() ? juce::String ("All") : category);

    list.updateContent();
    selectCurrent();
    list.repaint();
    updateButtons();
    resized();
    repaint();
}

void PresetBrowser::selectCurrent()
{
    const juce::ScopedValueSetter<bool> quiet (suppressLoad, true);
    const auto it = std::find (results.begin(), results.end(), library.current());
    if (it != results.end())
    {
        const int row = (int) std::distance (results.begin(), it);
        list.selectRow (row);
        list.scrollToEnsureRowIsOnscreen (row);
    }
    else
    {
        list.deselectAllRows();
    }
}

void PresetBrowser::loadRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) results.size()))
        return;
    library.load (results[(size_t) row]);
    list.repaint();
    updateButtons();
    repaint (detail.getSmallestIntegerContainer());
    if (onPresetLoaded)
        onPresetLoaded();
}

void PresetBrowser::updateButtons()
{
    const int row = list.getSelectedRow();
    const bool user = juce::isPositiveAndBelow (row, (int) results.size()) && ! library.entry (results[(size_t) row]).isFactory();
    deleteButton.setEnabled (user && ! saving);
    saveButton.setVisible (! saving);
    deleteButton.setVisible (! saving);
    folderButton.setVisible (! saving);
    saveName.setVisible (saving);
    saveTags.setVisible (saving);
    cancelButton.setVisible (saving);
    confirmButton.setVisible (saving);
}

void PresetBrowser::startSave()
{
    saving = true;
    const auto current = library.entry (library.current());
    saveName.setText (library.currentName(), false);
    saveTags.setText (current.tags.joinIntoString (", "), false);
    updateButtons();
    resized();
    repaint();
    saveName.grabKeyboardFocus();
    saveName.selectAll();
}

void PresetBrowser::finishSave (bool apply)
{
    if (! saving)
        return;
    if (apply)
    {
        auto t = juce::StringArray::fromTokens (saveTags.getText(), ",", {});
        t.trim();
        t.removeEmptyStrings();
        t.removeDuplicates (true);
        if (! library.saveUser (saveName.getText(), {}, t))
        {
            saveName.grabKeyboardFocus();   // empty name or an unwritable folder: stay in the form
            return;
        }
        if (onPresetLoaded) onPresetLoaded();
    }
    saving = false;
    refresh();
    search.grabKeyboardFocus();
}

//==============================================================================
void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (16);
    {
        auto top = r.removeFromTop (32);
        closeButton.setBounds (top.removeFromRight (32));
        top.removeFromRight (12);
        search.setBounds (top);
    }
    r.removeFromTop (16);

    auto bottom = r.removeFromBottom (56);
    detail = bottom.toFloat();
    r.removeFromBottom (12);

    auto side = r.removeFromLeft (208);
    sidebar = side.toFloat();
    r.removeFromLeft (24);
    side.removeFromTop (22);
    categories->setBounds (side.removeFromTop (categories->preferredHeight()));
    side.removeFromTop (16 + 22);
    {
        int x = side.getX(), y = side.getY();
        for (auto* chip : tagChips)
        {
            const int w = juce::roundToInt (type::width (type::caps(), chip->getButtonText().toUpperCase())) + 20;
            if (x + w > side.getRight()) { x = side.getX(); y += kChipH + 6; }
            chip->setBounds (x, y, w, kChipH);
            x += w + 6;
        }
    }

    listHeader = r.removeFromTop (22).toFloat();
    r.removeFromTop (4);
    list.setBounds (r);

    auto buttons = bottom.withSizeKeepingCentre (bottom.getWidth(), 32);
    if (saving)
    {
        confirmButton.setBounds (buttons.removeFromRight (88));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (88));
        buttons.removeFromRight (16);
        saveName.setBounds (buttons.removeFromLeft (260));
        buttons.removeFromLeft (8);
        saveTags.setBounds (buttons);
    }
    else
    {
        folderButton.setBounds (buttons.removeFromRight (32));
        buttons.removeFromRight (8);
        deleteButton.setBounds (buttons.removeFromRight (88));
        buttons.removeFromRight (8);
        saveButton.setBounds (buttons.removeFromRight (104));
    }
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (C::bg1);
    g.setColour (C::edge);
    g.drawRect (getLocalBounds(), 1);

    drawSectionHeader (g, sidebar.withHeight (16.0f), "Category");
    drawSectionHeader (g, sidebar.withY ((float) categories->getBottom() + 16.0f).withHeight (16.0f), "Tags");

    // list header: column names and the count
    {
        const float w = listHeader.getWidth();
        g.setFont (type::caps());
        g.setColour (C::text2);
        g.drawText ("NAME", listHeader.withTrimmedLeft (12.0f), juce::Justification::centredLeft, false);
        g.drawText ("TAGS", listHeader.withTrimmedLeft (w * kTagsX), juce::Justification::centredLeft, false);
        g.drawText (juce::String ((int) results.size()) + " OF " + juce::String (library.size()),
                    listHeader.withTrimmedRight (12.0f), juce::Justification::centredRight, false);
        g.setColour (C::line);
        g.fillRect (listHeader.getX(), listHeader.getBottom() - 1.0f, w, 1.0f);
    }
    if (results.empty())
    {
        g.setFont (type::text());
        g.setColour (C::text2);
        g.drawText ("No preset matches. Clear the search or a tag.", list.getBounds().toFloat().withHeight (60.0f),
                    juce::Justification::centred, false);
    }

    // detail: the selected preset, in words
    g.setColour (C::line);
    g.fillRect (detail.getX(), detail.getY() - 6.0f, detail.getWidth(), 1.0f);
    if (! saving)
    {
        const int row = list.getSelectedRow();
        const auto e = library.entry (juce::isPositiveAndBelow (row, (int) results.size()) ? results[(size_t) row] : library.current());
        auto text = detail.withTrimmedRight (260.0f).reduced (0.0f, 4.0f);
        g.setFont (type::strong());
        g.setColour (C::text0);
        g.drawText (e.name, text.removeFromTop (20.0f), juce::Justification::centredLeft, true);
        g.setFont (type::text());
        g.setColour (C::text1);
        g.drawFittedText (e.description.isNotEmpty() ? e.description : (e.isFactory() ? juce::String() : juce::String ("Your preset.")),
                          text.getSmallestIntegerContainer(), juce::Justification::topLeft, 2, 1.0f);
    }
}

void PresetBrowser::paintOverChildren (juce::Graphics& g)
{
    drawIcon (g, Icon::search, search.getBounds().toFloat().removeFromLeft (32.0f).withSizeKeepingCentre (16.0f, 16.0f),
              search.hasKeyboardFocus (true) ? C::accent : C::text2);
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) results.size()))
        return;
    const auto e = library.entry (results[(size_t) row]);
    const bool current = results[(size_t) row] == library.current();
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);

    if (selected)
    {
        g.setColour (C::bg3);
        g.fillRoundedRectangle (r.reduced (0.0f, 1.0f), 3.0f);
    }
    if (current)
    {
        g.setColour (C::accent);
        g.fillRect (r.reduced (0.0f, 8.0f).withWidth (2.0f));
    }

    g.setFont (type::text());
    g.setColour (current ? C::accent : C::text0);
    g.drawText (e.name, r.withTrimmedLeft (12.0f).withWidth ((float) w * kTagsX - 20.0f), juce::Justification::centredLeft, true);

    g.setColour (selected ? C::text1 : C::text2);   // text2 is under 4.5:1 on bg3
    g.drawText (e.tags.joinIntoString (", "), r.withTrimmedLeft ((float) w * kTagsX).withWidth ((float) w * (kCategoryX - kTagsX) - 12.0f),
                juce::Justification::centredLeft, true);

    g.setFont (type::caps());
    g.drawText (e.category.toUpperCase(), r.withTrimmedRight (12.0f).withTrimmedLeft ((float) w * kCategoryX),
                juce::Justification::centredRight, true);
}

void PresetBrowser::selectedRowsChanged (int row)
{
    updateButtons();
    repaint (detail.getSmallestIntegerContainer());
    if (! suppressLoad && row >= 0 && results[(size_t) row] != library.current())
        loadRow (row);   // audition: each preset loads as it is selected
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, (int) results.size()) && results[(size_t) row] != library.current())
        loadRow (row);
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    loadRow (row);
    close();
}

void PresetBrowser::returnKeyPressed (int)
{
    close();
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        if (results.empty())
            return true;
        const int sel = list.getSelectedRow();
        const int row = juce::jlimit (0, (int) results.size() - 1,
                                      sel < 0 ? 0 : sel + (key == juce::KeyPress::upKey ? -1 : 1));
        list.selectRow (row);
        list.scrollToEnsureRowIsOnscreen (row);
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        close();
        return true;
    }
    return keyPressed (key);
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (search.getText().isNotEmpty())
            search.clear(), refresh();
        else
            close();
        return true;
    }
    return false;
}
} // namespace rb::ui

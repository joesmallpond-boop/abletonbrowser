#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Library/SampleInfo.h"

namespace sb
{
// The list of matching sounds. Rows can be dragged straight into Live.
class ResultsTable : public juce::Component, private juce::TableListBoxModel
{
public:
    ResultsTable();

    enum Column { colFav = 1, colName, colInstrument, colKey, colBpm, colType, colLength, colContent, colProject };

    void setData (std::shared_ptr<const std::vector<SampleInfo>> samples, std::vector<int> indices);
    void selectPath (const juce::String& path, bool scrollTo);
    juce::String selectedPath() const;
    void setPlayingPath (const juce::String& p) { if (p != playingPath) { playingPath = p; table.repaint(); } }
    void setSamplerPath (const juce::String& p) { if (p != samplerPath) { samplerPath = p; table.repaint(); } }
    const SampleInfo* infoForRow (int row) const;
    void moveSelection (int delta);

    juce::var getSortState() const;
    void setSortState (const juce::var& v);

    std::function<void (const SampleInfo&)> onSelect;        // single click / arrow keys
    std::function<void (const SampleInfo&)> onDoubleClick;   // load into sampler
    std::function<void (const SampleInfo&)> onToggleFavourite;
    std::function<void (const SampleInfo&, const juce::MouseEvent&)> onRightClick;

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::TableListBox& getTable() { return table; }

private:
    int getNumRows() override { return (int) order.size(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRow) override;
    void sortOrderChanged (int columnId, bool forwards) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    juce::String getCellTooltip (int row, int col) override;
    void returnKeyPressed (int row) override;

    void applySort();

    juce::TableListBox table { {}, this };
    std::shared_ptr<const std::vector<SampleInfo>> data;
    std::vector<int> order;
    int sortColumn = colName;
    bool sortForwards = true;
    juce::String playingPath, samplerPath, lastSelected;
    bool suppressSelect = false;
};
}

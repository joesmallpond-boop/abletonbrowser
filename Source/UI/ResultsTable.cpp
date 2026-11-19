#include "ResultsTable.h"
#include "LookAndFeel.h"

namespace sb
{
using namespace juce;

static String lengthText (double s)
{
    if (s < 1.0) return String ((int) std::round (s * 1000)) + " ms";
    if (s < 60) return String (s, 2) + " s";
    return String ((int) s / 60) + ":" + String ((int) s % 60).paddedLeft ('0', 2);
}

ResultsTable::ResultsTable()
{
    auto& h = table.getHeader();
    h.addColumn ("", colFav, 24, 24, 24, TableHeaderComponent::notResizableOrSortable);
    h.addColumn ("Name", colName, 260, 80, 1000);
    h.addColumn ("Instrument", colInstrument, 96, 50, 200);
    h.addColumn ("Key", colKey, 64, 40, 120);
    h.addColumn ("BPM", colBpm, 56, 40, 100);
    h.addColumn ("Type", colType, 64, 40, 120);
    h.addColumn ("Length", colLength, 64, 40, 120);
    h.addColumn ("Pitch", colContent, 88, 40, 160);
    h.addColumn ("Project / source", colProject, 150, 60, 600);
    h.setSortColumnId (colName, true);
    h.setStretchToFitActive (false);
    table.setRowHeight (18);
    table.setHeaderHeight (19);
    table.setMultipleSelectionEnabled (false);
    table.setColour (ListBox::backgroundColourId, colours::area);
    table.setColour (ListBox::outlineColourId, colours::line);
    table.setOutlineThickness (0);
    table.setWantsKeyboardFocus (true);
    addAndMakeVisible (table);
}

void ResultsTable::resized()
{
    table.setBounds (getLocalBounds());
}

void ResultsTable::setData (std::shared_ptr<const std::vector<SampleInfo>> samples, std::vector<int> indices)
{
    auto keep = selectedPath();
    data = std::move (samples);
    order = std::move (indices);
    applySort();
    suppressSelect = true;
    table.updateContent();
    selectPath (keep, false);
    suppressSelect = false;
    table.repaint();
}

const SampleInfo* ResultsTable::infoForRow (int row) const
{
    if (data == nullptr || ! isPositiveAndBelow (row, (int) order.size())) return nullptr;
    auto idx = order[(size_t) row];
    return isPositiveAndBelow (idx, (int) data->size()) ? &(*data)[(size_t) idx] : nullptr;
}

void ResultsTable::selectPath (const String& path, bool scrollTo)
{
    lastSelected = path;
    for (int r = 0; r < (int) order.size(); ++r)
        if (auto* s = infoForRow (r); s != nullptr && s->path == path)
        {
            const bool was = suppressSelect;
            suppressSelect = true;
            table.selectRow (r, ! scrollTo, true);
            suppressSelect = was;
            if (scrollTo) table.scrollToEnsureRowIsOnscreen (r);
            return;
        }
    const bool was = suppressSelect;
    suppressSelect = true;
    table.deselectAllRows();
    suppressSelect = was;
}

String ResultsTable::selectedPath() const
{
    if (auto* s = infoForRow (table.getSelectedRow())) return s->path;
    return lastSelected;
}

void ResultsTable::moveSelection (int delta)
{
    if (order.empty()) return;
    int r = jlimit (0, (int) order.size() - 1, table.getSelectedRow() + delta);
    table.selectRow (r);
    table.scrollToEnsureRowIsOnscreen (r);
}

void ResultsTable::paintRowBackground (Graphics& g, int row, int w, int h, bool selected)
{
    auto* s = infoForRow (row);
    if (selected) g.fillAll (table.hasKeyboardFocus (true) ? colours::selection : colours::selectionStandby);
    if (s != nullptr && s->path == playingPath)
    {
        // Live's pre-listen blue marks the sound being previewed
        g.setColour (colours::prelisten);
        g.fillRect (0, 0, 3, h);
    }
    ignoreUnused (w);
}

void ResultsTable::paintCell (Graphics& g, int row, int col, int w, int h, bool selected)
{
    auto* s = infoForRow (row);
    if (s == nullptr) return;
    auto r = Rectangle<int> (4, 0, w - 8, h);
    g.setFont (liveFont());
    const Colour normal = selected ? colours::onText : colours::text;
    const Colour dim = selected ? colours::onText.withAlpha (0.65f) : colours::textDim;
    const Colour edited = selected ? colours::onText : colours::accent;
    g.setColour (normal);
    switch (col)
    {
        case colFav:
        {
            Path star;
            star.addStar ({ w * 0.5f, h * 0.5f }, 5, 2.6f, 6.0f, 0);
            g.setColour (s->favourite ? (selected ? colours::onText : colours::accent) : (selected ? dim : colours::panelHi.brighter (0.1f)));
            if (s->favourite) g.fillPath (star); else g.strokePath (star, PathStrokeType (1.0f));
            break;
        }
        case colName:
        {
            if (s->path == samplerPath)
            {
                g.setColour (selected ? colours::onText : colours::keyZone);
                g.fillRect ((float) r.getX(), h * 0.5f - 3, 6.0f, 6.0f);
                r.removeFromLeft (10);
                g.setColour (normal);
            }
            g.drawText (s->fileName(), r, Justification::centredLeft, true);
            break;
        }
        case colInstrument:
            g.setColour (s->userInstrument >= 0 ? edited : normal);
            g.drawText (instrumentName (s->instrument()), r, Justification::centredLeft, true);
            break;
        case colKey:
            g.setColour (s->userKeyRoot != -2 ? edited : (s->keyRoot() < 0 ? dim : normal));
            g.drawText (s->keyDisplay(), r, Justification::centredLeft, true);
            break;
        case colBpm:
            g.setColour (s->userBpm >= 0 ? edited : normal);
            g.drawText (s->bpm() > 0 ? String (s->bpm(), s->bpm() == std::floor (s->bpm()) ? 0 : 1) : String ("-"), r, Justification::centredLeft, true);
            break;
        case colType:
            g.drawText (s->type() == SoundType::Loop ? "Loop" : "One-shot", r, Justification::centredLeft, true);
            break;
        case colLength:
            g.setColour (dim);
            g.drawText (lengthText (s->effectiveLength()), r, Justification::centredLeft, true);
            break;
        case colContent:
            g.setColour (dim);
            g.drawText (tonalityName (s->tonality), r, Justification::centredLeft, true);
            break;
        case colProject:
            g.setColour (dim);
            g.drawText (s->project, r, Justification::centredLeft, true);
            break;
        default: break;
    }
}

String ResultsTable::getCellTooltip (int row, int col)
{
    auto* s = infoForRow (row);
    if (s == nullptr) return {};
    if (col == colFav) return "Favourite";
    return s->path;
}

void ResultsTable::cellClicked (int row, int col, const MouseEvent& e)
{
    auto* s = infoForRow (row);
    if (s == nullptr) return;
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (*s, e); return; }
    if (col == colFav) { if (onToggleFavourite) onToggleFavourite (*s); return; }
    // clicking the already-selected row plays it again
    if (row == table.getSelectedRow() && onSelect) onSelect (*s);
}

void ResultsTable::cellDoubleClicked (int row, int, const MouseEvent&)
{
    if (auto* s = infoForRow (row)) if (onDoubleClick) onDoubleClick (*s);
}

void ResultsTable::returnKeyPressed (int row)
{
    if (auto* s = infoForRow (row)) if (onDoubleClick) onDoubleClick (*s);
}

void ResultsTable::selectedRowsChanged (int lastRow)
{
    if (suppressSelect) return;
    if (auto* s = infoForRow (lastRow))
    {
        lastSelected = s->path;
        if (onSelect) onSelect (*s);
    }
}

bool ResultsTable::keyPressed (const KeyPress& k)
{
    return false;
}

void ResultsTable::sortOrderChanged (int columnId, bool forwards)
{
    sortColumn = columnId; sortForwards = forwards;
    auto keep = selectedPath();
    applySort();
    table.updateContent();
    selectPath (keep, false);
    table.repaint();
}

void ResultsTable::applySort()
{
    if (data == nullptr) return;
    auto& d = *data;
    auto key = [&] (const SampleInfo& s) -> String {
        switch (sortColumn)
        {
            case colInstrument: return instrumentName (s.instrument());
            case colType: return s.type() == SoundType::Loop ? "1" : "0";
            case colContent: return tonalityName (s.tonality);
            case colProject: return s.project;
            default: return s.fileName();
        }
    };
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) {
        const auto& x = d[(size_t) a]; const auto& y = d[(size_t) b];
        int c = 0;
        switch (sortColumn)
        {
            case colKey:
            {
                auto kx = x.keyRoot() < 0 ? 999 : x.keyMode() * 12 + x.keyRoot();
                auto ky = y.keyRoot() < 0 ? 999 : y.keyMode() * 12 + y.keyRoot();
                c = kx < ky ? -1 : kx > ky ? 1 : 0; break;
            }
            case colBpm: c = x.bpm() < y.bpm() ? -1 : x.bpm() > y.bpm() ? 1 : 0; break;
            case colLength: c = x.effectiveLength() < y.effectiveLength() ? -1 : x.effectiveLength() > y.effectiveLength() ? 1 : 0; break;
            default: c = key (x).compareNatural (key (y)); break;
        }
        if (c == 0) c = x.fileName().compareNatural (y.fileName());
        return sortForwards ? c < 0 : c > 0;
    });
}

var ResultsTable::getDragSourceDescription (const SparseSet<int>& rows)
{
    if (rows.isEmpty()) return {};
    if (auto* s = infoForRow (rows[0])) return "sample:" + s->path;
    return {};
}

var ResultsTable::getSortState() const
{
    auto* o = new DynamicObject();
    o->setProperty ("col", sortColumn);
    o->setProperty ("fwd", sortForwards);
    o->setProperty ("widths", table.getHeader().toString());
    return var (o);
}

void ResultsTable::setSortState (const var& v)
{
    if (! v.isObject()) return;
    if (v.hasProperty ("widths")) table.getHeader().restoreFromString (v["widths"].toString());
    sortColumn = (int) v.getProperty ("col", (int) colName);
    sortForwards = (bool) v.getProperty ("fwd", true);
    table.getHeader().setSortColumnId (sortColumn, sortForwards);
}
}

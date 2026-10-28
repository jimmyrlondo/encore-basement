/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Modified for Encore: Basement (2026): Encore colours, clip dragging, a bar
    ruler, wheel zoom/scroll, volume and pan on the track headers. Licensed
    under the GPL v3 (or later) as part of Basement (AGPLv3).
*/

#pragma once

namespace te = tracktion;
using namespace std::literals;

//==============================================================================
class PluginTreeBase
{
public:
    virtual ~PluginTreeBase() = default;
    virtual String getUniqueName() const = 0;

    void addSubItem (PluginTreeBase* itm)   { subitems.add (itm);       }
    int getNumSubItems()                    { return subitems.size();   }
    PluginTreeBase* getSubItem (int idx)    { return subitems[idx];     }

private:
    OwnedArray<PluginTreeBase> subitems;
};

//==============================================================================
class PluginTreeItem : public PluginTreeBase
{
public:
    PluginTreeItem (const PluginDescription&);
    PluginTreeItem (const String& uniqueId, const String& name, const String& xmlType, bool isSynth, bool isPlugin);

    te::Plugin::Ptr create (te::Edit&);

    String getUniqueName() const override
    {
        if (desc.fileOrIdentifier.startsWith (te::RackType::getRackPresetPrefix()))
            return desc.fileOrIdentifier;

        return desc.createIdentifierString();
    }

    PluginDescription desc;
    String xmlType;
    bool isPlugin = true;

    JUCE_LEAK_DETECTOR (PluginTreeItem)
};

//==============================================================================
class PluginTreeGroup : public PluginTreeBase
{
public:
    PluginTreeGroup (te::Edit&, KnownPluginList::PluginTree&, te::Plugin::Type);
    PluginTreeGroup (const String&);

    String getUniqueName() const override           { return name; }

    String name;

private:
    void populateFrom (KnownPluginList::PluginTree&);
    void createBuiltInItems (int& num, te::Plugin::Type);

    JUCE_LEAK_DETECTOR (PluginTreeGroup)
};

//==============================================================================
PluginTreeItem::PluginTreeItem (const juce::PluginDescription& d)
    : desc (d), xmlType (te::ExternalPlugin::xmlTypeName), isPlugin (true)
{
    jassert (xmlType.isNotEmpty());
}

PluginTreeItem::PluginTreeItem (const juce::String& uniqueId, const juce::String& name,
                                const juce::String& xmlType_, bool isSynth, bool isPlugin_)
    : xmlType (xmlType_), isPlugin (isPlugin_)
{
    jassert (xmlType.isNotEmpty());
    desc.name = name;
    desc.fileOrIdentifier = uniqueId;
    desc.pluginFormatName = (uniqueId.endsWith ("_trkbuiltin") || xmlType == te::RackInstance::xmlTypeName)
                                ? juce::String (te::PluginManager::builtInPluginFormatName) : juce::String();
    desc.category = xmlType;
    desc.isInstrument = isSynth;
}

te::Plugin::Ptr PluginTreeItem::create (te::Edit& ed)
{
    return ed.getPluginCache().createNewPlugin (xmlType, desc);
}

//==============================================================================
PluginTreeGroup::PluginTreeGroup (te::Edit& edit, KnownPluginList::PluginTree& tree, te::Plugin::Type types)
    : name ("Plugins")
{
    {
        int num = 1;

        auto builtinFolder = new PluginTreeGroup (TRANS("Builtin Plugins"));
        addSubItem (builtinFolder);
        builtinFolder->createBuiltInItems (num, types);
    }

    {
        auto racksFolder = new PluginTreeGroup (TRANS("Plugin Racks"));
        addSubItem (racksFolder);

        racksFolder->addSubItem (new PluginTreeItem (String (te::RackType::getRackPresetPrefix()) + "-1",
                                                     TRANS("Create New Empty Rack"),
                                                     te::RackInstance::xmlTypeName, false, false));

        int i = 0;
        for (auto rf : edit.getRackList().getTypes())
            racksFolder->addSubItem (new PluginTreeItem ("RACK__" + String (i++), rf->rackName,
                                                         te::RackInstance::xmlTypeName, false, false));
    }

    populateFrom (tree);
}

PluginTreeGroup::PluginTreeGroup (const String& s)  : name (s)
{
    jassert (name.isNotEmpty());
}

void PluginTreeGroup::populateFrom (KnownPluginList::PluginTree& tree)
{
    for (auto subTree : tree.subFolders)
    {
        if (subTree->plugins.size() > 0 || subTree->subFolders.size() > 0)
        {
            auto fs = new PluginTreeGroup (subTree->folder);
            addSubItem (fs);

            fs->populateFrom (*subTree);
        }
    }

    for (const auto& pd : tree.plugins)
        addSubItem (new PluginTreeItem (pd));
}


template<class FilterClass>
void addInternalPlugin (PluginTreeBase& item, int& num, bool synth = false)
{
    item.addSubItem (new PluginTreeItem (String (num++) + "_trkbuiltin",
                                         TRANS (FilterClass::getPluginName()),
                                         FilterClass::xmlTypeName, synth, false));
}

void PluginTreeGroup::createBuiltInItems (int& num, te::Plugin::Type types)
{
    addInternalPlugin<te::VolumeAndPanPlugin> (*this, num);
    addInternalPlugin<te::LevelMeterPlugin> (*this, num);
    addInternalPlugin<te::EqualiserPlugin> (*this, num);
    addInternalPlugin<te::ReverbPlugin> (*this, num);
    addInternalPlugin<te::DelayPlugin> (*this, num);
    addInternalPlugin<te::ChorusPlugin> (*this, num);
    addInternalPlugin<te::PhaserPlugin> (*this, num);
    addInternalPlugin<te::CompressorPlugin> (*this, num);
    addInternalPlugin<te::PitchShiftPlugin> (*this, num);
    addInternalPlugin<te::LowPassPlugin> (*this, num);
    addInternalPlugin<te::MidiModifierPlugin> (*this, num);
    addInternalPlugin<te::MidiPatchBayPlugin> (*this, num);
    addInternalPlugin<te::PatchBayPlugin> (*this, num);
    addInternalPlugin<te::AuxSendPlugin> (*this, num);
    addInternalPlugin<te::AuxReturnPlugin> (*this, num);
    addInternalPlugin<te::TextPlugin> (*this, num);
    addInternalPlugin<te::FreezePointPlugin> (*this, num);

   #if TRACKTION_ENABLE_REWIRE
    addInternalPlugin<te::ReWirePlugin> (*this, num, true);
   #endif

    if (types == te::Plugin::Type::allPlugins)
    {
        addInternalPlugin<te::SamplerPlugin> (*this, num, true);
        addInternalPlugin<te::FourOscPlugin> (*this, num, true);
    }

    addInternalPlugin<te::InsertPlugin> (*this, num);

   #if ENABLE_INTERNAL_PLUGINS
    for (auto& d : PluginTypeBase::getAllPluginDescriptions())
        if (isPluginAuthorised (d))
            addSubItem (new PluginTreeItem (d));
   #endif
}

//==============================================================================
class PluginMenu : public PopupMenu
{
public:
    PluginMenu() = default;

    PluginMenu (PluginTreeGroup& node, std::function<void (PluginTreeItem*)> callback = nullptr)
    {
        for (int i = 0; i < node.getNumSubItems(); ++i)
            if (auto subNode = dynamic_cast<PluginTreeGroup*> (node.getSubItem (i)))
                addSubMenu (subNode->name, PluginMenu (*subNode, callback), true);

        for (int i = 0; i < node.getNumSubItems(); ++i)
        {
            if (auto subType = dynamic_cast<PluginTreeItem*> (node.getSubItem (i)))
            {
                if (callback)
                    addItem (subType->desc.name, [subType, callback] { callback (subType); });
                else
                    addItem (subType->getUniqueName().hashCode(), subType->desc.name, true, false);
            }
        }
    }

    static PluginTreeItem* findType (PluginTreeGroup& node, int hash)
    {
        for (int i = 0; i < node.getNumSubItems(); ++i)
            if (auto subNode = dynamic_cast<PluginTreeGroup*> (node.getSubItem (i)))
                if (auto* t = findType (*subNode, hash))
                    return t;

        for (int i = 0; i < node.getNumSubItems(); ++i)
            if (auto t = dynamic_cast<PluginTreeItem*> (node.getSubItem (i)))
                if (t->getUniqueName().hashCode() == hash)
                    return t;

        return nullptr;
    }

    PluginTreeItem* runMenu (PluginTreeGroup& node)
    {
        int res = show();

        if (res == 0)
            return nullptr;

        return findType (node, res);
    }
};

//==============================================================================
inline te::Plugin::Ptr showMenuAndCreatePlugin (te::Edit& edit)
{
    if (auto tree = EngineHelpers::createPluginTree (edit.engine))
    {
        PluginTreeGroup root (edit, *tree, te::Plugin::Type::allPlugins);
        PluginMenu m (root);

        if (auto type = m.runMenu (root))
            return type->create (edit);
    }

    return {};
}

inline void showMenuAndCreatePluginAsync (te::Track::Ptr destTrack, int index,
                                          std::function<void (te::Plugin::Ptr)> onInserted)
{
    if (auto tree = std::shared_ptr<juce::KnownPluginList::PluginTree> (EngineHelpers::createPluginTree (destTrack->edit.engine)))
    {
        auto root = std::make_shared<PluginTreeGroup> (destTrack->edit, *tree, te::Plugin::Type::allPlugins);
        PluginMenu m (*root,
                      [tree, root, destTrack, index, onInserted] (PluginTreeItem* selectedItem)
                      {
                          if (auto newPlugin = selectedItem->create (destTrack->edit))
                          {
                              destTrack->pluginList.insertPlugin (newPlugin, index, nullptr);

                              if (onInserted)
                                  onInserted (newPlugin);
                          }
                      });
        m.showMenuAsync({});
    }
}


//==============================================================================
/** The Effects menu: Encore's plugins first, then yours by maker, then a short
    list of built-in effects. New plugins are found in the background, so the
    menu says when a search is still running. */
inline void showEffectsMenu (te::Track::Ptr track, te::SelectionManager* sm, Component* target)
{
    auto& edit = track->edit;
    auto add = [track, sm] (te::Plugin::Ptr p)
    {
        if (p == nullptr) return;
        // effects go after any earlier effects, before the track's volume/pan and meter
        auto list = track->pluginList.getPlugins();
        int index = list.size();
        for (int i = 0; i < list.size(); ++i)
            if (dynamic_cast<te::VolumeAndPanPlugin*> (list[i].get()) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (list[i].get()) != nullptr) { index = i; break; }
        track->pluginList.insertPlugin (p, index, sm);
        p->showWindowExplicitly();
    };

    PopupMenu m, encore, instruments;
    std::map<String, PopupMenu> byMaker;
    auto types = pluginsForMenus (edit.engine);
    std::sort (types.begin(), types.end(), [] (auto& a, auto& b) { return a.name.compareNatural (b.name) < 0; });
    for (auto& d : types)
    {
        auto maker = d.manufacturerName.trim().isEmpty() ? String ("Other") : d.manufacturerName.trim();
        auto item = [&edit, add, d] { add (edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, d)); };
        if (d.pluginFormatName == "Encore")
            encore.addItem (d.name + "  (" + d.category + ")", item);
        else if (maker.containsIgnoreCase ("Encore") || maker.containsIgnoreCase ("Sick & Sad"))
            continue; // installed VST3 copies of the same plugins: the built-in ones replace them
        else if (d.isInstrument)
            instruments.addItem (d.name, [&edit, track, d]
            {
                if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
                    instruments::makeInstrumentTrack (*at, edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, d));
            });
        else
            byMaker[maker].addItem (d.name, item);
    }

    if (encore.getNumItems() > 0) { m.addSectionHeader ("Encore"); for (PopupMenu::MenuItemIterator it (encore); it.next();) m.addItem (PopupMenu::Item (it.getItem())); }
    m.addSectionHeader ("Your plugins");
    if (byMaker.empty()) m.addItem ("None found yet", false, false, nullptr);
    for (auto& [maker, menu] : byMaker) m.addSubMenu (maker, menu);
    instruments.addSeparator();
    instruments.addItem ("Built-in synth (4OSC)", [&edit, track]
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
            instruments::makeInstrumentTrack (*at, edit.getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}));
    });
    m.addSubMenu ("Instrument (plays this track from MIDI)", instruments, true);

    m.addSectionHeader ("Built in");
    auto builtIn = [&m, &edit, add] (const char* name, const char* type)
    {
        m.addItem (name, [&edit, add, t = String (type)] { add (edit.getPluginCache().createNewPlugin (t, {})); });
    };
    builtIn ("EQ (4-band)", te::EqualiserPlugin::xmlTypeName);
    builtIn ("Compressor / Limiter", te::CompressorPlugin::xmlTypeName);
    builtIn ("Reverb", te::ReverbPlugin::xmlTypeName);
    builtIn ("Delay", te::DelayPlugin::xmlTypeName);
    builtIn ("Chorus", te::ChorusPlugin::xmlTypeName);
    builtIn ("Phaser", te::PhaserPlugin::xmlTypeName);
    builtIn ("Pitch Shifter", te::PitchShiftPlugin::xmlTypeName);
    builtIn ("Low / High-Pass Filter", te::LowPassPlugin::xmlTypeName);

    m.addSeparator();
    auto scanning = BasementHooks::pluginScanStatus ? BasementHooks::pluginScanStatus() : String();
    if (scanning.isNotEmpty()) m.addItem (scanning, false, false, nullptr);
    else m.addItem ("Find new plugins", [] { if (BasementHooks::findPlugins) BasementHooks::findPlugins(); });
    m.addItem ("Plugin Manager...", [] { if (BasementHooks::showPluginManager) BasementHooks::showPluginManager(); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target));
}

//==============================================================================
ClipComponent::ClipComponent (EditViewState& evs, te::Clip::Ptr c)
    : editViewState (evs), clip (c)
{
}

void ClipComponent::paint (Graphics& g)
{
    auto colour = clip->getTrack() != nullptr ? clip->getTrack()->getColour() : theme::accent;
    auto r = getLocalBounds().toFloat().reduced (0.5f, 3.0f);
    g.setColour (colour.withAlpha (0.22f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (colour.withAlpha (0.55f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    g.setColour (theme::paper.withAlpha (0.85f));
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText (clip->getName(), getLocalBounds().reduced (6, 5).removeFromTop (14), Justification::topLeft, true);

    if (editViewState.selectionManager.isSelected (clip.get()))
    {
        g.setColour (theme::paper);
        g.drawRoundedRectangle (r, 5.0f, 2.0f);
    }

    // fade handles in the top corners while the mouse is over the clip
    if (hovering && dynamic_cast<te::AudioClipBase*> (clip.get()) != nullptr && getWidth() > 30)
    {
        g.setColour (theme::paper);
        g.fillRect (Rectangle<float> (2.0f, 4.0f, 7.0f, 7.0f));
        g.fillRect (Rectangle<float> ((float) getWidth() - 9.0f, 4.0f, 7.0f, 7.0f));
    }
}

// Where the mouse is decides what a drag does: the top corners set fades,
// the left and right edges trim, anywhere else moves the clip.
ClipComponent::Zone ClipComponent::zoneAt (Point<int> p) const
{
    const bool audio = dynamic_cast<te::AudioClipBase*> (clip.get()) != nullptr;
    const int edge = jmin (8, getWidth() / 4);
    if (audio && getWidth() > 30 && p.y <= 13)
    {
        if (p.x <= 12) return Zone::fadeIn;
        if (p.x >= getWidth() - 12) return Zone::fadeOut;
    }
    if (p.x <= edge) return Zone::trimStart;
    if (p.x >= getWidth() - edge) return Zone::trimEnd;
    return Zone::body;
}

void ClipComponent::mouseMove (const MouseEvent& e)
{
    if (! hovering) { hovering = true; repaint(); }
    switch (zoneAt (e.getPosition()))
    {
        case Zone::trimStart: case Zone::trimEnd: setMouseCursor (MouseCursor::LeftRightResizeCursor); break;
        case Zone::fadeIn: case Zone::fadeOut:    setMouseCursor (MouseCursor::PointingHandCursor); break;
        default:                                  setMouseCursor (MouseCursor::NormalCursor); break;
    }
}

void ClipComponent::mouseExit (const MouseEvent&) { hovering = false; repaint(); }

void ClipComponent::mouseDown (const MouseEvent& e)
{
    auto& sm = editViewState.selectionManager;
    if (e.mods.isPopupMenu())
    {
        if (! sm.isSelected (clip.get())) sm.selectOnly (clip.get());
        PopupMenu m;
        if (BasementHooks::fillClipMenu) BasementHooks::fillClipMenu (m, *clip);
        m.showMenuAsync (PopupMenu::Options());
        return;
    }
    if (e.mods.isShiftDown() || e.mods.isCommandDown())
    {
        if (sm.isSelected (clip.get())) sm.deselect (clip.get());
        else sm.select (clip.get(), true);
    }
    else if (! sm.isSelected (clip.get()))
        sm.selectOnly (clip.get());

    dragZone = zoneAt (e.getPosition());
    posAtDragStart = clip->getPosition();
    dragStart = posAtDragStart.getStart();
    if (auto a = dynamic_cast<te::AudioClipBase*> (clip.get()))
        fadeAtDragStart = dragZone == Zone::fadeIn ? a->getFadeIn() : a->getFadeOut();
    groupAtDragStart.clear();
    for (auto* s : sm.getSelectedObjects())
        if (auto c = dynamic_cast<te::Clip*> (s))
            groupAtDragStart.push_back ({ c, c->getPosition().getStart() });
    dragging = false;
}

void ClipComponent::mouseDrag (const MouseEvent& e)
{
    auto* lane = getParentComponent();
    if (lane == nullptr || ! e.mouseWasDraggedSinceMouseDown()) return;
    auto& um = clip->edit.getUndoManager();
    const char* names[] = { "Move clips", "Trim clip", "Trim clip", "Fade in", "Fade out" };
    if (! dragging) { um.beginNewTransaction (names[(int) dragZone]); dragging = true; }

    const bool free = e.mods.isShiftDown(); // Shift: ignore the grid
    const auto mouseTime = editViewState.xToTime (e.getEventRelativeTo (lane).x, lane->getWidth());
    const auto dx = editViewState.xToTime (e.getDistanceFromDragStartX(), lane->getWidth()) - editViewState.xToTime (0, lane->getWidth());
    const auto minLen = te::TimeDuration::fromSeconds (0.01);

    switch (dragZone)
    {
        case Zone::body:
        {
            // the grabbed clip snaps; everything selected moves with it
            auto newStart = editViewState.snap (te::TimePosition::fromSeconds (jmax (0.0, (dragStart + dx).inSeconds())), free);
            auto delta = newStart - dragStart;
            for (auto& [c, start] : groupAtDragStart)
                c->setStart (te::TimePosition::fromSeconds (jmax (0.0, (start + delta).inSeconds())), false, true);
            // dragged over another audio track: it moves there when you let go
            dropTrack = nullptr;
            if (auto ec = findParentComponentOfClass<EditComponent>())
                if (auto target = ec->audioTrackAt (e.getEventRelativeTo (ec).getPosition()))
                    if (target != clip->getTrack() && groupAtDragStart.size() == 1)
                        dropTrack = target;
            break;
        }
        case Zone::trimStart:
        {
            // can't pull the start before the beginning of the recording
            auto earliest = posAtDragStart.getStart() - posAtDragStart.getOffset();
            auto t = jlimit (jmax (te::TimePosition(), earliest), posAtDragStart.getEnd() - minLen, editViewState.snap (mouseTime, free));
            clip->setStart (t, true, false);
            break;
        }
        case Zone::trimEnd:
        {
            auto latest = posAtDragStart.getStart() + (clip->getMaximumLength() - posAtDragStart.getOffset());
            auto t = jlimit (posAtDragStart.getStart() + minLen, latest, editViewState.snap (mouseTime, free));
            clip->setEnd (t, true);
            break;
        }
        case Zone::fadeIn:
        case Zone::fadeOut:
            if (auto a = dynamic_cast<te::AudioClipBase*> (clip.get()))
            {
                const auto len = a->getPosition().getLength();
                if (dragZone == Zone::fadeIn)
                    a->setFadeIn (jlimit (te::TimeDuration(), len - a->getFadeOut(), mouseTime - a->getPosition().getStart()));
                else
                    a->setFadeOut (jlimit (te::TimeDuration(), len - a->getFadeIn(), a->getPosition().getEnd() - mouseTime));
                repaint();
            }
            break;
    }
}

void ClipComponent::mouseUp (const MouseEvent&)
{
    dragging = false;
    groupAtDragStart.clear();
    if (auto target = std::exchange (dropTrack, nullptr))
        clip->moveTo (*target); // this component is rebuilt on the new track, so nothing after this
}

//==============================================================================
AudioClipComponent::AudioClipComponent (EditViewState& evs, te::Clip::Ptr c)
    : ClipComponent (evs, c)
{
    // where clips overlap they crossfade, instead of cutting hard
    if (auto a = dynamic_cast<te::AudioClipBase*> (c.get()); a != nullptr && ! a->getAutoCrossfade())
        a->setAutoCrossfade (true);
    updateThumbnail();
}

void AudioClipComponent::paint (Graphics& g)
{
    ClipComponent::paint (g);

    if (editViewState.drawWaveforms && thumbnail != nullptr)
        drawWaveform (g, *getWaveAudioClip(), *thumbnail, theme::paper.withAlpha (0.7f),
                      0, getWidth(), 18, getHeight() - 22, 0);

    // fades, mute, reverse and clip gain (Audio menu)
    auto& c = *getWaveAudioClip();
    const double len = c.getPosition().getLength().inSeconds();
    if (len > 0)
    {
        const float w = (float) getWidth(), h = (float) getHeight();
        g.setColour (theme::base.withAlpha (0.55f));
        if (auto fi = (float) (c.getFadeIn().inSeconds() / len * w); fi > 1.0f)
        {
            Path p; p.startNewSubPath (0, 0); p.lineTo (fi, 0); p.lineTo (0, h); p.closeSubPath();
            g.fillPath (p);
        }
        if (auto fo = (float) (c.getFadeOut().inSeconds() / len * w); fo > 1.0f)
        {
            Path p; p.startNewSubPath (w, 0); p.lineTo (w - fo, 0); p.lineTo (w, h); p.closeSubPath();
            g.fillPath (p);
        }
    }
    StringArray tags;
    if (c.isMuted()) tags.add ("Muted");
    if (c.getIsReversed()) tags.add ("Reversed");
    if (std::abs (c.getGainDB()) > 0.05f) tags.add ((c.getGainDB() > 0 ? "+" : "") + String (c.getGainDB(), 1) + " dB");
    if (c.isMuted()) g.fillAll (theme::base.withAlpha (0.5f));
    if (! tags.isEmpty())
    {
        g.setColour (theme::paper.withAlpha (0.85f));
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (tags.joinIntoString ("  "), getLocalBounds().reduced (6, 2), Justification::topRight, true);
    }
}

void AudioClipComponent::drawWaveform (Graphics& g, te::AudioClipBase& c, te::SmartThumbnail& thumb, Colour colour,
                                       int left, int right, int y, int h, int xOffset)
{
    auto getTimeRangeForDrawing = [this] (const int l, const int r) -> tracktion::TimeRange
    {
        if (auto p = getParentComponent())
        {
            auto t1 = editViewState.xToTime (l, p->getWidth());
            auto t2 = editViewState.xToTime (r, p->getWidth());

            return { t1, t2 };
        }

        return {};
    };

    jassert (left <= right);
    const auto gain = c.getGain() * jlimit (1.0f, 32.0f, editViewState.waveformZoom.get());
    const auto pan = thumb.getNumChannels() == 1 ? 0.0f : c.getPan();

    const float pv = pan * gain;
    const float gainL = (gain - pv);
    const float gainR = (gain + pv);

    const bool usesTimeStretchedProxy = c.usesTimeStretchedProxy();

    const auto clipPos = c.getPosition();
    auto offset = clipPos.getOffset();
    auto speedRatio = c.getSpeedRatio();

    g.setColour (colour);

    const auto activeChannels = c.getActiveChannelConfiguration();
    const bool useLeft = activeChannels.containsDeviceChannel (0);
    const bool useRight = activeChannels.containsDeviceChannel (1);

    if (usesTimeStretchedProxy)
    {
        const Rectangle<int> area (left + xOffset, y, right - left, h);

        if (! thumb.isOutOfDate())
        {
            drawChannels (g, thumb, area,
                          getTimeRangeForDrawing (left, right),
                          useLeft, useRight,
                          gainL, gainR);
        }
    }
    else if (c.getLoopLength() == 0s)
    {
        auto region = getTimeRangeForDrawing (left, right);

        auto t1 = (region.getStart() + offset) * speedRatio;
        auto t2 = (region.getEnd()   + offset) * speedRatio;

        drawChannels (g, thumb,
                      { left + xOffset, y, right - left, h },
                      { t1, t2 },
                      useLeft, useRight,
                      gainL, gainR);
    }
}

void AudioClipComponent::drawChannels (Graphics& g, te::SmartThumbnail& thumb, Rectangle<int> area,
                                       te::TimeRange time, bool useLeft, bool useRight,
                                       float leftGain, float rightGain)
{
    if (useLeft && useRight && thumb.getNumChannels() > 1)
    {
        thumb.drawChannel (g, area.removeFromTop (area.getHeight() / 2), time, 0, leftGain);
        thumb.drawChannel (g, area, time, 1, rightGain);
    }
    else if (useLeft)
    {
        thumb.drawChannel (g, area, time, 0, leftGain);
    }
    else if (useRight)
    {
        thumb.drawChannel (g, area, time, 1, rightGain);
    }
}

void AudioClipComponent::updateThumbnail()
{
    if (auto* wac = getWaveAudioClip())
    {
        te::AudioFile af (wac->getAudioFile());

        if (af.getFile().existsAsFile() || (! wac->usesSourceFile()))
        {
            if (af.isValid())
            {
                const te::AudioFile proxy ((wac->hasAnyTakes() && wac->isShowingTakes()) ? wac->getAudioFile() : wac->getPlaybackFile());

                if (thumbnail == nullptr)
                    thumbnail = std::make_unique<te::SmartThumbnail> (wac->edit.engine, proxy, *this, &wac->edit);
                else
                    thumbnail->setNewFile (proxy);
            }
            else
            {
                thumbnail = nullptr;
            }
        }
    }
}

void drawMidiClip (juce::Graphics& g, te::MidiClip& mc, juce::Rectangle<int> r, te::TimeRange tr)
{
    auto timeToX = [width = r.getWidth(), tr] (auto time)
    {
        return juce::roundToInt (((time - tr.getStart()) * width) / (tr.getLength()));
    };

    for (auto n : mc.getSequence().getNotes())
    {
        auto sBeat = mc.getStartBeat() + toDuration (n->getStartBeat());
        auto eBeat = mc.getStartBeat() + toDuration (n->getEndBeat());

        auto s = mc.edit.tempoSequence.toTime (sBeat);
        auto e = mc.edit.tempoSequence.toTime (eBeat);

        auto t1 = (double) timeToX (s) - r.getX();
        auto t2 = (double) timeToX (e) - r.getX();

        double y = (1.0 - double (n->getNoteNumber()) / 127.0) * r.getHeight();

        g.setColour (Colours::white.withAlpha (static_cast<float> (n->getVelocity()) / 127.0f));
        g.drawLine (float (t1), float (y), float (t2), float (y));
    }
}

//==============================================================================
MidiClipComponent::MidiClipComponent (EditViewState& evs, te::Clip::Ptr c)
    : ClipComponent (evs, c)
{
}

void MidiClipComponent::paint (Graphics& g)
{
    ClipComponent::paint (g);

    if (auto mc = getMidiClip())
    {
        auto& seq = mc->getSequence();
        for (auto n : seq.getNotes())
        {
            auto sBeat = mc->getStartBeat() + toDuration (n->getStartBeat());
            auto eBeat = mc->getStartBeat() + toDuration (n->getEndBeat());

            auto s = editViewState.beatToTime (sBeat);
            auto e = editViewState.beatToTime (eBeat);

            if (auto p = getParentComponent())
            {
                auto t1 = (double) editViewState.timeToX (s, p->getWidth()) - getX();
                auto t2 = (double) editViewState.timeToX (e, p->getWidth()) - getX();

                double y = (1.0 - double (n->getNoteNumber()) / 127.0) * getHeight();

                g.setColour (Colours::white.withAlpha (static_cast<float> (n->getVelocity()) / 127.0f));
                g.drawLine (float (t1), float (y), float (t2), float (y));
            }
        }
    }
}

//==============================================================================
RecordingClipComponent::RecordingClipComponent (te::Track::Ptr t, EditViewState& evs)
    : track (t), editViewState (evs)
{
    startTimerHz (10);
    initialiseThumbnailAndPunchTime();
}

void RecordingClipComponent::initialiseThumbnailAndPunchTime()
{
    if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
    {
        for (auto idi : at->edit.getEditInputDevices().getDevicesForTargetTrack (*at))
        {
            punchInTime = idi->getPunchInTime (at->itemID);

            if (idi->getRecordingFile (at->itemID).exists())
                thumbnail = at->edit.engine.getRecordingThumbnailManager().getThumbnailFor (idi->getRecordingFile (at->itemID));
        }
    }
}

void RecordingClipComponent::paint (Graphics& g)
{
    // a dark red take with the waveform drawn in paper as it's recorded
    g.fillAll (theme::record.withAlpha (0.28f));
    g.setColour (theme::record);
    g.drawRect (getLocalBounds());
    g.fillRect (getLocalBounds().removeFromTop (3));

    if (editViewState.drawWaveforms)
        drawThumbnail (g, theme::paper.withAlpha (0.9f));
}

void RecordingClipComponent::drawThumbnail (Graphics& g, Colour waveformColour) const
{
    if (thumbnail == nullptr)
        return;

    Rectangle<int> bounds;
    tracktion::TimeRange times;
    getBoundsAndTime (bounds, times);
    auto w = bounds.getWidth();

    if (w > 0 && w < 10000)
    {
        g.setColour (waveformColour);
        thumbnail->thumb->drawChannels (g, bounds, times.getStart().inSeconds(), times.getEnd().inSeconds(), jlimit (1.0f, 32.0f, editViewState.waveformZoom.get()));
    }
}

bool RecordingClipComponent::getBoundsAndTime (Rectangle<int>& bounds, tracktion::TimeRange& times) const
{
    auto editTimeToX = [this] (te::TimePosition t)
    {
        if (auto p = getParentComponent())
            return editViewState.timeToX (t, p->getWidth()) - getX();

        return 0;
    };

    auto xToEditTime = [this] (int x)
    {
        if (auto p = getParentComponent())
            return editViewState.xToTime (x + getX(), p->getWidth());

        return te::TimePosition();
    };

    bool hasLooped = false;
    auto& edit = track->edit;

    if (auto epc = edit.getTransport().getCurrentPlaybackContext())
    {
        auto localBounds = getLocalBounds();

        auto timeStarted = thumbnail->punchInTime;
        auto unloopedPos = timeStarted + te::TimeDuration::fromSeconds (thumbnail->thumb->getTotalLength());

        auto t1 = timeStarted;
        auto t2 = unloopedPos;

        if (epc->isLooping() && t2 >= epc->getLoopTimes().getEnd())
        {
            hasLooped = true;

            t1 = jmin (t1, epc->getLoopTimes().getStart());
            t2 = epc->getPosition();

            t1 = jmax (editViewState.viewX1.get(), t1);
            t2 = jmin (editViewState.viewX2.get(), t2);
        }
        else if (edit.recordingPunchInOut)
        {
            const auto in  = thumbnail->punchInTime;
            const auto out = edit.getTransport().getLoopRange().getEnd();

            t1 = jlimit (in, out, t1);
            t2 = jlimit (in, out, t2);
        }

        bounds = localBounds.withX (jmax (localBounds.getX(), editTimeToX (t1)))
                 .withRight (jmin (localBounds.getRight(), editTimeToX (t2)));

        auto loopRange = epc->getLoopTimes();
        const auto recordedTime = unloopedPos - toDuration (epc->getLoopTimes().getStart());
        const int numLoops = (int) (recordedTime / loopRange.getLength());

        const tracktion::TimeRange editTimes (xToEditTime (bounds.getX()),
                                              xToEditTime (bounds.getRight()));

        times = (editTimes + (loopRange.getLength() * numLoops)) - toDuration (timeStarted);
    }

    return hasLooped;
}

void RecordingClipComponent::timerCallback()
{
    updatePosition();
}

void RecordingClipComponent::updatePosition()
{
    auto& edit = track->edit;

    if (auto epc = edit.getTransport().getCurrentPlaybackContext())
    {
        auto t1 = punchInTime >= 0s ? punchInTime : edit.getTransport().getTimeWhenStarted();
        auto t2 = jmax (t1, epc->getUnloopedPosition());

        if (epc->isLooping())
        {
            auto loopTimes = epc->getLoopTimes();

            if (t2 >= loopTimes.getEnd())
            {
                t1 = jmin (t1, loopTimes.getStart());
                t2 = loopTimes.getEnd();
            }
        }
        else if (edit.recordingPunchInOut)
        {
            auto mr = edit.getTransport().getLoopRange();
            auto in  = mr.getStart();
            auto out = mr.getEnd();

            t1 = jlimit (in, out, t1);
            t2 = jlimit (in, out, t2);
        }

        t1 = jmax (t1, editViewState.viewX1.get());
        t2 = jmin (t2, editViewState.viewX2.get());

        if (auto p = getParentComponent())
        {
            int x1 = editViewState.timeToX (t1, p->getWidth());
            int x2 = editViewState.timeToX (t2, p->getWidth());

            setBounds (x1, 0, x2 - x1, p->getHeight());
            return;
        }
    }

    setBounds ({});
}

//==============================================================================
TrackHeaderComponent::TrackHeaderComponent (EditViewState& evs, te::Track::Ptr t)
    : editViewState (evs), track (t)
{
    Helpers::addAndMakeVisible (*this, { &trackName, &monitorButton, &armButton, &muteButton, &soloButton, &inputButton, &volume, &pan, &meter });
    monitorButton.setTooltip ("Monitor: hear this track's input in your headphones while you play or record");
    monitorButton.setMouseClickGrabsKeyboardFocus (false);

    armButton.setColour (TextButton::buttonOnColourId, theme::record);
    muteButton.setColour (TextButton::buttonOnColourId, theme::muted);
    soloButton.setColour (TextButton::buttonOnColourId, theme::solo);
    armButton.setTooltip ("Arm for recording");
    muteButton.setTooltip ("Mute");
    soloButton.setTooltip ("Solo");
    for (auto* b : { &armButton, &muteButton, &soloButton, &inputButton })
        b->setMouseClickGrabsKeyboardFocus (false);

    trackName.setText (t->getName(), dontSendNotification);
    trackName.setFont (FontOptions (14.0f, Font::bold));
    trackName.setEditable (false, true, false);
    trackName.setTooltip ("Double-click to rename");
    trackName.onTextChange = [this] { track->setName (trackName.getText()); };

    if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
    {
        if (auto vp = at->getVolumePlugin())
        {
            volume.setRange (-60.0, 6.0, 0.1);
            volume.setSkewFactorFromMidPoint (-12.0);
            volume.setValue (vp->getVolumeDb(), dontSendNotification);
            volume.setDoubleClickReturnValue (true, 0.0);
            volume.textFromValueFunction = [] (double v) { return v <= -59.9 ? String ("-inf dB") : String (v, 1) + " dB"; };
            volume.setPopupDisplayEnabled (true, true, nullptr);
            volume.setTooltip ("Volume (double-click for 0 dB)");
            volume.onValueChange = [this, vp] { vp->setVolumeDb ((float) volume.getValue()); };

            pan.setRange (-1.0, 1.0, 0.01);
            pan.setValue (vp->getPan(), dontSendNotification);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.textFromValueFunction = [] (double v) { return std::abs (v) < 0.005 ? String ("Center") : String (roundToInt (std::abs (v) * 100)) + (v < 0 ? "% L" : "% R"); };
            pan.setPopupDisplayEnabled (true, true, nullptr);
            pan.setTooltip ("Pan (double-click for center)");
            pan.onValueChange = [this, vp] { vp->setPan ((float) pan.getValue()); };
        }
    }
    else
    {
        volume.setVisible (false);
        pan.setVisible (false);
    }

    if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
    {
        inputButton.onClick = [this, at]
        {
            PopupMenu m;

            if (EngineHelpers::trackHasInput (*at))
            {
                bool ticked = EngineHelpers::isInputMonitoringEnabled (*at);
                m.addItem (1000, "Input Monitoring", true, ticked);
                m.addSeparator();
            }

            if (editViewState.showWaveDevices)
            {
                int id = 1;
                for (auto instance : at->edit.getAllInputDevices())
                {
                    if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                    {
                        bool ticked = instance->getTargets().getFirst() == at->itemID;
                        m.addItem (id++, instance->getInputDevice().getName(), true, ticked);
                    }
                }
            }

            if (editViewState.showMidiDevices)
            {
                m.addSeparator();

                int id = 100;
                for (auto instance : at->edit.getAllInputDevices())
                {
                    if (instance->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice)
                    {
                        bool ticked = instance->getTargets().getFirst() == at->itemID;
                        m.addItem (id++, instance->getInputDevice().getName(), true, ticked);
                    }
                }
            }

            int res = m.showAt (&inputButton);

            if (res == 1000)
            {
                EngineHelpers::enableInputMonitoring (*at, ! EngineHelpers::isInputMonitoringEnabled (*at));
            }
            else if (res >= 100)
            {
                int id = 100;
                for (auto instance : at->edit.getAllInputDevices())
                {
                    if (instance->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice)
                    {
                        if (id == res)
                            [[ maybe_unused ]] auto result = instance->setTarget (at->itemID, true, &at->edit.getUndoManager(), 0);

                        id++;
                    }
                }
            }
            else if (res >= 1)
            {
                int id = 1;
                for (auto instance : at->edit.getAllInputDevices())
                {
                    if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
                    {
                        if (id == res)
                            [[ maybe_unused ]] auto result = instance->setTarget (at->itemID, true, &at->edit.getUndoManager(), 0);

                        id++;
                    }
                }
            }
        };
        armButton.onClick = [this, at]
        {
            EngineHelpers::armTrack (*at, ! EngineHelpers::isTrackArmed (*at));
            armButton.setToggleState (EngineHelpers::isTrackArmed (*at), dontSendNotification);
            updateMeterSource();
        };
        monitorButton.onClick = [this, at]
        {
            // explicit on/off for the input feeding this track
            const bool on = ! EngineHelpers::isInputMonitoringEnabled (*at);
            for (auto instance : at->edit.getAllInputDevices())
                if (te::isOnTargetTrack (*instance, *at, 0))
                    instance->getInputDevice().setMonitorMode (on ? te::InputDevice::MonitorMode::on : te::InputDevice::MonitorMode::off);
            updateMonitor();
            updateMeterSource();
        };
        muteButton.onClick = [at] { at->setMute (! at->isMuted (false)); };
        soloButton.onClick = [at] { at->setSolo (! at->isSolo (false)); };

        armButton.setToggleState (EngineHelpers::isTrackArmed (*at), dontSendNotification);
    }
    else
    {
        armButton.setVisible (false);
        muteButton.setVisible (false);
        soloButton.setVisible (false);
    }

    track->state.addListener (this);
    inputsState = track->edit.state.getChildWithName (te::IDs::INPUTDEVICES);
    inputsState.addListener (this);

    valueTreePropertyChanged (track->state, te::IDs::mute);
    valueTreePropertyChanged (track->state, te::IDs::solo);
    valueTreePropertyChanged (inputsState, te::IDs::targetIndex);
    startTimerHz (4);
}

TrackHeaderComponent::~TrackHeaderComponent()
{
    track->state.removeListener (this);
}

void TrackHeaderComponent::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    if (te::TrackList::isTrack (v))
    {
        if (i == te::IDs::mute)
            muteButton.setToggleState ((bool)v[i], dontSendNotification);
        else if (i == te::IDs::solo)
            soloButton.setToggleState ((bool)v[i], dontSendNotification);
    }
    else if (v.hasType (te::IDs::INPUTDEVICES)
             || v.hasType (te::IDs::INPUTDEVICE)
             || v.hasType (te::IDs::INPUTDEVICEDESTINATION))
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
        {
            armButton.setEnabled (EngineHelpers::trackHasInput (*at));
            armButton.setToggleState (EngineHelpers::isTrackArmed (*at), dontSendNotification);
            updateInputName();
        }
    }
}

// Inputs change outside this track's own state (devices come and go), so keep checking
void TrackHeaderComponent::timerCallback()
{
    if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
    {
        armButton.setEnabled (EngineHelpers::trackHasInput (*at));
        armButton.setToggleState (EngineHelpers::isTrackArmed (*at), dontSendNotification);
        updateInputName();
        updateMonitor();
        updateMeterSource();
    }
}

// While a track is armed or monitoring, its meter shows the input (so you can
// set levels before you record); otherwise it shows what the track plays.
void TrackHeaderComponent::updateMeterSource()
{
    auto at = dynamic_cast<te::AudioTrack*> (track.get());
    if (at == nullptr) { meter.setVisible (false); return; }
    te::LevelMeasurer* source = nullptr;
    if (EngineHelpers::isTrackArmed (*at) || EngineHelpers::isInputMonitoringEnabled (*at))
        for (auto instance : at->edit.getAllInputDevices())
            if (te::isOnTargetTrack (*instance, *at, 0))
                source = &instance->getInputDevice().levelMeasurer;
    if (source == nullptr)
        if (auto lm = at->getLevelMeterPlugin())
            source = &lm->measurer;
    meter.setSource (source);
    meter.setTooltip (source != nullptr && EngineHelpers::isTrackArmed (*at) ? "Input level (set it so the loudest part stays out of the red). Click to reset the clip light."
                                                                             : "Track level. Click to reset the clip light.");
}

void TrackHeaderComponent::updateMonitor()
{
    auto at = dynamic_cast<te::AudioTrack*> (track.get());
    if (at == nullptr) { monitorButton.setVisible (false); return; }
    monitorButton.setEnabled (EngineHelpers::trackHasInput (*at));
    monitorButton.setToggleState (EngineHelpers::trackHasInput (*at) && EngineHelpers::isInputMonitoringEnabled (*at), dontSendNotification);
}

void TrackHeaderComponent::MonitorButton::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    auto c = getToggleState() ? theme::accent : theme::raised;
    if (! isEnabled()) c = c.withMultipliedAlpha (0.5f);
    else if (down) c = c.brighter (0.15f);
    else if (over) c = c.brighter (0.08f);
    g.setColour (c);
    g.fillEllipse (r);
    // headphones
    auto ink = getToggleState() ? theme::base : (isEnabled() ? theme::paper : theme::muted);
    auto b = r.reduced (r.getWidth() * 0.26f);
    const float cy = b.getCentreY() + b.getHeight() * 0.1f;
    Path band;
    band.addCentredArc (b.getCentreX(), cy, b.getWidth() * 0.45f, b.getHeight() * 0.45f, 0.0f,
                        -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
    g.setColour (ink);
    g.strokePath (band, PathStrokeType (1.6f));
    const float cupW = b.getWidth() * 0.22f, cupH = b.getHeight() * 0.38f;
    g.fillRoundedRectangle (b.getX() + b.getWidth() * 0.05f - cupW * 0.5f, cy - cupH * 0.15f, cupW, cupH, 1.5f);
    g.fillRoundedRectangle (b.getRight() - b.getWidth() * 0.05f - cupW * 0.5f, cy - cupH * 0.15f, cupW, cupH, 1.5f);
}

// The input button shows what the track records from
void TrackHeaderComponent::updateInputName()
{
    auto at = dynamic_cast<te::AudioTrack*> (track.get());
    if (at == nullptr) return;
    String name;
    for (auto instance : at->edit.getAllInputDevices())
        if (te::isOnTargetTrack (*instance, *at, 0))
            name = instance->getInputDevice().getName();
    inputButton.setButtonText (name.isEmpty() ? "No input" : name);
    inputButton.setTooltip (name.isEmpty() ? "Choose which input this track records" : "Records from " + name + " (click to change)");
}

void TrackHeaderComponent::paint (Graphics& g)
{
    const bool sel = editViewState.selectionManager.isSelected (track.get());
    g.setColour (sel ? theme::raised : theme::surface);
    g.fillRect (getLocalBounds());
    g.setColour (track->getColour());
    g.fillRect (getLocalBounds().removeFromLeft (4));
    g.setColour (theme::strong);
    g.fillRect (getLocalBounds().removeFromBottom (2).withTrimmedLeft (4)); // the edge you drag to resize
    if (volume.isVisible())
    {
        g.setColour (theme::muted);
        g.setFont (FontOptions (10.0f));
        g.drawText ("VOL", volume.getBounds().withX (volume.getX() - 30).withWidth (28), Justification::centredLeft);
        g.drawText ("PAN", pan.getBounds().withX (pan.getX() - 30).withWidth (28), Justification::centredLeft);
    }
}

void TrackHeaderComponent::mouseDown (const MouseEvent& e)
{
    resizing = overResizeEdge (e);
    heightAtDragStart = getHeight();
    if (! resizing) editViewState.selectionManager.selectOnly (track.get());
}

// Drag the bottom edge: this track's height (Alt: every track)
void TrackHeaderComponent::mouseDrag (const MouseEvent& e)
{
    if (! resizing) return;
    const int h = jlimit (EditComponent::minTrackHeight, EditComponent::maxTrackHeight, heightAtDragStart + e.getDistanceFromDragStartY());
    if (e.mods.isAltDown())
        for (auto t : te::getAllTracks (track->edit)) t->state.setProperty ("basementHeight", h, nullptr);
    else
        track->state.setProperty ("basementHeight", h, nullptr);
    if (auto ec = findParentComponentOfClass<EditComponent>()) ec->relayout();
}

void TrackHeaderComponent::mouseUp (const MouseEvent&) { resizing = false; }

void TrackHeaderComponent::mouseMove (const MouseEvent& e)
{
    setMouseCursor (overResizeEdge (e) ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
}

void TrackHeaderComponent::resized()
{
    meter.setBounds (getLocalBounds().removeFromRight (16).reduced (4, 6));
    auto r = getLocalBounds().withTrimmedRight (14).reduced (10, 6).withTrimmedLeft (2);
    auto top = r.removeFromTop (24);
    for (auto* b : std::initializer_list<Component*> { &soloButton, &muteButton, &armButton, &monitorButton })
    {
        b->setBounds (top.removeFromRight (24));
        top.removeFromRight (4);
    }
    trackName.setBounds (top);
    // short tracks show just the name and buttons, then the input, then the faders
    const bool isAudio = dynamic_cast<te::AudioTrack*> (track.get()) != nullptr;
    inputButton.setVisible (getHeight() >= 62);
    volume.setVisible (isAudio && getHeight() >= 80);
    pan.setVisible (volume.isVisible());
    r.removeFromTop (6);
    inputButton.setBounds (r.removeFromTop (22).removeFromLeft (170));
    auto bottom = r.removeFromBottom (16);
    bottom.removeFromLeft (30);
    volume.setBounds (bottom.removeFromLeft (100));
    bottom.removeFromLeft (38);
    pan.setBounds (bottom);
}

//==============================================================================
PluginComponent::PluginComponent (EditViewState& evs, te::Plugin::Ptr p)
    : editViewState (evs), plugin (p)
{
    setButtonText (plugin->getName().substring (0, 18));
    setTooltip (plugin->getName() + ": click to open, right-click to remove");
}

PluginComponent::~PluginComponent()
{
}

void PluginComponent::clicked (const ModifierKeys& modifiers)
{
    editViewState.selectionManager.selectOnly (plugin.get());
    if (modifiers.isPopupMenu())
    {
        PopupMenu m;
        m.addItem ("Delete", [this] { plugin->deleteFromParent(); });
        m.showAt (this);
    }
    else
    {
        auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin.get());
        basementLog ("open plugin " + plugin->getName() + ": modal=" + String (ModalComponentManager::getInstance()->getNumModalComponents())
                     + " instance=" + String ((int) (ext == nullptr || ext->getAudioPluginInstance() != nullptr))
                     + (ext ? " error=" + ext->getLoadError() : String()));
        plugin->showWindowExplicitly();
        basementLog ("  window showing=" + String ((int) plugin->windowState->isWindowShowing()));
    }
}

//==============================================================================
TrackFooterComponent::TrackFooterComponent (EditViewState& evs, te::Track::Ptr t)
    : editViewState (evs), track (t)
{
    addAndMakeVisible (addButton);

    buildPlugins();

    track->state.addListener (this);

    addButton.onClick = [this]
    {
        showEffectsMenu (track, &editViewState.selectionManager, &addButton);
    };
}

TrackFooterComponent::~TrackFooterComponent()
{
    track->state.removeListener (this);
}

void TrackFooterComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c)
{
    if (c.hasType (te::IDs::PLUGIN))
        markAndUpdate (updatePlugins);
}

void TrackFooterComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int)
{
    if (c.hasType (te::IDs::PLUGIN))
        markAndUpdate (updatePlugins);
}

void TrackFooterComponent::valueTreeChildOrderChanged (juce::ValueTree&, int, int)
{
    markAndUpdate (updatePlugins);
}

void TrackFooterComponent::paint (Graphics& g)
{
    g.setColour (editViewState.selectionManager.isSelected (track.get()) ? theme::raised : theme::surface);
    g.fillRect (getLocalBounds());
    if (plugins.isEmpty())
    {
        g.setColour (theme::muted);
        g.setFont (FontOptions (11.0f));
        g.drawText ("Effects", getLocalBounds().reduced (8, 6).withTrimmedLeft (30).removeFromTop (22), Justification::centredLeft);
    }
}

void TrackFooterComponent::mouseDown (const MouseEvent&)
{
    editViewState.selectionManager.selectOnly (track.get());
}

void TrackFooterComponent::resized()
{
    // the effect chain, top to bottom (the track's own volume/meter aren't shown)
    auto r = getLocalBounds().reduced (8, 6);
    addButton.setBounds (r.removeFromTop (22).removeFromLeft (22));
    addButton.setTooltip ("Add an effect");
    r.removeFromTop (3);
    for (auto p : plugins)
    {
        p->setVisible (r.getHeight() >= 16);
        p->setBounds (r.removeFromTop (16));
        r.removeFromTop (2);
    }
}

void TrackFooterComponent::handleAsyncUpdate()
{
    if (compareAndReset (updatePlugins))
        buildPlugins();
}

void TrackFooterComponent::buildPlugins()
{
    plugins.clear();

    for (auto plugin : track->pluginList)
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (track.get()))
            if (plugin == at->getVolumePlugin() || plugin == at->getLevelMeterPlugin())
                continue;
        auto p = new PluginComponent (editViewState, plugin);
        addAndMakeVisible (p);
        plugins.add (p);
    }
    resized();
}

//==============================================================================
TrackComponent::TrackComponent (EditViewState& evs, te::Track::Ptr t)
    : editViewState (evs), track (t)
{
    track->state.addListener (this);
    track->edit.getTransport().addChangeListener (this);

    markAndUpdate (updateClips);
}

TrackComponent::~TrackComponent()
{
    track->state.removeListener (this);
    track->edit.getTransport().removeChangeListener (this);
}

void TrackComponent::paint (Graphics& g)
{
    g.fillAll (editViewState.selectionManager.isSelected (track.get()) ? theme::surface : theme::subtle);

    // a faint line per bar
    auto& ts = editViewState.edit.tempoSequence;
    const int num = jmax (1, ts.getTimeSig (0)->numerator.get());
    g.setColour (theme::strong.withAlpha (0.6f));
    auto b0 = (int) std::floor (ts.toBeats (editViewState.viewX1.get()).inBeats() / num);
    for (int bar = jmax (0, b0); bar < b0 + 4000; ++bar)
    {
        int x = editViewState.timeToX (ts.toTime (te::BeatPosition::fromBeats (bar * num)), getWidth());
        if (x > getWidth()) break;
        g.fillRect (x, 0, 1, getHeight());
    }
}

void TrackComponent::mouseDown (const MouseEvent& e)
{
    if (auto* p = automationParam())
    {
        // automation: grab a point, or add one where you click
        auto& um = track->edit.getUndoManager();
        um.beginNewTransaction ("Automation");
        draggingPoint = pointNear (e.getPosition());
        if (draggingPoint < 0)
        {
            auto t = editViewState.snap (te::TimePosition::fromSeconds (jmax (0.0, editViewState.xToTime (e.x, getWidth()).inSeconds())), e.mods.isShiftDown());
            draggingPoint = p->getCurve().addPoint (t, valueAtY (e.y), 0.0f, &um);
        }
        repaint();
        return;
    }
    if (! e.mods.isShiftDown()) editViewState.selectionManager.selectOnly (track.get());
}

te::AutomatableParameter* TrackComponent::automationParam() const
{
    auto at = dynamic_cast<te::AudioTrack*> (track.get());
    if (at == nullptr) return nullptr;
    auto vp = at->getVolumePlugin();
    if (vp == nullptr) return nullptr;
    switch (editViewState.automationMode.get())
    {
        case 1: return vp->volParam.get();
        case 2: return vp->panParam.get();
        default: return nullptr;
    }
}

float TrackComponent::valueAtY (int y) const
{
    auto* p = automationParam();
    const float norm = jlimit (0.0f, 1.0f, 1.0f - float (y - 4) / float (jmax (1, getHeight() - 8)));
    return p != nullptr ? p->valueRange.convertFrom0to1 (norm) : 0.0f;
}

int TrackComponent::yForValue (float v) const
{
    auto* p = automationParam();
    const float norm = p != nullptr ? p->valueRange.convertTo0to1 (v) : 0.0f;
    return 4 + roundToInt ((1.0f - norm) * float (getHeight() - 8));
}

int TrackComponent::pointNear (Point<int> pos) const
{
    auto* p = automationParam();
    if (p == nullptr) return -1;
    auto& curve = p->getCurve();
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        Point<int> pt (editViewState.timeToX (curve.getPointTime (i), getWidth()), yForValue (curve.getPointValue (i)));
        if (pt.getDistanceFrom (pos) <= 7) return i;
    }
    return -1;
}

void TrackComponent::mouseDoubleClick (const MouseEvent& e)
{
    if (auto* p = automationParam())
        if (auto i = pointNear (e.getPosition()); i >= 0)
        {
            track->edit.getUndoManager().beginNewTransaction ("Remove automation point");
            p->getCurve().removePoint (i, &track->edit.getUndoManager());
            draggingPoint = -1;
            repaint();
        }
}

/** In automation mode the clips step aside so clicks land on the curve */
void TrackComponent::updateClipInterception()
{
    const bool clipsClickable = automationParam() == nullptr;
    for (auto* c : clips) c->setInterceptsMouseClicks (clipsClickable, clipsClickable);
}

void TrackComponent::paintOverChildren (Graphics& g)
{
    auto* p = automationParam();
    if (p == nullptr) return;
    auto& curve = p->getCurve();
    const auto colour = editViewState.automationMode.get() == 1 ? theme::solo : theme::accent2;
    g.setColour (theme::base.withAlpha (0.35f));
    g.fillRect (getLocalBounds());

    // the line: follows the curve (or sits at the current value when there are no points)
    Path line;
    for (int x = 0; x <= getWidth(); x += 3)
    {
        const auto t = editViewState.xToTime (x, getWidth());
        const float v = curve.getNumPoints() > 0 ? curve.getValueAt (t, p->getCurrentValue()) : p->getCurrentValue();
        if (x == 0) line.startNewSubPath ((float) x, (float) yForValue (v));
        else line.lineTo ((float) x, (float) yForValue (v));
    }
    g.setColour (colour);
    g.strokePath (line, PathStrokeType (2.0f));
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const float x = (float) editViewState.timeToX (curve.getPointTime (i), getWidth()), y = (float) yForValue (curve.getPointValue (i));
        g.setColour (theme::base);
        g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
        g.setColour (colour);
        g.drawEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f, 2.0f);
    }
    g.setColour (colour);
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText ((editViewState.automationMode.get() == 1 ? "Volume: " : "Pan: ") + p->getCurrentValueAsStringWithLabel()
                    + (curve.getNumPoints() == 0 ? "   (click to add points)" : String()),
                getLocalBounds().reduced (8, 4), Justification::bottomLeft);
}

void TrackComponent::mouseDrag (const MouseEvent& e)
{
    if (auto* p = automationParam())
    {
        if (draggingPoint < 0) return;
        auto& curve = p->getCurve();
        // a point stays between its neighbours
        auto t = editViewState.snap (te::TimePosition::fromSeconds (jmax (0.0, editViewState.xToTime (e.x, getWidth()).inSeconds())), e.mods.isShiftDown());
        if (draggingPoint > 0) t = jmax (t, curve.getPointTime (draggingPoint - 1));
        if (draggingPoint < curve.getNumPoints() - 1) t = jmin (t, curve.getPointTime (draggingPoint + 1));
        draggingPoint = curve.movePoint (*p, draggingPoint, t, valueAtY (e.y), false, &track->edit.getUndoManager());
        repaint();
        return;
    }
    if (auto ec = findParentComponentOfClass<EditComponent>())
    {
        auto ev = e.getEventRelativeTo (ec);
        ec->lassoDrag (Rectangle<int> (ev.getMouseDownPosition(), ev.getPosition()), e.mods.isShiftDown());
    }
}

void TrackComponent::mouseUp (const MouseEvent& e)
{
    if (automationParam() != nullptr) { draggingPoint = -1; return; }
    if (auto ec = findParentComponentOfClass<EditComponent>()) ec->lassoEnd();
    // a click (not a box-select) on an empty part of the lane moves the playhead there
    if (! e.mouseWasDraggedSinceMouseDown())
        track->edit.getTransport().setPosition (te::TimePosition::fromSeconds (jmax (0.0, editViewState.xToTime (e.x, getWidth()).inSeconds())));
}

void TrackComponent::changeListenerCallback (ChangeBroadcaster*)
{
    markAndUpdate (updateRecordClips);
}

void TrackComponent::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    if (te::Clip::isClipState (v))
    {
        if (i == te::IDs::start
            || i == te::IDs::length)
        {
            markAndUpdate (updatePositions);
        }
    }
}

void TrackComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c)
{
    if (te::Clip::isClipState (c))
        markAndUpdate (updateClips);
}

void TrackComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int)
{
    if (te::Clip::isClipState (c))
        markAndUpdate (updateClips);
}

void TrackComponent::valueTreeChildOrderChanged (juce::ValueTree& v, int a, int b)
{
    if (te::Clip::isClipState (v.getChild (a)))
        markAndUpdate (updatePositions);
    else if (te::Clip::isClipState (v.getChild (b)))
        markAndUpdate (updatePositions);
}

void TrackComponent::handleAsyncUpdate()
{
    if (compareAndReset (updateClips))
        buildClips();
    if (compareAndReset (updatePositions))
        resized();
    if (compareAndReset (updateRecordClips))
        buildRecordClips();
}

void TrackComponent::resized()
{
    updateClipInterception();
    for (auto cc : clips)
    {
        auto& c = cc->getClip();
        auto pos = c.getPosition();
        int x1 = editViewState.timeToX (pos.getStart(), getWidth());
        int x2 = editViewState.timeToX (pos.getEnd(), getWidth());

        cc->setBounds (x1, 0, x2 - x1, getHeight());
    }
}

void TrackComponent::buildClips()
{
    clips.clear();

    if (auto ct = dynamic_cast<te::ClipTrack*> (track.get()))
    {
        for (auto c : ct->getClips())
        {
            ClipComponent* cc = nullptr;

            if (dynamic_cast<te::WaveAudioClip*> (c))
                cc = new AudioClipComponent (editViewState, c);
            else if (dynamic_cast<te::MidiClip*> (c))
                cc = new MidiClipComponent (editViewState, c);
            else
                cc = new ClipComponent (editViewState, c);

            clips.add (cc);
            addAndMakeVisible (cc);
        }
    }

    resized();
}

void TrackComponent::buildRecordClips()
{
    bool needed = false;

    if (track->edit.getTransport().isRecording())
    {
        for (auto in : track->edit.getAllInputDevices())
        {
            if (in->isRecordingActive() && track->itemID == in->getTargets().getFirst())
            {
                needed = true;
                break;
            }
        }
    }

    if (needed)
    {
        recordingClip = std::make_unique<RecordingClipComponent> (track, editViewState);
        addAndMakeVisible (*recordingClip);
    }
    else
    {
        recordingClip = nullptr;
    }
}

//==============================================================================
PlayheadComponent::PlayheadComponent (te::Edit& e , EditViewState& evs)
    : edit (e), editViewState (evs)
{
    startTimerHz (30);
}

void PlayheadComponent::paint (Graphics& g)
{
    g.setColour (edit.getTransport().isRecording() ? theme::record : theme::paper);
    g.fillRect (xPosition, 0, 2, getHeight());
}

bool PlayheadComponent::hitTest (int x, int)
{
    if (std::abs (x - xPosition) <= 3)
        return true;

    return false;
}

void PlayheadComponent::mouseDown (const MouseEvent&)
{
    edit.getTransport().setUserDragging (true);
}

void PlayheadComponent::mouseUp (const MouseEvent&)
{
    edit.getTransport().setUserDragging (false);
}

void PlayheadComponent::mouseDrag (const MouseEvent& e)
{
    auto t = editViewState.xToTime (e.x, getWidth());
    edit.getTransport().setPosition (t);
    timerCallback();
}

void PlayheadComponent::timerCallback()
{
    if (firstTimer)
    {
        // On Linux, don't set the mouse cursor until after the Component has appeared
        firstTimer = false;
        setMouseCursor (MouseCursor::LeftRightResizeCursor);
    }

    int newX = editViewState.timeToX (edit.getTransport().getPosition(), getWidth());
    if (newX != xPosition)
    {
        repaint (jmin (newX, xPosition) - 1, 0, jmax (newX, xPosition) - jmin (newX, xPosition) + 3, getHeight());
        xPosition = newX;
    }
}

//==============================================================================
EditComponent::EditComponent (te::Edit& e, te::SelectionManager& sm)
    : edit (e), editViewState (e, sm)
{
    edit.state.addListener (this);
    editViewState.selectionManager.addChangeListener (this);

    addAndMakeVisible (playhead);
    addAndMakeVisible (ruler);

    markAndUpdate (updateTracks);
}

EditComponent::~EditComponent()
{
    editViewState.selectionManager.removeChangeListener (this);
    edit.state.removeListener (this);
}

void EditComponent::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    if (v.hasType (IDs::EDITVIEWSTATE))
    {
        if (i == IDs::viewX1
            || i == IDs::viewX2
            || i == IDs::viewY)
        {
            markAndUpdate (updateZoom);
        }
        else if (i == IDs::showHeaders
                 || i == IDs::showFooters)
        {
            markAndUpdate (updateZoom);
        }
        else if (i == IDs::drawWaveforms)
        {
            repaint();
        }
        else if (i == Identifier ("waveformZoom"))
        {
            for (auto* t : tracks) t->repaint();
            for (auto* t : tracks) for (auto* c : t->getClipComponents()) c->repaint();
        }
        else if (i == Identifier ("automationMode"))
        {
            for (auto* t : tracks) { t->updateClipInterception(); t->repaint(); }
        }
    }
}

void EditComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c)
{
    if (te::TrackList::isTrack (c))
        markAndUpdate (updateTracks);
}

void EditComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int)
{
    if (te::TrackList::isTrack (c))
        markAndUpdate (updateTracks);
}

void EditComponent::valueTreeChildOrderChanged (juce::ValueTree& v, int a, int b)
{
    if (te::TrackList::isTrack (v.getChild (a)))
        markAndUpdate (updateTracks);
    else if (te::TrackList::isTrack (v.getChild (b)))
        markAndUpdate (updateTracks);
}

void EditComponent::handleAsyncUpdate()
{
    if (compareAndReset (updateTracks))
        buildTracks();
    if (compareAndReset (updateZoom))
        resized();
}

void EditComponent::paint (Graphics& g)
{
    g.fillAll (theme::base);
    g.setColour (theme::surface);
    g.fillRect (0, 0, getWidth(), rulerHeight);
    if (tracks.isEmpty())
    {
        g.setColour (theme::muted);
        g.setFont (FontOptions (15.0f));
        g.drawText ("Drag audio files here, or add a track to record.", getLocalBounds().withTrimmedTop (rulerHeight), Justification::centred);
    }
}

// Ctrl + wheel zooms around the mouse, Shift + wheel scrolls through time,
// the wheel on its own scrolls the tracks
void EditComponent::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    auto ev = e.getEventRelativeTo (this);
    const int laneW = jmax (1, getWidth() - headerWidth - footerWidth);
    const double x1 = editViewState.viewX1.get().inSeconds(), x2 = editViewState.viewX2.get().inSeconds(), len = x2 - x1;
    const float dy = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : w.deltaX;
    if (ev.mods.isCtrlDown() || ev.mods.isCommandDown())
    {
        const double at = x1 + len * jlimit (0.0, 1.0, double (ev.x - headerWidth) / laneW);
        const double f = dy > 0 ? 0.8 : 1.25, newLen = jlimit (1.0, 3600.0, len * f);
        const double n1 = jmax (0.0, at - (at - x1) * (newLen / len));
        editViewState.viewX1 = te::TimePosition::fromSeconds (n1);
        editViewState.viewX2 = te::TimePosition::fromSeconds (n1 + newLen);
    }
    else if (ev.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const double n1 = jmax (0.0, x1 - dy * len * 0.25);
        editViewState.viewX1 = te::TimePosition::fromSeconds (n1);
        editViewState.viewX2 = te::TimePosition::fromSeconds (n1 + len);
    }
    else
    {
        double content = 0;
        for (auto t : tracks) content += heightOf (*t->getTrack()) + trackGap;
        const double visible = getHeight() - rulerHeight;
        editViewState.viewY = jlimit (jmin (0.0, visible - content - 40), 0.0, editViewState.viewY.get() + dy * 240.0);
    }
}

void EditComponent::lassoDrag (Rectangle<int> area, bool add)
{
    lasso = area;
    auto& sm = editViewState.selectionManager;
    if (! add) sm.deselectAll();
    for (auto* t : tracks)
        for (auto* cc : t->getClipComponents())
            if (getLocalArea (cc, cc->getLocalBounds()).intersects (area))
                sm.select (&cc->getClip(), true);
    repaint();
}

void EditComponent::paintOverChildren (Graphics& g)
{
    if (lasso.isEmpty()) return;
    g.setColour (theme::accent.withAlpha (0.12f));
    g.fillRect (lasso);
    g.setColour (theme::accent.withAlpha (0.8f));
    g.drawRect (lasso, 1);
}

te::AudioTrack* EditComponent::audioTrackAt (Point<int> p) const
{
    for (auto* t : tracks)
        if (t->getBounds().withX (0).withWidth (getWidth()).contains (p))
            return dynamic_cast<te::AudioTrack*> (t->getTrack());
    return nullptr;
}

void EditComponent::resized()
{
    jassert (headers.size() == tracks.size());

    playhead.setBounds (getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedRight (footerWidth).withTrimmedTop (rulerHeight));
    ruler.setBounds (getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedRight (footerWidth).removeFromTop (rulerHeight));

    int y = rulerHeight + roundToInt (editViewState.viewY.get());
    for (int i = 0; i < jmin (headers.size(), tracks.size()); i++)
    {
        auto h = headers[i];
        auto t = tracks[i];
        auto f = footers[i];

        const int th = heightOf (*t->getTrack());
        h->setBounds (0, y, headerWidth, th);
        t->setBounds (headerWidth, y, getWidth() - headerWidth - footerWidth, th);
        f->setBounds (getWidth() - footerWidth, y, footerWidth, th);

        y += th + trackGap;
    }

    for (auto t : tracks)
        t->resized();
}

void EditComponent::buildTracks()
{
    tracks.clear();
    headers.clear();
    footers.clear();

    for (auto t : getAllTracks (edit))
    {
        TrackComponent* c = nullptr;

        if (t->isMasterTrack())
        {
            if (editViewState.showMasterTrack)
                c = new TrackComponent (editViewState, t);
        }
        else if (t->isTempoTrack())
        {
            if (editViewState.showGlobalTrack)
                c = new TrackComponent (editViewState, t);
        }
        else if (t->isMarkerTrack())
        {
            if (editViewState.showMarkerTrack)
                c = new TrackComponent (editViewState, t);
        }
        else if (t->isChordTrack())
        {
            if (editViewState.showChordTrack)
                c = new TrackComponent (editViewState, t);
        }
        else if (t->isArrangerTrack())
        {
            if (editViewState.showArrangerTrack)
                c = new TrackComponent (editViewState, t);
        }
        else
        {
            c = new TrackComponent (editViewState, t);
        }

        if (c != nullptr)
        {
            tracks.add (c);
            addAndMakeVisible (c);

            auto h = new TrackHeaderComponent (editViewState, t);
            headers.add (h);
            addAndMakeVisible (h);

            auto f = new TrackFooterComponent (editViewState, t);
            footers.add (f);
            addAndMakeVisible (f);
        }
    }

    playhead.toFront (false);
    ruler.toFront (false);
    resized();
    repaint();
}

//==============================================================================
void TimeRulerComponent::paint (Graphics& g)
{
    g.fillAll (theme::surface);
    // the loop range, while looping is on
    if (edit.getTransport().looping.get())
    {
        auto lr = edit.getTransport().getLoopRange();
        const int x1 = editViewState.timeToX (lr.getStart(), getWidth()), x2 = editViewState.timeToX (lr.getEnd(), getWidth());
        g.setColour (theme::accent.withAlpha (0.25f));
        g.fillRect (x1, 0, jmax (1, x2 - x1), getHeight());
        g.setColour (theme::accent);
        g.fillRect (x1, 0, 1, getHeight());
        g.fillRect (x2, 0, 1, getHeight());
    }
    auto& ts = edit.tempoSequence;
    const int num = jmax (1, ts.getTimeSig (0)->numerator.get());
    // label every bar, or every 2nd/4th/... bar when zoomed out
    const double pxPerBar = editViewState.timeToX (ts.toTime (te::BeatPosition::fromBeats (num)), getWidth())
                          - editViewState.timeToX (ts.toTime (te::BeatPosition::fromBeats (0)), getWidth());
    int every = 1;
    while (pxPerBar * every < 44 && every < 4096) every *= 2;
    g.setFont (FontOptions (11.0f));
    auto b0 = (int) std::floor (ts.toBeats (editViewState.viewX1.get()).inBeats() / num);
    for (int bar = jmax (0, b0 - b0 % every); bar < b0 + 100000; bar += every)
    {
        int x = editViewState.timeToX (ts.toTime (te::BeatPosition::fromBeats (bar * num)), getWidth());
        if (x > getWidth()) break;
        g.setColour (theme::strong);
        g.fillRect (x, getHeight() - 10, 1, 10);
        g.setColour (theme::muted);
        g.drawText (String (bar + 1), x + 4, markerLaneH, 40, getHeight() - markerLaneH - 2, Justification::centredLeft);
    }
    // markers: a flag and a name in the top lane
    g.setColour (theme::base.withAlpha (0.5f));
    g.fillRect (getLocalBounds().removeFromTop (markerLaneH));
    g.setFont (FontOptions (11.0f, Font::bold));
    for (auto m : markers::list (edit))
    {
        const int x = editViewState.timeToX (markers::timeOf (m), getWidth());
        if (x < -150 || x > getWidth()) continue;
        auto name = m["name"].toString();
        const int w = GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 12;
        g.setColour (theme::accent2);
        g.fillRect (x, 0, 2, getHeight());
        g.fillRoundedRectangle (Rectangle<float> ((float) x, 1.0f, (float) w, (float) markerLaneH - 3.0f), 3.0f);
        g.setColour (theme::base);
        g.drawText (name, x + 6, 1, w - 6, markerLaneH - 3, Justification::centredLeft);
    }

    int px = editViewState.timeToX (edit.getTransport().getPosition(), getWidth());
    Path p; p.addTriangle ((float) px - 6, (float) markerLaneH, (float) px + 6, (float) markerLaneH, (float) px, (float) markerLaneH + 9);
    g.setColour (theme::paper);
    g.fillPath (p);
}

ValueTree TimeRulerComponent::markerAt (int x) const
{
    // the flag is the clickable part: from the marker line to the end of its name
    Font f (FontOptions (11.0f, Font::bold));
    for (auto m : markers::list (edit))
    {
        const int mx = editViewState.timeToX (markers::timeOf (m), getWidth());
        if (x >= mx - 3 && x <= mx + GlyphArrangement::getStringWidthInt (f, m["name"].toString()) + 12) return m;
    }
    return {};
}

void TimeRulerComponent::mouseDown (const MouseEvent& e)
{
    movedMarker = false;
    if (e.y < markerLaneH)
    {
        auto m = markerAt (e.x);
        if (e.mods.isPopupMenu())
        {
            PopupMenu menu;
            const auto t = te::TimePosition::fromSeconds (jmax (0.0, editViewState.xToTime (e.x, getWidth()).inSeconds()));
            if (m.isValid())
            {
                PopupMenu names;
                for (auto& n : markers::quickNames()) names.addItem (n, [this, m, n] { edit.getUndoManager().beginNewTransaction ("Rename marker"); auto mm = m; mm.setProperty ("name", n, &edit.getUndoManager()); });
                menu.addSubMenu ("Name", names);
                menu.addItem ("Rename...", [this, m] { renameMarker (m); });
                menu.addItem ("Delete Marker", [this, m] { edit.getUndoManager().beginNewTransaction ("Delete marker"); markers::list (edit).removeChild (m, &edit.getUndoManager()); });
            }
            else
            {
                for (auto& n : markers::quickNames()) menu.addItem ("Add \"" + n + "\" here", [this, t, n] { markers::add (edit, editViewState.snap (t), n); });
                menu.addItem ("Add Marker here", [this, t] { markers::add (edit, editViewState.snap (t)); });
            }
            menu.showMenuAsync (PopupMenu::Options());
            return;
        }
        if (m.isValid())
        {
            draggedMarker = m;
            edit.getTransport().setPosition (markers::timeOf (m)); // click a marker: jump there
            return;
        }
    }
    mouseDrag (e);
}

void TimeRulerComponent::mouseDrag (const MouseEvent& e)
{
    const auto t = te::TimePosition::fromSeconds (jmax (0.0, editViewState.xToTime (e.x, getWidth()).inSeconds()));
    if (draggedMarker.isValid())
    {
        if (! e.mouseWasDraggedSinceMouseDown()) return;
        if (! movedMarker) { edit.getUndoManager().beginNewTransaction ("Move marker"); movedMarker = true; }
        draggedMarker.setProperty ("time", editViewState.snap (t, e.mods.isShiftDown()).inSeconds(), &edit.getUndoManager());
        return;
    }
    edit.getTransport().setPosition (t);
}

void TimeRulerComponent::mouseDoubleClick (const MouseEvent& e)
{
    if (e.y >= markerLaneH) return;
    if (auto m = markerAt (e.x); m.isValid()) renameMarker (m);
    else markers::add (edit, editViewState.snap (te::TimePosition::fromSeconds (jmax (0.0, editViewState.xToTime (e.x, getWidth()).inSeconds()))));
}

void TimeRulerComponent::renameMarker (ValueTree m)
{
    auto* w = new AlertWindow ("Rename Marker", {}, MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", m["name"].toString());
    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([this, w, m] (int r) mutable
    {
        auto name = w->getTextEditorContents ("name").trim();
        if (r == 1 && name.isNotEmpty()) { edit.getUndoManager().beginNewTransaction ("Rename marker"); m.setProperty ("name", name, &edit.getUndoManager()); }
    }), true);
}



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
}

void ClipComponent::mouseDown (const MouseEvent&)
{
    editViewState.selectionManager.selectOnly (clip.get());
    dragStart = clip->getPosition().getStart();
    dragging = false;
}

// Drag a clip left or right along its track
void ClipComponent::mouseDrag (const MouseEvent& e)
{
    auto* lane = getParentComponent();
    if (lane == nullptr || ! e.mouseWasDraggedSinceMouseDown()) return;
    if (! dragging) { clip->edit.getUndoManager().beginNewTransaction ("Move clip"); dragging = true; }
    auto dx = editViewState.xToTime (e.getDistanceFromDragStartX(), lane->getWidth()) - editViewState.xToTime (0, lane->getWidth());
    clip->setStart (te::TimePosition::fromSeconds (jmax (0.0, (dragStart + dx).inSeconds())), false, true);
}

void ClipComponent::mouseUp (const MouseEvent&) { dragging = false; }

//==============================================================================
AudioClipComponent::AudioClipComponent (EditViewState& evs, te::Clip::Ptr c)
    : ClipComponent (evs, c)
{
    updateThumbnail();
}

void AudioClipComponent::paint (Graphics& g)
{
    ClipComponent::paint (g);

    if (editViewState.drawWaveforms && thumbnail != nullptr)
        drawWaveform (g, *getWaveAudioClip(), *thumbnail, theme::paper.withAlpha (0.7f),
                      0, getWidth(), 18, getHeight() - 22, 0);
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
    const auto gain = c.getGain();
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
    g.fillAll (Colours::red.withAlpha (0.5f));
    g.setColour (Colours::black);
    g.drawRect (getLocalBounds());

    if (editViewState.drawWaveforms)
        drawThumbnail (g, Colours::black.withAlpha (0.5f));
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
        thumbnail->thumb->drawChannels (g, bounds, times.getStart().inSeconds(), times.getEnd().inSeconds(), 1.0f);
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
    Helpers::addAndMakeVisible (*this, { &trackName, &armButton, &muteButton, &soloButton, &inputButton, &volume, &pan });

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
    startTimerHz (2);
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
    }
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
    if (volume.isVisible())
    {
        g.setColour (theme::muted);
        g.setFont (FontOptions (10.0f));
        g.drawText ("VOL", volume.getBounds().withX (volume.getX() - 30).withWidth (28), Justification::centredLeft);
        g.drawText ("PAN", pan.getBounds().withX (pan.getX() - 30).withWidth (28), Justification::centredLeft);
    }
}

void TrackHeaderComponent::mouseDown (const MouseEvent&)
{
    editViewState.selectionManager.selectOnly (track.get());
}

void TrackHeaderComponent::resized()
{
    auto r = getLocalBounds().reduced (10, 6).withTrimmedLeft (2);
    auto top = r.removeFromTop (24);
    for (auto* b : { &soloButton, &muteButton, &armButton })
    {
        b->setBounds (top.removeFromRight (24));
        top.removeFromRight (4);
    }
    trackName.setBounds (top);
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
        plugin->showWindowExplicitly();
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
        if (auto plugin = showMenuAndCreatePlugin (track->edit))
            track->pluginList.insertPlugin (plugin, 0, &editViewState.selectionManager);
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
    editViewState.selectionManager.selectOnly (track.get());
    // clicking an empty part of the lane moves the playhead there
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
        const double content = tracks.size() * (trackHeight + trackGap), visible = getHeight() - rulerHeight;
        editViewState.viewY = jlimit (jmin (0.0, visible - content - 40), 0.0, editViewState.viewY.get() + dy * 240.0);
    }
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

        h->setBounds (0, y, headerWidth, trackHeight);
        t->setBounds (headerWidth, y, getWidth() - headerWidth - footerWidth, trackHeight);
        f->setBounds (getWidth() - footerWidth, y, footerWidth, trackHeight);

        y += trackHeight + trackGap;
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
        g.drawText (String (bar + 1), x + 4, 0, 40, getHeight() - 4, Justification::centredLeft);
    }
    int px = editViewState.timeToX (edit.getTransport().getPosition(), getWidth());
    Path p; p.addTriangle ((float) px - 6, 0, (float) px + 6, 0, (float) px, 9);
    g.setColour (theme::paper);
    g.fillPath (p);
}



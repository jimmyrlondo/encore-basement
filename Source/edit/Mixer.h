// Encore: Basement
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// The mixer (Window > Mixer, F3): a channel strip per track, with its effects,
// sends, pan, mute/solo/record, fader and meter, and the master strip on the
// right with the effects on the whole mix (Encore Master goes here).
//
// FX tracks: a track that plays an "aux" bus (Tracktion's aux return) instead
// of recording, so several tracks can share one reverb or delay. Each track's
// strip has a send knob per FX track; the send sits after the track's fader.
#pragma once

namespace fx
{
    /** The bus an FX track plays, or -1 for an ordinary track */
    inline int busOf (te::AudioTrack& t)
    {
        for (auto p : t.pluginList.getPlugins())
            if (auto r = dynamic_cast<te::AuxReturnPlugin*> (p)) return r->busNumber.get();
        return -1;
    }

    inline juce::Array<te::AudioTrack*> tracks (te::Edit& e)
    {
        juce::Array<te::AudioTrack*> out;
        for (auto t : te::getAudioTracks (e)) if (busOf (*t) >= 0) out.add (t);
        return out;
    }

    inline te::AuxSendPlugin* sendOn (te::AudioTrack& t, int bus)
    {
        for (auto p : t.pluginList.getPlugins())
            if (auto s = dynamic_cast<te::AuxSendPlugin*> (p); s != nullptr && s->busNumber.get() == bus) return s;
        return nullptr;
    }

    /** The send from t to bus, made (silent, after the fader) if it isn't there yet */
    inline te::AuxSendPlugin* ensureSend (te::AudioTrack& t, int bus)
    {
        if (auto s = sendOn (t, bus)) return s;
        auto p = t.edit.getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
        auto* s = dynamic_cast<te::AuxSendPlugin*> (p.get());
        if (s == nullptr) return nullptr;
        s->busNumber = bus;
        s->setGainDb (-100.0f);
        // after the volume/pan, before the meter: a post-fader send
        auto list = t.pluginList.getPlugins();
        int index = list.size();
        for (int i = 0; i < list.size(); ++i) if (dynamic_cast<te::LevelMeterPlugin*> (list[i].get()) != nullptr) { index = i; break; }
        t.pluginList.insertPlugin (p, index, nullptr);
        return s;
    }

    /** A new FX track that plays the next free bus, with an effect on it */
    inline te::AudioTrack* addTrack (te::Edit& e, const juce::String& name, const juce::String& effectType, const juce::PluginDescription& effectDesc = {})
    {
        int bus = 0;
        for (auto t : tracks (e)) bus = juce::jmax (bus, busOf (*t) + 1);
        auto all = te::getAudioTracks (e);
        auto t = e.insertNewAudioTrack (te::TrackInsertPoint (nullptr, all.isEmpty() ? nullptr : all.getLast()), nullptr);
        if (t == nullptr) return nullptr;
        t->setName (name);
        t->setColour (theme::accent2);
        t->state.setProperty ("basementInput", "none", nullptr);
        e.setAuxBusName (bus, name);
        auto ret = e.getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
        if (auto* r = dynamic_cast<te::AuxReturnPlugin*> (ret.get())) r->busNumber = bus;
        t->pluginList.insertPlugin (ret, 0, nullptr);
        if (effectType.isNotEmpty())
            if (auto effect = e.getPluginCache().createNewPlugin (effectType, effectDesc))
                t->pluginList.insertPlugin (effect, 1, nullptr);
        return t.get();
    }
}

//==============================================================================
/** A fader with a meter beside it and the dB under it */
class FaderWithMeter : public Component
{
public:
    std::function<void (float)> onDb;

    FaderWithMeter (bool stereo)
    {
        fader.setSliderStyle (Slider::LinearVertical);
        fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setTooltip ("Volume (double-click for 0 dB)");
        fader.onValueChange = [this] { if (onDb) onDb ((float) fader.getValue()); updateText(); };
        addAndMakeVisible (fader);
        addAndMakeVisible (meter);
        juce::ignoreUnused (stereo);
        db.setJustificationType (Justification::centred);
        db.setFont (FontOptions (11.0f, Font::bold));
        db.setColour (Label::textColourId, theme::text2);
        addAndMakeVisible (db);
    }

    void setDb (float v) { if (! fader.isMouseButtonDown()) { fader.setValue (v, dontSendNotification); updateText(); } }
    LevelMeter meter;

    void resized() override
    {
        auto r = getLocalBounds();
        db.setBounds (r.removeFromBottom (18));
        meter.setBounds (r.removeFromRight (12).reduced (0, 6));
        r.removeFromRight (4);
        fader.setBounds (r);
    }

private:
    Slider fader;
    Label db;
    void updateText() { auto v = fader.getValue(); db.setText (v <= -59.9 ? String ("-inf") : String (v, 1), dontSendNotification); }
};

//==============================================================================
class ChannelStrip : public Component,
                     private Timer
{
public:
    static constexpr int width = 108;

    ChannelStrip (te::AudioTrack& t, te::SelectionManager& s) : track (&t), selection (s), fader (false)
    {
        name.setFont (FontOptions (13.0f, Font::bold));
        name.setJustificationType (Justification::centred);
        name.setEditable (false, true, false);
        name.onTextChange = [this] { if (track) track->setName (name.getText()); };
        addAndMakeVisible (name);

        addFx.onClick = [this] { if (track) showEffectsMenu (track.get(), &selection, &addFx); };
        addFx.setTooltip ("Add an effect");
        addAndMakeVisible (addFx);

        pan.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setPopupDisplayEnabled (true, true, nullptr);
        pan.textFromValueFunction = [] (double v) { return std::abs (v) < 0.005 ? String ("Center") : String (roundToInt (std::abs (v) * 100)) + (v < 0 ? "% L" : "% R"); };
        pan.setTooltip ("Pan (double-click for center)");
        pan.onValueChange = [this] { if (auto vp = volume()) vp->setPan ((float) pan.getValue()); };
        addAndMakeVisible (pan);

        for (auto* b : { &mute, &solo, &arm })
        {
            b->setClickingTogglesState (false);
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        mute.setColour (TextButton::buttonOnColourId, theme::muted);
        solo.setColour (TextButton::buttonOnColourId, theme::solo);
        arm.setColour (TextButton::buttonOnColourId, theme::record);
        mute.onClick = [this] { if (track) track->setMute (! track->isMuted (false)); };
        solo.onClick = [this] { if (track) track->setSolo (! track->isSolo (false)); };
        arm.onClick = [this] { if (track) EngineHelpers::armTrack (*track, ! EngineHelpers::isTrackArmed (*track)); };

        fader.onDb = [this] (float v) { if (auto vp = volume()) vp->setVolumeDb (v); };
        addAndMakeVisible (fader);
        if (auto lm = t.getLevelMeterPlugin()) fader.meter.setSource (&lm->measurer);

        rebuild();
        startTimerHz (10);
    }

    void paint (Graphics& g) override
    {
        if (track == nullptr) return;
        const bool sel = selection.isSelected (track.get());
        g.setColour (sel ? theme::raised : theme::surface);
        g.fillRect (getLocalBounds().withTrimmedRight (2));
        g.setColour (track->getColour());
        g.fillRect (getLocalBounds().withTrimmedRight (2).removeFromTop (4));
        g.setColour (theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        auto r = getLocalBounds().withTrimmedRight (2).reduced (6, 0);
        g.drawText (isFx ? "FX TRACK" : "EFFECTS", r.withY (insertsTop - 14).withHeight (12), Justification::centredLeft);
        if (! sends.isEmpty()) g.drawText ("SENDS", r.withY (sendsTop - 14).withHeight (12), Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedRight (2).reduced (6, 0);
        r.removeFromTop (8);
        name.setBounds (r.removeFromTop (22));
        r.removeFromTop (16);
        insertsTop = r.getY();
        for (auto* b : inserts) { b->setBounds (r.removeFromTop (20)); r.removeFromTop (3); }
        addFx.setBounds (r.removeFromTop (20));
        r.removeFromTop (18);
        sendsTop = r.getY();
        for (auto* s : sends) { s->setBounds (r.removeFromTop (30)); r.removeFromTop (2); }
        r.removeFromTop (sends.isEmpty() ? 0 : 6);
        pan.setBounds (r.removeFromTop (44).withSizeKeepingCentre (44, 44));
        r.removeFromTop (6);
        auto buttons = r.removeFromTop (22);
        const int bw = (buttons.getWidth() - 8) / 3;
        mute.setBounds (buttons.removeFromLeft (bw)); buttons.removeFromLeft (4);
        solo.setBounds (buttons.removeFromLeft (bw)); buttons.removeFromLeft (4);
        arm.setBounds (buttons);
        r.removeFromTop (8);
        fader.setBounds (r.removeFromTop (juce::jmax (80, r.getHeight() - 10)));
    }

    te::AudioTrack* getTrack() const { return track.get(); }

    void mouseDown (const MouseEvent&) override { if (track) selection.selectOnly (track.get()); }

private:
    te::AudioTrack::Ptr track;
    te::SelectionManager& selection;
    Label name;
    TextButton addFx { "+ Effect" }, mute { "M" }, solo { "S" }, arm { "R" };
    Slider pan;
    FaderWithMeter fader;
    OwnedArray<TextButton> inserts;
    struct Send : public Component
    {
        Label label;
        Slider knob { Slider::LinearHorizontal, Slider::NoTextBox };
        int bus = 0;
        void resized() override { auto r = getLocalBounds(); label.setBounds (r.removeFromTop (13)); knob.setBounds (r); }
    };
    OwnedArray<Send> sends;
    int insertsTop = 0, sendsTop = 0;
    bool isFx = false;
    String signature;

    te::VolumeAndPanPlugin* volume() const { return track ? track->getVolumePlugin() : nullptr; }

    /** What the strip shows depends on the track's effects and the FX tracks: rebuilt when either changes */
    String currentSignature() const
    {
        String s;
        for (auto p : track->pluginList.getPlugins()) s << p->itemID.toString() << p->getName() << (int) p->isEnabled() << ";";
        s << "|";
        for (auto t : fx::tracks (track->edit)) s << t->itemID.toString() << t->getName() << ";";
        return s;
    }

    void rebuild()
    {
        signature = currentSignature();
        isFx = fx::busOf (*track) >= 0;
        inserts.clear();
        for (auto p : track->pluginList.getPlugins())
        {
            te::Plugin::Ptr plugin (p);
            if (dynamic_cast<te::VolumeAndPanPlugin*> (p) || dynamic_cast<te::LevelMeterPlugin*> (p)
                || dynamic_cast<te::AuxSendPlugin*> (p) || dynamic_cast<te::AuxReturnPlugin*> (p)) continue;
            auto* b = inserts.add (new TextButton (p->getName()));
            b->setColour (TextButton::buttonColourId, p->isEnabled() ? theme::raised : theme::base);
            b->setColour (TextButton::textColourOffId, p->isEnabled() ? theme::paper : theme::muted);
            b->setTooltip (p->getName() + ": click to open, right-click for more");
            b->onClick = [plugin, b]
            {
                if (ModifierKeys::currentModifiers.isPopupMenu()) return;
                plugin->showWindowExplicitly();
                juce::ignoreUnused (b);
            };
            b->addMouseListener (this, false);
            b->getProperties().set ("pluginID", p->itemID.toString());
            addAndMakeVisible (b);
        }
        sends.clear();
        if (! isFx)
            for (auto fxTrack : fx::tracks (track->edit))
            {
                auto* s = sends.add (new Send());
                s->bus = fx::busOf (*fxTrack);
                s->label.setText (fxTrack->getName(), dontSendNotification);
                s->label.setFont (FontOptions (11.0f));
                s->label.setColour (Label::textColourId, theme::text2);
                s->knob.setRange (-60.0, 6.0, 0.1);
                s->knob.setSkewFactorFromMidPoint (-12.0);
                s->knob.setDoubleClickReturnValue (true, -60.0);
                s->knob.setPopupDisplayEnabled (true, true, nullptr);
                s->knob.textFromValueFunction = [] (double v) { return v <= -59.9 ? String ("off") : String (v, 1) + " dB"; };
                s->knob.setTooltip ("How much of this track goes to " + fxTrack->getName());
                auto existing = fx::sendOn (*track, s->bus);
                s->knob.setValue (existing ? juce::jlimit (-60.0f, 6.0f, existing->getGainDb()) : -60.0, dontSendNotification);
                s->knob.onValueChange = [this, s]
                {
                    if (auto send = fx::ensureSend (*track, s->bus))
                        send->setGainDb (s->knob.getValue() <= -59.9 ? -100.0f : (float) s->knob.getValue());
                };
                s->addAndMakeVisible (s->label);
                s->addAndMakeVisible (s->knob);
                addAndMakeVisible (s);
            }
        arm.setVisible (! isFx);
        resized();
        repaint();
    }

    // right-click an effect: open, bypass, remove
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        auto* b = dynamic_cast<TextButton*> (e.eventComponent);
        if (b == nullptr || track == nullptr) return;
        te::Plugin::Ptr plugin;
        for (auto p : track->pluginList.getPlugins())
            if (p->itemID.toString() == b->getProperties()["pluginID"].toString()) plugin = p;
        if (plugin == nullptr) return;
        PopupMenu m;
        m.addItem ("Open", [plugin] { plugin->showWindowExplicitly(); });
        m.addItem (plugin->isEnabled() ? "Bypass" : "Turn on", [plugin] { plugin->setEnabled (! plugin->isEnabled()); });
        m.addSeparator();
        m.addItem ("Remove", [plugin] { plugin->deleteFromParent(); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (b));
    }

    void timerCallback() override
    {
        if (track == nullptr) return;
        if (currentSignature() != signature) rebuild();
        if (name.getText() != track->getName() && ! name.isBeingEdited()) name.setText (track->getName(), dontSendNotification);
        if (auto vp = volume())
        {
            fader.setDb (vp->getVolumeDb());
            if (! pan.isMouseButtonDown()) pan.setValue (vp->getPan(), dontSendNotification);
        }
        mute.setToggleState (track->isMuted (false), dontSendNotification);
        solo.setToggleState (track->isSolo (false), dontSendNotification);
        arm.setEnabled (EngineHelpers::trackHasInput (*track));
        arm.setToggleState (EngineHelpers::isTrackArmed (*track), dontSendNotification);
        for (auto* s : sends)
            if (! s->knob.isMouseButtonDown())
                if (auto send = fx::sendOn (*track, s->bus))
                    s->knob.setValue (juce::jlimit (-60.0f, 6.0f, send->getGainDb()), dontSendNotification);
    }
};

//==============================================================================
/** The master strip: effects on the whole mix, the master fader and meter */
class MasterStrip : public Component,
                    private Timer
{
public:
    MasterStrip (te::Edit& e) : edit (e), fader (true)
    {
        addFx.onClick = [this] { showMasterEffectsMenu(); };
        addFx.setTooltip ("Add an effect to the whole mix (Encore Master goes here)");
        addAndMakeVisible (addFx);
        fader.onDb = [this] (float v) { if (auto vp = edit.getMasterVolumePlugin()) vp->setVolumeDb (v); };
        addAndMakeVisible (fader);
        rebuild();
        startTimerHz (10);
    }

    void paint (Graphics& g) override
    {
        g.setColour (theme::raised);
        g.fillRect (getLocalBounds());
        g.setColour (theme::brand);
        g.fillRect (getLocalBounds().removeFromTop (4));
        auto r = getLocalBounds().reduced (8, 0);
        g.setColour (theme::paper);
        g.setFont (FontOptions (13.0f, Font::bold));
        g.drawText ("MASTER", r.withY (8).withHeight (22), Justification::centred);
        g.setColour (theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText ("ON THE MIX", r.withY (insertsTop - 14).withHeight (12), Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0);
        r.removeFromTop (46);
        insertsTop = r.getY();
        for (auto* b : inserts) { b->setBounds (r.removeFromTop (20)); r.removeFromTop (3); }
        addFx.setBounds (r.removeFromTop (20));
        r.removeFromTop (12);
        fader.setBounds (r.removeFromBottom (juce::jmax (120, r.getHeight() - 140)).withTrimmedBottom (10));
    }

    void setMeterSource (te::LevelMeasurer* m) { fader.meter.setSource (m); }

private:
    te::Edit& edit;
    TextButton addFx { "+ Effect" };
    FaderWithMeter fader;
    OwnedArray<TextButton> inserts;
    int insertsTop = 0;
    String signature;

    String currentSignature()
    {
        String s;
        for (auto p : edit.getMasterPluginList().getPlugins()) s << p->itemID.toString() << (int) p->isEnabled() << ";";
        return s;
    }

    void rebuild()
    {
        signature = currentSignature();
        inserts.clear();
        for (auto p : edit.getMasterPluginList().getPlugins())
        {
            te::Plugin::Ptr plugin (p);
            auto* b = inserts.add (new TextButton (p->getName()));
            b->setColour (TextButton::buttonColourId, p->isEnabled() ? theme::strong : theme::base);
            b->setTooltip (p->getName() + ": click to open, right-click to bypass or remove");
            b->onClick = [plugin]
            {
                if (ModifierKeys::currentModifiers.isPopupMenu()) return;
                plugin->showWindowExplicitly();
            };
            b->addMouseListener (this, false);
            b->getProperties().set ("pluginID", p->itemID.toString());
            addAndMakeVisible (b);
        }
        resized();
        repaint();
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        auto* b = dynamic_cast<TextButton*> (e.eventComponent);
        if (b == nullptr) return;
        te::Plugin::Ptr plugin;
        for (auto p : edit.getMasterPluginList().getPlugins())
            if (p->itemID.toString() == b->getProperties()["pluginID"].toString()) plugin = p;
        if (plugin == nullptr) return;
        PopupMenu m;
        m.addItem ("Open", [plugin] { plugin->showWindowExplicitly(); });
        m.addItem (plugin->isEnabled() ? "Bypass" : "Turn on", [plugin] { plugin->setEnabled (! plugin->isEnabled()); });
        m.addSeparator();
        m.addItem ("Remove", [plugin] { plugin->deleteFromParent(); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (b));
    }

    /** Same menu as a track's, but the effect goes on the whole mix */
    void showMasterEffectsMenu()
    {
        PopupMenu m;
        auto add = [this] (te::Plugin::Ptr p)
        {
            if (p == nullptr) return;
            edit.getMasterPluginList().insertPlugin (p, -1, nullptr);
            p->showWindowExplicitly();
        };
        for (auto& d : edit.engine.getPluginManager().knownPluginList.getTypes())
            if (d.pluginFormatName == "Encore")
                m.addItem (d.name, [this, add, d] { add (edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, d)); });
        m.addSeparator();
        std::map<String, PopupMenu> byMaker;
        for (auto& d : edit.engine.getPluginManager().knownPluginList.getTypes())
            if (d.pluginFormatName != "Encore" && ! d.isInstrument && ! d.manufacturerName.containsIgnoreCase ("Encore") && ! d.manufacturerName.containsIgnoreCase ("Sick & Sad"))
                byMaker[d.manufacturerName.isEmpty() ? String ("Other") : d.manufacturerName].addItem (d.name, [this, add, d] { add (edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, d)); });
        for (auto& [maker, menu] : byMaker) m.addSubMenu (maker, menu);
        m.addSeparator();
        m.addItem ("EQ (4-band)", [this, add] { add (edit.getPluginCache().createNewPlugin (te::EqualiserPlugin::xmlTypeName, {})); });
        m.addItem ("Compressor / Limiter", [this, add] { add (edit.getPluginCache().createNewPlugin (te::CompressorPlugin::xmlTypeName, {})); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&addFx));
    }

    void timerCallback() override
    {
        if (currentSignature() != signature) rebuild();
        if (auto vp = edit.getMasterVolumePlugin()) fader.setDb (vp->getVolumeDb());
        if (auto* ctx = edit.getTransport().getCurrentPlaybackContext()) setMeterSource (&ctx->masterLevels);
    }
};

//==============================================================================
class MixerComponent : public Component,
                       private Timer
{
public:
    std::function<void()> onAddFxTrack;

    MixerComponent (te::Edit& e, te::SelectionManager& s) : edit (e), selection (s), master (e)
    {
        viewport.setViewedComponent (&strips, false);
        viewport.setScrollBarsShown (false, true);
        addAndMakeVisible (viewport);
        addAndMakeVisible (master);
        addFxTrack.onClick = [this] { if (onAddFxTrack) onAddFxTrack(); };
        addFxTrack.setTooltip ("A track the others can send to: share one reverb or delay between them");
        strips.addAndMakeVisible (addFxTrack);
        rebuild();
        startTimerHz (4);
    }

    void paint (Graphics& g) override { g.fillAll (theme::base); }

    void resized() override
    {
        auto r = getLocalBounds();
        master.setBounds (r.removeFromRight (130));
        viewport.setBounds (r);
        layoutStrips();
    }

private:
    te::Edit& edit;
    te::SelectionManager& selection;
    Viewport viewport;
    Component strips;
    OwnedArray<ChannelStrip> channelStrips;
    MasterStrip master;
    TextButton addFxTrack { "+ FX track" };
    String signature;

    String currentSignature() const
    {
        String s;
        for (auto t : te::getAudioTracks (edit)) s << t->itemID.toString() << ";";
        return s;
    }

    void rebuild()
    {
        signature = currentSignature();
        channelStrips.clear();
        for (auto t : te::getAudioTracks (edit))
            strips.addAndMakeVisible (channelStrips.add (new ChannelStrip (*t, selection)));
        layoutStrips();
    }

    void layoutStrips()
    {
        const int h = juce::jmax (300, viewport.getHeight() - viewport.getScrollBarThickness());
        int x = 0;
        for (auto* s : channelStrips) { s->setBounds (x, 0, ChannelStrip::width, h); x += ChannelStrip::width; }
        addFxTrack.setBounds (x + 12, 12, 96, 28);
        strips.setSize (x + 120, h);
    }

    void timerCallback() override
    {
        if (currentSignature() != signature) rebuild();
    }
};

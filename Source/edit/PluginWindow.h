#pragma once

inline bool isDPIAware (te::Plugin&)
{
	// You should keep a DB of if plugins are DPI aware or not and recall that value
	// here. You should let the user toggle the value if the plugin appears tiny
	return true;
}

//==============================================================================
struct AudioProcessorEditorContentComp : public te::Plugin::EditorComponent
{
    AudioProcessorEditorContentComp (te::ExternalPlugin& plug) : plugin (plug)
    {
        JUCE_AUTORELEASEPOOL
        {
            if (auto pi = plugin.getAudioPluginInstance())
            {
                editor.reset (pi->createEditorAndMakeActive());

                if (editor == nullptr)
                    editor = std::make_unique<GenericAudioProcessorEditor> (*pi);

                addAndMakeVisible (*editor);
            }
        }

        resizeToFitEditor (true);
    }

    bool allowWindowResizing() override { return false; }

    ComponentBoundsConstrainer* getBoundsConstrainer() override
    {
        if (editor == nullptr || allowWindowResizing())
            return {};

        return editor->getConstrainer();
    }

    void resized() override
    {
        if (editor != nullptr)
            editor->setBounds (getLocalBounds());
    }

    void childBoundsChanged (Component* c) override
    {
        if (c == editor.get())
        {
            plugin.edit.pluginChanged (plugin);
            resizeToFitEditor();
        }
    }

    void resizeToFitEditor (bool force = false)
    {
        if (force || ! allowWindowResizing())
            setSize (jmax (8, editor != nullptr ? editor->getWidth() : 0), jmax (8, editor != nullptr ? editor->getHeight() : 0));
    }

    te::ExternalPlugin& plugin;
    std::unique_ptr<AudioProcessorEditor> editor;

    AudioProcessorEditorContentComp() = delete;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioProcessorEditorContentComp)
};

//==============================================================================
/** Controls for plugins without their own window (Studio's built-in effects):
    a knob per parameter, an On switch, and a reset. Knobs follow automation. */
struct GenericPluginEditor : public te::Plugin::EditorComponent,
                             private Timer
{
    GenericPluginEditor (te::Plugin& p) : plugin (p)
    {
        onButton.setClickingTogglesState (true);
        onButton.setToggleState (plugin.isEnabled(), dontSendNotification);
        onButton.setColour (TextButton::buttonOnColourId, theme::accent);
        onButton.onClick = [this] { plugin.setEnabled (onButton.getToggleState()); };
        resetButton.onClick = [this]
        {
            for (auto* k : knobs) k->param->setParameter (k->param->getDefaultValue().value_or (k->param->valueRange.start), sendNotification);
        };
        addAndMakeVisible (onButton);
        addAndMakeVisible (resetButton);

        for (auto param : plugin.getAutomatableParameters())
        {
            auto* k = knobs.add (new Knob (*param));
            area.addAndMakeVisible (k);
        }
        // many parameters (the built-in synth has dozens): six columns, and scroll past five rows
        const int cols = jlimit (1, 6, knobs.size()), rows = jmax (1, (knobs.size() + cols - 1) / cols);
        area.setSize (cols * knobW, rows * knobH);
        viewport.setViewedComponent (&area, false);
        viewport.setScrollBarsShown (rows > 5, false);
        addAndMakeVisible (viewport);
        setSize (jmax (300, cols * knobW + 32 + (rows > 5 ? 12 : 0)), 64 + jmin (rows, 5) * knobH + 16);
        startTimerHz (15);
    }

    bool allowWindowResizing() override { return false; }
    ComponentBoundsConstrainer* getBoundsConstrainer() override { return nullptr; }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::surface);
        g.setColour (theme::paper);
        g.setFont (FontOptions (16.0f, Font::bold));
        g.drawText (plugin.getName(), getLocalBounds().reduced (16, 0).removeFromTop (52), Justification::centredLeft);
        if (knobs.isEmpty())
        {
            g.setColour (theme::muted);
            g.setFont (FontOptions (13.0f));
            g.drawText ("Nothing to adjust on this one.", getLocalBounds().withTrimmedTop (52), Justification::centred);
        }
    }

    void resized() override
    {
        auto top = getLocalBounds().reduced (16, 12).removeFromTop (28);
        onButton.setBounds (top.removeFromRight (56));
        top.removeFromRight (8);
        resetButton.setBounds (top.removeFromRight (70));
        const int cols = jlimit (1, 6, knobs.size());
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds ((i % cols) * knobW, (i / cols) * knobH, knobW, knobH);
        viewport.setBounds (getLocalBounds().withTrimmedTop (64).withTrimmedLeft (16).withTrimmedBottom (8));
    }

private:
    static constexpr int knobW = 104, knobH = 118;

    struct Knob : public Component
    {
        Knob (te::AutomatableParameter& p) : param (&p)
        {
            slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            const auto& r = p.valueRange;
            slider.setNormalisableRange ({ (double) r.start, (double) r.end, (double) r.interval, (double) r.skew });
            slider.setValue (p.getCurrentValue(), dontSendNotification);
            slider.setColour (Slider::rotarySliderFillColourId, theme::accent);
            slider.onDragStart = [this] { param->parameterChangeGestureBegin(); };
            slider.onDragEnd = [this] { param->parameterChangeGestureEnd(); };
            slider.onValueChange = [this] { param->setParameter ((float) slider.getValue(), sendNotification); value.setText (param->getCurrentValueAsStringWithLabel(), dontSendNotification); };
            if (auto d = p.getDefaultValue()) slider.setDoubleClickReturnValue (true, *d);
            name.setText (p.getParameterName(), dontSendNotification);
            name.setJustificationType (Justification::centred);
            name.setFont (FontOptions (12.0f, Font::bold));
            value.setJustificationType (Justification::centred);
            value.setFont (FontOptions (11.0f));
            value.setColour (Label::textColourId, theme::text2);
            value.setText (p.getCurrentValueAsStringWithLabel(), dontSendNotification);
            for (auto* c : std::initializer_list<Component*> { &slider, &name, &value }) addAndMakeVisible (c);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 0);
            name.setBounds (r.removeFromTop (18));
            value.setBounds (r.removeFromBottom (18));
            slider.setBounds (r.reduced (6));
        }

        void refresh()
        {
            if (slider.isMouseButtonDown()) return;
            if (std::abs (slider.getValue() - param->getCurrentValue()) > 1.0e-6)
            {
                slider.setValue (param->getCurrentValue(), dontSendNotification);
                value.setText (param->getCurrentValueAsStringWithLabel(), dontSendNotification);
            }
        }

        te::AutomatableParameter::Ptr param;
        Slider slider;
        Label name, value;
    };

    te::Plugin& plugin;
    Component area;           // before the viewport, so the viewport (which shows it) goes first
    Viewport viewport;
    OwnedArray<Knob> knobs;
    TextButton onButton { "On" }, resetButton { "Reset" };

    void timerCallback() override
    {
        for (auto* k : knobs) k->refresh();
        onButton.setToggleState (plugin.isEnabled(), dontSendNotification);
    }
};

//=============================================================================
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (te::Plugin&);
    ~PluginWindow() override;

    static std::unique_ptr<Component> create (te::Plugin&);

    void show();

    void setEditor (std::unique_ptr<te::Plugin::EditorComponent>);
    te::Plugin::EditorComponent* getEditor() const         { return editor.get(); }

    void recreateEditor();
    void recreateEditorAsync();

private:
    void moved() override;
    void userTriedToCloseWindow() override          { plugin.windowState->closeWindowExplicitly(); }
    bool keyPressed (const KeyPress& k) override    { return StudioHooks::forwardKey != nullptr && StudioHooks::forwardKey (k); }
    void closeButtonPressed() override              { userTriedToCloseWindow(); }
    float getDesktopScaleFactor() const override    { return 1.0f; }

    std::unique_ptr<te::Plugin::EditorComponent> editor;

    te::Plugin& plugin;
    te::PluginWindowState& windowState;
    bool updateStoredBounds = false;
};

//==============================================================================
#if JUCE_LINUX
 constexpr bool shouldAddPluginWindowToDesktop = false;
#else
 constexpr bool shouldAddPluginWindowToDesktop = true;
#endif

PluginWindow::PluginWindow (te::Plugin& plug)
    : DocumentWindow (plug.getName() + (plug.getOwnerTrack() != nullptr ? " - " + plug.getOwnerTrack()->getName() : String()),
                      theme::surface, DocumentWindow::closeButton, shouldAddPluginWindowToDesktop),
      plugin (plug), windowState (*plug.windowState)
{
    setUsingNativeTitleBar (true);
    setIcon (encoreIconImage (64));
    getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
    setResizeLimits (100, 50, 4000, 4000);

    recreateEditor();

    setBoundsConstrained (getLocalBounds() + plugin.windowState->choosePositionForPluginWindow());

    #if JUCE_LINUX
     setAlwaysOnTop (true);
     addToDesktop();
    #endif

     updateStoredBounds = true;
}

PluginWindow::~PluginWindow()
{
    updateStoredBounds = false;
    plugin.edit.flushPluginStateIfNeeded (plugin);
    setEditor (nullptr);
}

void PluginWindow::show()
{
    setVisible (true);
    toFront (false);
    setBoundsConstrained (getBounds());
}

void PluginWindow::setEditor (std::unique_ptr<te::Plugin::EditorComponent> newEditor)
{
    JUCE_AUTORELEASEPOOL
    {
        setConstrainer (nullptr);
        editor.reset();

        if (newEditor != nullptr)
        {
            editor = std::move (newEditor);
            setContentNonOwned (editor.get(), true);
        }

        setResizable (editor == nullptr || editor->allowWindowResizing(), false);

        if (editor != nullptr && editor->allowWindowResizing())
            setConstrainer (editor->getBoundsConstrainer());
    }
}

std::unique_ptr<Component> PluginWindow::create (te::Plugin& plugin)
{
    if (auto externalPlugin = dynamic_cast<te::ExternalPlugin*> (&plugin))
        if (externalPlugin->getAudioPluginInstance() == nullptr)
            return nullptr;

    std::unique_ptr<PluginWindow> w;

    {
        struct Blocker : public Component { void inputAttemptWhenModal() override {} };

        Blocker blocker;
        blocker.enterModalState (false);

       #if JUCE_WINDOWS && JUCE_WIN_PER_MONITOR_DPI_AWARE
        if (! isDPIAware (plugin))
        {
            ScopedDPIAwarenessDisabler disableDPIAwareness;
            w = std::make_unique<PluginWindow> (plugin);
        }
        else
       #endif
        {
            w = std::make_unique<PluginWindow> (plugin);
        }
    }

    if (w == nullptr || w->getEditor() == nullptr)
        return {};

    w->show();

    return w;
}

void PluginWindow::recreateEditor()
{
    setEditor (nullptr);
    auto ed = plugin.createEditor();
    if (ed == nullptr && dynamic_cast<te::ExternalPlugin*> (&plugin) == nullptr)
        ed = std::make_unique<GenericPluginEditor> (plugin); // built-in effects have no window of their own
    setEditor (std::move (ed));
}

void PluginWindow::recreateEditorAsync()
{
    setEditor (nullptr);

    Timer::callAfterDelay (50, [this, sp = SafePointer<Component> (this)]
                               {
                                   if (sp != nullptr)
                                       recreateEditor();
                               });
}

void PluginWindow::moved()
{
    if (updateStoredBounds)
    {
        plugin.windowState->lastWindowBounds = getBounds();
        plugin.edit.pluginChanged (plugin);
    }
}

//==============================================================================
class ExtendedUIBehaviour : public te::UIBehaviour
{
public:
    ExtendedUIBehaviour() = default;

    std::unique_ptr<Component> createPluginWindow (te::PluginWindowState& pws) override
    {
        if (auto ws = dynamic_cast<te::Plugin::WindowState*> (&pws))
            return PluginWindow::create (ws->plugin);

        return {};
    }

    void recreatePluginWindowContentAsync (te::Plugin& p) override
    {
        if (auto* w = dynamic_cast<PluginWindow*> (p.windowState->pluginWindow.get()))
            return w->recreateEditorAsync();

        UIBehaviour::recreatePluginWindowContentAsync (p);
    }
};

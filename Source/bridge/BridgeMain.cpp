// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// "Encore Studio Bridge32.exe": a small 32-bit program that runs one 32-bit
// VST or VST3 plugin for Studio (which is 64-bit and can't load them itself).
//
//   --scan <plugin> <out.xml>        what's in a plugin file (written as XML), then quit
//   --host <pipe> <shared-name>      load and run a plugin for Studio until told to quit
//
// A plugin that crashes takes only this program down, never Studio.

#include <JuceHeader.h>
#include <string>
#include "Shm.h"
#include "Protocol.h"

namespace
{
    /** %TEMP%/Encore Studio Bridge32.log: what the bridge did, for problem reports (kept short) */
    void log (const juce::String& line)
    {
        static juce::CriticalSection lock;
        const juce::ScopedLock sl (lock);
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Encore Studio Bridge32.log");
        if (f.getSize() > 200000) f.deleteFile();
        f.appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  [" + juce::String ((int) bridgeos::currentProcessId()) + "] " + line + juce::newLine);
    }

    juce::AudioPluginFormatManager& formats()
    {
        static juce::AudioPluginFormatManager f;
        if (f.getNumFormats() == 0)
        {
            f.addFormat (std::make_unique<juce::VST3PluginFormat>());
            f.addFormat (std::make_unique<juce::VSTPluginFormat>());
        }
        return f;
    }

    /** "--scan": every plugin in the file, as Studio's format will list them */
    int scan (const juce::String& plugin, const juce::File& out)
    {
        juce::XmlElement list ("PLUGINS");
        list.setAttribute ("pid", (int) bridgeos::currentProcessId());
        out.replaceWithText (list.toString());   // the pid first, so Studio can tell if a prompt window comes up
        for (auto* f : formats().getFormats())
        {
            if (! f->fileMightContainThisPluginType (plugin)) continue;
            juce::OwnedArray<juce::PluginDescription> found;
            f->findAllTypesForFile (found, plugin);
            for (auto* d : found) list.addChildElement (d->createXml().release());
            if (! found.isEmpty()) break;
        }
        list.setAttribute ("done", 1);
        return out.replaceWithText (list.toString()) ? 0 : 1;
    }

    //==============================================================================
    /** The plugin's own window, in front of Studio's (closing it just hides it) */
    class EditorWindow : public juce::DocumentWindow
    {
    public:
        std::function<void()> onClose;
        EditorWindow (const juce::String& name, juce::AudioProcessorEditor* editor)
            : DocumentWindow (name + "  (32-bit)", juce::Colour (0xff171412), DocumentWindow::closeButton | DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
            setAlwaysOnTop (true);
            centreWithSize (getWidth(), getHeight());
        }
        void closeButtonPressed() override { setVisible (false); if (onClose) onClose(); }
    };

    //==============================================================================
    /** "--host": one plugin, run for Studio */
    class Host : public juce::InterprocessConnection,
                 private juce::AudioProcessorListener,
                 private juce::AudioPlayHead,
                 private juce::Thread
    {
    public:
        Host (const juce::String& sharedName) : InterprocessConnection (true, 0x45534231), Thread ("bridge audio"), base (sharedName) {}

        ~Host() override
        {
            stopThread (2000);
            editor = nullptr;
            if (plugin) plugin->removeListener (this);
            plugin = nullptr;
            disconnect();
            bridgeos::close (go); bridgeos::close (done); bridgeos::close (memory);
        }

        bool start (const juce::String& pipe)
        {
            memory = bridgeos::openMemory (bridge::objectName (base, "mem").c_str(), sizeof (bridge::Shared));
            go = bridgeos::openEvent (bridge::objectName (base, "go").c_str());
            done = bridgeos::openEvent (bridge::objectName (base, "done").c_str());
            shared = static_cast<bridge::Shared*> (bridgeos::address (memory));
            if (shared == nullptr || go == nullptr || done == nullptr) return false;
            if (! connectToPipe (pipe, -1)) { log ("couldn't connect to " + pipe); return false; }   // no timeout: a quiet pipe isn't a lost one
            log ("connected to Studio");
            send (juce::ValueTree ("HELLO"));
            startThread (juce::Thread::Priority::highest);
            return true;
        }

    private:
        juce::String base;
        bridgeos::Memory* memory = nullptr;
        bridgeos::Event* go = nullptr;
        bridgeos::Event* done = nullptr;
        bridge::Shared* shared = nullptr;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::unique_ptr<EditorWindow> editor;
        juce::CriticalSection pluginLock;   // held while processing and while swapping or preparing the plugin
        std::atomic<bool> ready { false };
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;
        // the plugin's own parameter moves (its window), on their way to Studio
        juce::AbstractFifo moves { 1024 };
        std::array<bridge::ParamChange, 1024> moveData;
        static inline thread_local bool applyingStudioMoves = false;
        int blocks = 0, movesLogged = 0;

        void connectionMade() override {}
        void connectionLost() override { log ("Studio closed the connection"); juce::JUCEApplicationBase::quit(); }

        void send (juce::ValueTree v) { sendMessage (bridge::pack (v)); }

        void reply (const juce::ValueTree& request, juce::ValueTree r)
        {
            r.setProperty ("id", request["id"], nullptr);
            send (r);
        }

        void messageReceived (const juce::MemoryBlock& m) override
        {
            auto v = bridge::unpack (m);
            const auto type = v.getType().toString();
            log ("message " + type);
            juce::ValueTree r ("REPLY");
            if (type == "LOAD")           load (v, r);
            else if (type == "PREPARE")   prepare (v["sampleRate"], v["blockSize"]);
            else if (type == "GETSTATE")
            {
                juce::MemoryBlock state;
                if (plugin) plugin->getStateInformation (state);
                r.setProperty ("state", state.toBase64Encoding(), nullptr);
            }
            else if (type == "SETSTATE")
            {
                juce::MemoryBlock state;
                state.fromBase64Encoding (v["state"].toString());
                const juce::ScopedLock sl (pluginLock);
                if (plugin && state.getSize() > 0) plugin->setStateInformation (state.getData(), (int) state.getSize());
                // every value now, since not every plugin says which ones a restore changed
                if (plugin) for (auto* param : plugin->getParameters()) { juce::ValueTree x ("PARAM"); x.setProperty ("value", param->getValue(), nullptr); r.appendChild (x, nullptr); }
            }
            else if (type == "SHOWEDITOR") showEditor (true);
            else if (type == "HIDEEDITOR") showEditor (false);
            else if (type == "QUIT")       { juce::JUCEApplicationBase::quit(); return; }
            reply (v, r);
        }

        void load (const juce::ValueTree& v, juce::ValueTree& r)
        {
            juce::PluginDescription d;
            if (auto xml = juce::parseXML (v["description"].toString())) d.loadFromXml (*xml);
            juce::String error;
            auto p = formats().createPluginInstance (d, v["sampleRate"], v["blockSize"], error);
            log ("load " + d.name + ": " + (p != nullptr ? juce::String ("ok") : error));
            if (p == nullptr) { r.setProperty ("error", error.isNotEmpty() ? error : juce::String ("couldn't open it"), nullptr); return; }
            p->enableAllBuses();
            p->addListener (this);
            r.setProperty ("ok", true, nullptr);
            r.setProperty ("name", p->getName(), nullptr);
            r.setProperty ("ins", p->getTotalNumInputChannels(), nullptr);
            r.setProperty ("outs", p->getTotalNumOutputChannels(), nullptr);
            r.setProperty ("latency", p->getLatencySamples(), nullptr);
            r.setProperty ("hasEditor", p->hasEditor(), nullptr);
            r.setProperty ("midiIn", p->acceptsMidi(), nullptr);
            r.setProperty ("midiOut", p->producesMidi(), nullptr);
            r.setProperty ("tail", p->getTailLengthSeconds(), nullptr);
            for (auto* param : p->getParameters())
            {
                juce::ValueTree x ("PARAM");
                x.setProperty ("name", param->getName (64), nullptr);
                x.setProperty ("label", param->getLabel(), nullptr);
                x.setProperty ("value", param->getValue(), nullptr);
                x.setProperty ("default", param->getDefaultValue(), nullptr);
                x.setProperty ("steps", param->getNumSteps(), nullptr);
                x.setProperty ("discrete", param->isDiscrete(), nullptr);
                x.setProperty ("boolean", param->isBoolean(), nullptr);
                x.setProperty ("automatable", param->isAutomatable(), nullptr);
                r.appendChild (x, nullptr);
            }
            const juce::ScopedLock sl (pluginLock);
            plugin = std::move (p);
            plugin->setPlayHead (this);
            prepareLocked (v["sampleRate"], v["blockSize"]);
        }

        void prepare (double rate, int block) { const juce::ScopedLock sl (pluginLock); prepareLocked (rate, block); }

        void prepareLocked (double rate, int block)
        {
            if (! plugin || rate <= 0) return;
            ready = false;
            block = juce::jlimit (16, bridge::maxBlock, block);
            plugin->setRateAndBufferSizeDetails (rate, block);
            plugin->prepareToPlay (rate, block);
            buffer.setSize (juce::jmax (1, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), bridge::maxBlock);
            ready = true;
        }

        void showEditor (bool show)
        {
            if (! show) { if (editor) editor->setVisible (false); return; }
            if (! plugin || ! plugin->hasEditor()) return;
            if (editor == nullptr)
                if (auto* e = plugin->createEditorIfNeeded())
                {
                    editor = std::make_unique<EditorWindow> (plugin->getName(), e);
                    editor->onClose = [this] { send (juce::ValueTree ("EDITORCLOSED")); };
                }
            if (editor) { editor->setVisible (true); editor->toFront (true); }
        }

        // the plugin's window moved a parameter: tell Studio (so it can record the move as automation)
        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float value) override
        {
            if (applyingStudioMoves) return;   // Studio's own moves, coming back: not news to Studio
            const auto scope = moves.write (1);
            if (scope.blockSize1 > 0) moveData[(size_t) scope.startIndex1] = { index, value };
        }

        void audioProcessorChanged (juce::AudioProcessor* p, const ChangeDetails& d) override
        {
            if (! d.latencyChanged) return;
            juce::ValueTree v ("LATENCY");
            v.setProperty ("samples", p->getLatencySamples(), nullptr);
            juce::MessageManager::callAsync ([this, v] { send (v); });
        }

        // Studio's transport, as the plugin sees it
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo i;
            i.setBpm (shared->bpm);
            i.setTimeInSeconds (shared->timeSeconds);
            i.setTimeInSamples ((juce::int64) (shared->timeSeconds * shared->sampleRate));
            i.setPpqPosition (shared->ppq);
            i.setPpqPositionOfLastBarStart (shared->ppqBarStart);
            i.setTimeSignature (juce::AudioPlayHead::TimeSignature { shared->sigNum, shared->sigDen });
            i.setIsPlaying (shared->playing != 0);
            i.setIsRecording (shared->recording != 0);
            i.setIsLooping (shared->looping != 0);
            i.setLoopPoints (juce::AudioPlayHead::LoopPoints { shared->loopStartPpq, shared->loopEndPpq });
            return i;
        }

        /** The audio thread: one block each time Studio says go */
        void run() override
        {
            while (! threadShouldExit())
            {
                if (! bridgeos::wait (go, 200)) continue;
                processOneBlock();
                bridgeos::signal (done);
            }
        }

        void processOneBlock()
        {
            auto& s = *shared;
            if (blocks++ < 3) log ("block " + juce::String (blocks) + ": " + juce::String (s.numSamples) + " samples, ready " + juce::String ((int) ready.load()));
            const int n = juce::jlimit (0, bridge::maxBlock, (int) s.numSamples);
            const juce::ScopedTryLock sl (pluginLock);
            if (! sl.isLocked() || ! ready || ! plugin || n == 0)
            {
                for (int c = 0; c < juce::jmin ((int) s.numOut, bridge::maxChannels); ++c) juce::FloatVectorOperations::clear (s.audio[c], n);
                s.numMidiOut = s.numParamsOut = 0;
                return;
            }
            auto& params = plugin->getParameters();
            if (s.numParamsIn > 0 && movesLogged++ < 5) log (juce::String (s.numParamsIn) + " parameter moves from Studio, first " + juce::String (s.paramsIn[0].index) + " = " + juce::String (s.paramsIn[0].value, 3) + " (was " + juce::String (juce::isPositiveAndBelow (s.paramsIn[0].index, params.size()) ? params[s.paramsIn[0].index]->getValue() : -1.0f, 3) + ")");
            applyingStudioMoves = true;
            for (int i = 0; i < juce::jmin ((int) s.numParamsIn, bridge::maxParamChanges); ++i)
                if (juce::isPositiveAndBelow (s.paramsIn[i].index, params.size()))
                {
                    params[s.paramsIn[i].index]->setValue (s.paramsIn[i].value);
                    params[s.paramsIn[i].index]->sendValueChangedMessageToListeners (s.paramsIn[i].value);   // so the plugin's window follows
                }
            applyingStudioMoves = false;

            const int ins = plugin->getTotalNumInputChannels(), outs = plugin->getTotalNumOutputChannels();
            const int chans = juce::jmin (bridge::maxChannels, juce::jmax (ins, outs));
            buffer.setSize (juce::jmax (1, chans), n, false, false, true);
            for (int c = 0; c < chans; ++c)
                if (c < ins && c < s.numIn) buffer.copyFrom (c, 0, s.audio[c], n);
                else buffer.clear (c, 0, n);
            midi.clear();
            for (int i = 0; i < juce::jmin ((int) s.numMidiIn, bridge::maxMidi); ++i)
                midi.addEvent (s.midiIn[i].data, juce::jlimit (1, 4, (int) s.midiIn[i].size), juce::jlimit (0, n - 1, (int) s.midiIn[i].offset));

            plugin->processBlock (buffer, midi);

            for (int c = 0; c < juce::jmin ((int) s.numOut, bridge::maxChannels); ++c)
                if (c < outs) juce::FloatVectorOperations::copy (s.audio[c], buffer.getReadPointer (c), n);
                else juce::FloatVectorOperations::clear (s.audio[c], n);
            int m = 0;
            for (const auto meta : midi)
            {
                if (m >= bridge::maxMidi || meta.numBytes > 4) continue;
                auto& e = s.midiOut[m++];
                e.offset = meta.samplePosition;
                e.size = meta.numBytes;
                std::memcpy (e.data, meta.data, (size_t) meta.numBytes);
            }
            s.numMidiOut = m;
            int p = 0;
            const auto scope = moves.read (moves.getNumReady());
            for (int i = 0; i < scope.blockSize1 && p < bridge::maxParamChanges; ++i) s.paramsOut[p++] = moveData[(size_t) (scope.startIndex1 + i)];
            for (int i = 0; i < scope.blockSize2 && p < bridge::maxParamChanges; ++i) s.paramsOut[p++] = moveData[(size_t) (scope.startIndex2 + i)];
            s.numParamsOut = p;
            s.latencySamples = plugin->getLatencySamples();
        }
    };
}

//==============================================================================
class BridgeApplication : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override    { return "Encore Studio Bridge32"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}

    void initialise (const juce::String& commandLine) override
    {
        auto args = juce::StringArray::fromTokens (commandLine, true);
        for (auto& a : args) a = a.unquoted();
        args.removeEmptyStrings();
        if (args.size() >= 3 && args[0] == "--scan")
        {
            bridgeos::hideWindowsWhileScanning (4000, nullptr);
            setApplicationReturnValue (scan (args[1], juce::File (args[2])));
            quit();
            return;
        }
        if (args.size() >= 3 && args[0] == "--statecheck")   // in-process: does the plugin's saved state follow a parameter?
        {
            juce::OwnedArray<juce::PluginDescription> found;
            for (auto* f : formats().getFormats()) if (found.isEmpty() && f->fileMightContainThisPluginType (args[1])) f->findAllTypesForFile (found, args[1]);
            juce::String error, out;
            auto p = found.isEmpty() ? nullptr : formats().createPluginInstance (*found[0], 48000, 512, error);
            if (p != nullptr && ! p->getParameters().isEmpty())
            {
                p->enableAllBuses();
                p->prepareToPlay (48000, 512);
                juce::AudioBuffer<float> b (juce::jmax (2, p->getTotalNumOutputChannels()), 512);
                juce::MidiBuffer m;
                auto* param = p->getParameters()[0];
                juce::MemoryBlock s1, s2;
                param->setValueNotifyingHost (0.2f); for (int i = 0; i < 3; ++i) p->processBlock (b, m); p->getStateInformation (s1);
                param->setValueNotifyingHost (0.8f); for (int i = 0; i < 3; ++i) p->processBlock (b, m); p->getStateInformation (s2);
                p->setStateInformation (s1.getData(), (int) s1.getSize());
                out << "state differs: " << (int) (s1 != s2) << ", after restore " << param->getValue() << ", sizes " << (int) s1.getSize() << "/" << (int) s2.getSize();
            }
            else out << "couldn't load: " << error;
            juce::File (args[2]).replaceWithText (out);
            quit();
            return;
        }
        if (args.size() >= 3 && args[0] == "--host")
        {
            host = std::make_unique<Host> (args[2]);
            if (host->start (args[1])) return;
        }
        setApplicationReturnValue (2);
        quit();
    }

    void shutdown() override { host = nullptr; }

private:
    std::unique_ptr<Host> host;
};

START_JUCE_APPLICATION (BridgeApplication)

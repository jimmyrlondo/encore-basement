// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// 32-bit plugins (Windows): Studio is 64-bit and can't load a 32-bit .dll or
// .vst3 itself, so each one runs in "Encore Studio Bridge32.exe" beside it
// (see bridge/BridgeMain.cpp). To the engine it's a normal plugin of the
// formats "32-bit" (VST2) and "32-bit v3" (VST3): parameters, automation, saved
// state and MIDI all work; its window opens on its own, in front of Studio.
// A 32-bit plugin that crashes only takes its bridge down: Studio carries on
// (silent where the plugin was) and says so.
#pragma once

#if JUCE_WINDOWS
#include "bridge/Shm.h"
#include "bridge/Protocol.h"

namespace bridged
{
    inline juce::File helperExe()
    {
        return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("Encore Studio Bridge32.exe");
    }

    inline bool available() { return helperExe().existsAsFile(); }

    /** The 32-bit binary inside a plugin file (a .dll, or a .vst3 file or bundle), if it is one */
    inline bool is32Bit (const juce::File& f)
    {
        if (f.isDirectory())   // a .vst3 bundle: 32-bit only when it has no 64-bit binary (64-bit Studio loads those itself)
        {
            auto contents = f.getChildFile ("Contents");
            if (contents.getChildFile ("x86_64-win").isDirectory()) return false;
            auto bin = contents.getChildFile ("x86-win").getChildFile (f.getFileName());
            return bin.existsAsFile() && bridgeos::binaryBits (bin.getFullPathName().toWideCharPointer()) == 32;
        }
        return f.existsAsFile() && bridgeos::binaryBits (f.getFullPathName().toWideCharPointer()) == 32;
    }

    //==============================================================================
    /** One running bridge: the helper process, its pipe, and the shared block */
    class Connection : public juce::InterprocessConnection
    {
    public:
        std::function<void (const juce::ValueTree&)> onNotice;   // LATENCY, EDITORCLOSED (on the pipe's thread)
        std::function<void()> onLost;

        Connection() : InterprocessConnection (false, 0x45534231)
        {
            static std::atomic<int> counter { 0 };
            base = "EncoreBridge-" + juce::String ((int) bridgeos::currentProcessId()) + "-" + juce::String (++counter);
        }

        ~Connection() override
        {
            if (isConnected() && ! dead) send (juce::ValueTree ("QUIT"));
            disconnect (500);
            process.waitForProcessToFinish (1500);
            process.kill();
            bridgeos::close (go); bridgeos::close (done); bridgeos::close (memory);
        }

        bool start (juce::String& error)
        {
            memory = bridgeos::createMemory (bridge::objectName (base, "mem").c_str(), sizeof (bridge::Shared));
            go = bridgeos::createEvent (bridge::objectName (base, "go").c_str());
            done = bridgeos::createEvent (bridge::objectName (base, "done").c_str());
            shared = static_cast<bridge::Shared*> (bridgeos::address (memory));
            if (shared == nullptr || go == nullptr || done == nullptr) { error = "couldn't set up the 32-bit bridge"; return false; }
            if (! createPipe (base, -1, true)) { error = "couldn't open the 32-bit bridge's pipe"; return false; }   // no timeout: a quiet pipe isn't a lost one
            if (! process.start (juce::StringArray { helperExe().getFullPathName(), "--host", base, base }, 0)) { error = "couldn't start the 32-bit bridge"; return false; }
            if (! hello.wait (15000)) { error = "the 32-bit bridge didn't answer"; process.kill(); return false; }
            return true;
        }

        /** Sends a request and waits for its REPLY (on the pipe's own thread, so the message thread can wait) */
        juce::ValueTree request (juce::ValueTree v, int timeoutMs)
        {
            auto waiter = std::make_shared<Pending>();
            const int id = ++nextId;
            { const juce::ScopedLock sl (lock); pending[id] = waiter; }
            v.setProperty ("id", id, nullptr);
            if (! send (v)) { const juce::ScopedLock sl (lock); pending.erase (id); return {}; }
            const bool answered = waiter->event.wait (timeoutMs);
            const juce::ScopedLock sl (lock);
            pending.erase (id);
            return answered ? waiter->reply : juce::ValueTree();
        }

        bool send (const juce::ValueTree& v) { return isConnected() && sendMessage (bridge::pack (v)); }

        bridge::Shared* shared = nullptr;

        /** Runs one block in the bridge: true if it answered in time */
        bool runBlock (int timeoutMs)
        {
            if (dead || ! isConnected()) return false;
            bridgeos::signal (go);
            if (bridgeos::wait (done, timeoutMs)) { missed = 0; return true; }
            if (++missed > 50) dead = true;   // stopped answering for good (hung or crashed)
            return false;
        }

        std::atomic<bool> dead { false };

    private:
        struct Pending { juce::WaitableEvent event; juce::ValueTree reply; };
        juce::String base;
        juce::ChildProcess process;
        bridgeos::Memory* memory = nullptr;
        bridgeos::Event* go = nullptr;
        bridgeos::Event* done = nullptr;
        juce::CriticalSection lock;
        std::map<int, std::shared_ptr<Pending>> pending;
        std::atomic<int> nextId { 0 };
        int missed = 0;
        juce::WaitableEvent hello;

        void connectionMade() override {}
        void connectionLost() override { dead = true; if (onLost) onLost(); }
        void messageReceived (const juce::MemoryBlock& m) override
        {
            auto v = bridge::unpack (m);
            if (v.hasType ("HELLO")) { hello.signal(); return; }
            if (v.hasType ("REPLY"))
            {
                const juce::ScopedLock sl (lock);
                if (auto it = pending.find ((int) v["id"]); it != pending.end()) { it->second->reply = v; it->second->event.signal(); }
                return;
            }
            if (onNotice) onNotice (v);
        }
    };

    //==============================================================================
    /** A parameter of the bridged plugin: Studio's moves go over in the next block, the plugin's come back */
    class BridgedParameter : public juce::AudioPluginInstance::HostedParameter
    {
    public:
        BridgedParameter (const juce::ValueTree& p, int i, std::function<void (int, float)> moved)
            : index (i), name (p["name"].toString()), label (p["label"].toString()), defaultValue ((float) p["default"]),
              steps ((int) p["steps"]), discrete ((bool) p["discrete"]), boolean ((bool) p["boolean"]), automatable ((bool) p["automatable"]),
              onMoved (std::move (moved))
        {
            value = (float) p["value"];
        }

        juce::String getParameterID() const override           { return juce::String (index); }
        float getValue() const override                        { return value; }
        void setValue (float v) override                       { value = v; if (onMoved) onMoved (index, v); }
        void setFromPlugin (float v)                           { value = v; sendValueChangedMessageToListeners (v); }
        float getDefaultValue() const override                 { return defaultValue; }
        juce::String getName (int max) const override          { return name.substring (0, max); }
        juce::String getLabel() const override                 { return label; }
        int getNumSteps() const override                       { return steps > 0 ? steps : juce::AudioProcessor::getDefaultNumParameterSteps(); }
        bool isDiscrete() const override                       { return discrete; }
        bool isBoolean() const override                        { return boolean; }
        bool isAutomatable() const override                    { return automatable; }
        float getValueForText (const juce::String& t) const override { return t.getFloatValue(); }
        juce::String getText (float v, int) const override     { return juce::String (v, 3); }

    private:
        const int index;
        const juce::String name, label;
        const float defaultValue;
        const int steps;
        const bool discrete, boolean, automatable;
        std::atomic<float> value { 0 };
        std::function<void (int, float)> onMoved;
    };

    //==============================================================================
    class Instance;

    /** What Studio shows for a 32-bit plugin's window: the real one opens beside it */
    class Editor : public juce::AudioProcessorEditor
    {
    public:
        Editor (Instance& i);
        ~Editor() override;
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff171412));
            g.setColour (juce::Colour (0xffbdb4a9));
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText (getAudioProcessor()->getName() + " is a 32-bit plugin: its window opens on its own, in front of Studio.",
                              getLocalBounds().reduced (14, 10).withTrimmedBottom (40), juce::Justification::topLeft, 3);
        }
        void resized() override { show.setBounds (getLocalBounds().reduced (14, 10).removeFromBottom (32).removeFromLeft (180)); }
    private:
        Instance& instance;
        juce::TextButton show { "Show its window" };
    };

    /** A 32-bit plugin as the engine sees it */
    class Instance : public juce::AudioPluginInstance
    {
    public:
        static std::unique_ptr<Instance> open (const juce::PluginDescription& d, double rate, int block, juce::String& error)
        {
            auto c = std::make_unique<Connection>();
            if (! c->start (error)) return {};
            auto real = d;
            real.pluginFormatName = d.pluginFormatName.endsWith ("v3") ? "VST3" : "VST";   // the format the bridge opens it with
            real.name = real.name.upToLastOccurrenceOf (" (32-bit)", false, false);
            juce::ValueTree load ("LOAD");
            load.setProperty ("description", real.createXml()->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);
            load.setProperty ("sampleRate", rate > 0 ? rate : 48000.0, nullptr);
            load.setProperty ("blockSize", block > 0 ? block : 512, nullptr);
            auto r = c->request (load, 30000);
            if (! r.isValid()) { error = "the 32-bit bridge didn't load it in time"; return {}; }
            if (! (bool) r["ok"]) { error = r["error"].toString(); return {}; }
            return std::unique_ptr<Instance> (new Instance (d, std::move (c), r));
        }

        ~Instance() override { connection = nullptr; }

        const juce::String getName() const override              { return description.name; }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }
        double getTailLengthSeconds() const override             { return tail; }
        bool acceptsMidi() const override                        { return midiIn; }
        bool producesMidi() const override                       { return midiOut; }
        bool hasEditor() const override                          { return hasOwnEditor; }
        juce::AudioProcessorEditor* createEditor() override      { return new Editor (*this); }
        int getNumPrograms() override                            { return 1; }
        int getCurrentProgram() override                         { return 0; }
        void setCurrentProgram (int) override                    {}
        const juce::String getProgramName (int) override         { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void prepareToPlay (double rate, int block) override
        {
            sampleRate = rate;
            juce::ValueTree v ("PREPARE");
            v.setProperty ("sampleRate", rate, nullptr);
            v.setProperty ("blockSize", juce::jmin (block, bridge::maxBlock), nullptr);
            connection->request (v, 10000);
        }
        void releaseResources() override {}

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
        {
            const int n = buffer.getNumSamples();
            auto& s = *connection->shared;
            if (connection->dead || n > bridge::maxBlock)
            {
                buffer.clear();
                midi.clear();
                if (connection->dead && ! reportedDead.exchange (true))
                    juce::MessageManager::callAsync ([name = getName()] { studioLog ("32-bit plugin stopped (its bridge crashed or hung): " + name); });
                return;
            }
            s.sampleRate = sampleRate;
            s.numSamples = n;
            s.numIn = juce::jmin (bridge::maxChannels, buffer.getNumChannels());
            s.numOut = juce::jmin (bridge::maxChannels, buffer.getNumChannels());
            for (int c = 0; c < s.numIn; ++c) std::memcpy (s.audio[c], buffer.getReadPointer (c), sizeof (float) * (size_t) n);
            int m = 0;
            for (const auto meta : midi)
            {
                if (m >= bridge::maxMidi || meta.numBytes > 4) continue;
                auto& e = s.midiIn[m++];
                e.offset = meta.samplePosition; e.size = meta.numBytes;
                std::memcpy (e.data, meta.data, (size_t) meta.numBytes);
            }
            s.numMidiIn = m;
            {
                int p = 0;
                const auto scope = moves.read (moves.getNumReady());
                for (int i = 0; i < scope.blockSize1 && p < bridge::maxParamChanges; ++i) s.paramsIn[p++] = moveData[(size_t) (scope.startIndex1 + i)];
                for (int i = 0; i < scope.blockSize2 && p < bridge::maxParamChanges; ++i) s.paramsIn[p++] = moveData[(size_t) (scope.startIndex2 + i)];
                s.numParamsIn = p;
            }
            fillPlayhead (s);

            // a block's worth of time and then some: a plugin that misses it plays silence for that block
            const int timeout = juce::jmax (20, (int) (2000.0 * n / juce::jmax (8000.0, sampleRate)) + 10);
            if (! connection->runBlock (timeout)) { buffer.clear(); midi.clear(); return; }

            for (int c = 0; c < buffer.getNumChannels(); ++c)
                if (c < s.numOut) std::memcpy (buffer.getWritePointer (c), s.audio[c], sizeof (float) * (size_t) n);
            midi.clear();
            for (int i = 0; i < juce::jmin ((int) s.numMidiOut, bridge::maxMidi); ++i)
                midi.addEvent (s.midiOut[i].data, juce::jlimit (1, 4, (int) s.midiOut[i].size), juce::jlimit (0, n - 1, (int) s.midiOut[i].offset));
            auto& params = getParameters();
            for (int i = 0; i < juce::jmin ((int) s.numParamsOut, bridge::maxParamChanges); ++i)
                if (juce::isPositiveAndBelow (s.paramsOut[i].index, params.size()))
                    if (auto* p = dynamic_cast<BridgedParameter*> (params[s.paramsOut[i].index])) p->setFromPlugin (s.paramsOut[i].value);
            if (s.latencySamples != getLatencySamples())
            {
                const int l = s.latencySamples;
                juce::MessageManager::callAsync ([safe = juce::WeakReference<Instance> (this), l] { if (safe) safe->setLatencySamples (l); });
            }
        }

        void getStateInformation (juce::MemoryBlock& out) override
        {
            auto r = connection->request (juce::ValueTree ("GETSTATE"), 10000);
            if (r.isValid()) out.fromBase64Encoding (r["state"].toString());
        }

        void setStateInformation (const void* data, int size) override
        {
            juce::ValueTree v ("SETSTATE");
            v.setProperty ("state", juce::MemoryBlock (data, (size_t) size).toBase64Encoding(), nullptr);
            auto r = connection->request (v, 10000);
            auto& params = getParameters();
            for (int i = 0; i < juce::jmin (r.getNumChildren(), params.size()); ++i)
                if (auto* p = dynamic_cast<BridgedParameter*> (params[i]))
                    if (std::abs (p->getValue() - (float) r.getChild (i)["value"]) > 1.0e-6f) p->setFromPlugin ((float) r.getChild (i)["value"]);
        }

        void showWindow (bool show) { connection->send (juce::ValueTree (show ? "SHOWEDITOR" : "HIDEEDITOR")); }

    private:
        juce::PluginDescription description;
        std::unique_ptr<Connection> connection;
        double sampleRate = 48000, tail = 0;
        bool midiIn = false, midiOut = false, hasOwnEditor = false;
        std::atomic<bool> reportedDead { false };
        // Studio's parameter moves (automation, the generic sliders), sent with the next block
        juce::AbstractFifo moves { 2048 };
        std::array<bridge::ParamChange, 2048> moveData;
        juce::CriticalSection moveLock;

        static BusesProperties busesFor (int ins, int outs)
        {
            BusesProperties b;
            if (ins > 0) b = b.withInput ("Input", juce::AudioChannelSet::canonicalChannelSet (juce::jmin (ins, bridge::maxChannels)), true);
            if (outs > 0) b = b.withOutput ("Output", juce::AudioChannelSet::canonicalChannelSet (juce::jmin (outs, bridge::maxChannels)), true);
            return b;
        }

        Instance (const juce::PluginDescription& d, std::unique_ptr<Connection> c, const juce::ValueTree& r)
            : AudioPluginInstance (busesFor ((int) r["ins"], (int) r["outs"])), description (d), connection (std::move (c))
        {
            tail = r["tail"];
            midiIn = r["midiIn"];
            midiOut = r["midiOut"];
            hasOwnEditor = r["hasEditor"];
            setLatencySamples (r["latency"]);
            for (int i = 0; i < r.getNumChildren(); ++i)
                addHostedParameter (std::make_unique<BridgedParameter> (r.getChild (i), i, [this] (int index, float v)
                {
                    const juce::ScopedLock sl (moveLock);   // the engine and the window can both move parameters
                    const auto scope = moves.write (1);
                    if (scope.blockSize1 > 0) moveData[(size_t) scope.startIndex1] = { index, v };
                }));
            connection->onNotice = [safe = juce::WeakReference<Instance> (this)] (const juce::ValueTree& v)
            {
                if (v.hasType ("LATENCY"))
                    juce::MessageManager::callAsync ([safe, l = (int) v["samples"]] { if (safe) safe->setLatencySamples (l); });
            };
        }

        void fillPlayhead (bridge::Shared& s)
        {
            s.playing = s.recording = s.looping = 0;
            s.bpm = 120; s.sigNum = 4; s.sigDen = 4;
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                {
                    s.bpm = pos->getBpm().orFallback (120.0);
                    s.timeSeconds = pos->getTimeInSeconds().orFallback (0.0);
                    s.ppq = pos->getPpqPosition().orFallback (0.0);
                    s.ppqBarStart = pos->getPpqPositionOfLastBarStart().orFallback (0.0);
                    if (auto sig = pos->getTimeSignature()) { s.sigNum = sig->numerator; s.sigDen = sig->denominator; }
                    if (auto loop = pos->getLoopPoints()) { s.loopStartPpq = loop->ppqStart; s.loopEndPpq = loop->ppqEnd; }
                    s.playing = pos->getIsPlaying(); s.recording = pos->getIsRecording(); s.looping = pos->getIsLooping();
                }
        }

        JUCE_DECLARE_WEAK_REFERENCEABLE (Instance)
    };

    inline Editor::Editor (Instance& i) : AudioProcessorEditor (i), instance (i)
    {
        show.onClick = [this] { instance.showWindow (true); };
        addAndMakeVisible (show);
        setSize (360, 110);
        instance.showWindow (true);
    }

    inline Editor::~Editor() { instance.showWindow (false); }

    //==============================================================================
    /** "32-bit" (VST2) or "32-bit v3" (VST3): finds 32-bit plugins and opens each in a bridge */
    class Format : public juce::AudioPluginFormat
    {
    public:
        explicit Format (bool vst3) : isVst3 (vst3) {}

        // no "VST" in the names: Tracktion scans anything called VST in a helper of its own, which knows nothing of the bridge
        juce::String getName() const override { return isVst3 ? "32-bit v3" : "32-bit"; }

        /** Asks the bridge what's in the file ("--scan"), giving up on one that hangs or stops to show a window */
        void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& path) override
        {
            if (! available() || ! fileMightContainThisPluginType (path)) return;
            auto out = juce::File::createTempFile ("xml");
            juce::ChildProcess p;
            if (! p.start (juce::StringArray { helperExe().getFullPathName(), "--scan", path, out.getFullPathName() }, 0)) return;
            const auto t0 = juce::Time::getMillisecondCounter();
            unsigned long pid = 0;
            while (p.isRunning())
            {
                juce::Thread::sleep (100);
                const auto ms = juce::Time::getMillisecondCounter() - t0;
                if (pid == 0) if (auto x = juce::parseXML (out)) pid = (unsigned long) x->getIntAttribute ("pid");
                const bool prompt = ms > 4000 && pid != 0 && bridgeos::processHasWindow (pid);
                if (ms > 45000 || prompt)
                {
                    studioLog ("32-bit plugin skipped (" + juce::String (prompt ? "it opened a window, like an activation prompt" : "no answer in 45 s") + "): " + path);
                    if (pid != 0) bridgeos::killProcess (pid);
                    p.kill();
                    out.deleteFile();
                    return;
                }
            }
            if (auto xml = juce::parseXML (out))
                for (auto* e : xml->getChildIterator())
                {
                    auto d = std::make_unique<juce::PluginDescription>();
                    if (! d->loadFromXml (*e)) continue;
                    d->pluginFormatName = getName();
                    if (! d->name.endsWith (" (32-bit)")) d->name << " (32-bit)";   // so menus say which it is
                    results.add (d.release());
                }
            out.deleteFile();
        }

        bool fileMightContainThisPluginType (const juce::String& path) override
        {
            const juce::File f (path);
            if (! f.hasFileExtension (isVst3 ? ".vst3" : ".dll")) return false;
            return is32Bit (f);
        }

        juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return juce::File (id).getFileNameWithoutExtension(); }
        bool pluginNeedsRescanning (const juce::PluginDescription& d) override    { return juce::File (d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime; }
        bool doesPluginStillExist (const juce::PluginDescription& d) override     { return juce::File (d.fileOrIdentifier).exists(); }
        bool canScanForPlugins() const override                                   { return available(); }
        bool isTrivialToScan() const override                                     { return false; }

        juce::StringArray searchPathsForPlugins (const juce::FileSearchPath& paths, bool recursive, bool) override
        {
            juce::StringArray found;
            for (int i = 0; i < paths.getNumPaths(); ++i)
                for (const auto& entry : juce::RangedDirectoryIterator (paths[i], recursive, isVst3 ? "*.vst3" : "*.dll",
                                                                         juce::File::findFilesAndDirectories))
                {
                    const auto f = entry.getFile();
                    if (isVst3 && f.isDirectory() && f.getFileName().endsWithIgnoreCase (".vst3") == false) continue;
                    if (! isVst3 && f.isDirectory()) continue;
                    if (fileMightContainThisPluginType (f.getFullPathName())) found.addIfNotAlreadyThere (f.getFullPathName());
                }
            return found;
        }

        /** Where 32-bit plugins live (and the 64-bit VST folders, where people often put them too) */
        juce::FileSearchPath getDefaultLocationsToSearch() override
        {
            juce::FileSearchPath p;
            const auto x86 = juce::File (juce::SystemStats::getEnvironmentVariable ("ProgramFiles(x86)", "C:\\Program Files (x86)"));
            const auto x64 = juce::File (juce::SystemStats::getEnvironmentVariable ("ProgramW6432", "C:\\Program Files"));
            juce::Array<juce::File> dirs;
            if (isVst3) dirs = { x86.getChildFile ("Common Files\\VST3") };
            else dirs = { x86.getChildFile ("VstPlugins"), x86.getChildFile ("Steinberg\\VstPlugins"), x86.getChildFile ("Common Files\\VST2"),
                          x86.getChildFile ("Common Files\\Steinberg\\VST2"), x64.getChildFile ("VstPlugins"), x64.getChildFile ("Steinberg\\VstPlugins") };
            for (auto& d : dirs) if (d.isDirectory()) p.addIfNotAlreadyThere (d);
            return p;
        }

        bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

    protected:
        void createPluginInstance (const juce::PluginDescription& d, double rate, int block, PluginCreationCallback callback) override
        {
            juce::String error;
            auto p = Instance::open (d, rate, block, error);
            if (p == nullptr) studioLog ("32-bit plugin didn't open (" + error + "): " + d.fileOrIdentifier);
            callback (std::move (p), error);
        }

    private:
        const bool isVst3;
    };
}

/** Adds the 32-bit formats to an engine, when the bridge is installed beside Studio */
inline void registerBridgedPlugins (te::Engine& engine)
{
    auto& pm = engine.getPluginManager();
    for (auto* f : pm.pluginFormatManager.getFormats())
        if (f->getName().startsWith ("32-bit")) return;
    pm.pluginFormatManager.addFormat (std::make_unique<bridged::Format> (false));
    pm.pluginFormatManager.addFormat (std::make_unique<bridged::Format> (true));
}
#else
inline void registerBridgedPlugins (te::Engine&) {}
#endif

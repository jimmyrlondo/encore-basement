// Encore: Basement
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// "Encore Basement.exe --selftest <folder>" runs the core of the app without
// a window and exits 0 when everything works: make a song, import audio as
// tracks, mix, pan and mute, export, check the export, save and reopen.
// Writes only inside <folder>.
#pragma once

namespace selftest
{
    /** "--devices <file>": what audio devices and inputs Basement sees, before and after splitting pairs */
    inline int devices (const juce::File& outFile)
    {
        juce::String o;
        te::Engine engine { "Encore Basement" };
        auto& dm = engine.getDeviceManager();
        auto dump = [&] (const juce::String& when)
        {
            const juce::String nl = juce::newLine;
            o << "== " << when << nl;
            if (auto* dev = dm.deviceManager.getCurrentAudioDevice())
                o << "device: " << dev->getTypeName() << " / " << dev->getName() << ", inputs " << dev->getActiveInputChannels().countNumberOfSetBits()
                  << " of " << dev->getInputChannelNames().size() << " (" << dev->getInputChannelNames().joinIntoString (", ") << "), rate " << dev->getCurrentSampleRate() << nl;
            else o << "no device open" << nl;
            for (int i = 0; i < dm.getNumWaveInDevices(); i++)
                if (auto w = dm.getWaveInDevice (i))
                    o << "  in: " << w->getName() << " stereoPair=" << (int) w->isStereoPair() << " track=" << (int) w->isTrackDevice() << " enabled=" << (int) w->isEnabled() << nl;
        };
        dump ("at start");
        juce::Array<te::WaveInputDevice*> pairs;
        for (int i = 0; i < dm.getNumWaveInDevices(); i++)
            if (auto w = dm.getWaveInDevice (i)) if (! w->isTrackDevice() && w->isStereoPair()) pairs.add (w);
        for (auto* w : pairs) dm.setDeviceNumChannels (*w, 1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1000);
        dump ("after splitting " + juce::String (pairs.size()) + " pair(s)");
        outFile.replaceWithText (o);
        return 0;
    }

    inline int run (const juce::File& dir)
    {
        using namespace juce;
        int failures = 0;
        auto out = File (dir.getChildFile ("selftest.log"));
        out.replaceWithText ({});
        auto expect = [&] (const String& name, bool ok, const String& detail = {})
        {
            out.appendText ((ok ? "PASS " : "FAIL ") + name + (detail.isEmpty() ? String() : "  " + detail) + "\n");
            if (! ok) ++failures;
        };

        dir.createDirectory();
        auto songDir = dir.getChildFile ("Self Test");
        songDir.deleteRecursively();
        songDir.createDirectory();

        // two "stems": a 220 Hz tone and a 330 Hz tone, 4 s each, made here
        auto makeTone = [&] (const String& name, double hz, double seconds)
        {
            auto f = dir.getChildFile (name);
            f.deleteFile();
            WavAudioFormat wav;
            std::unique_ptr<OutputStream> os (f.createOutputStream().release());
            auto opts = AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24);
            auto w = wav.createWriterFor (os, opts);
            AudioBuffer<float> b (2, (int) (48000 * seconds));
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.25f * (float) std::sin (MathConstants<double>::twoPi * hz * i / 48000.0)), b.setSample (1, i, b.getSample (0, i));
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            return f;
        };
        auto a = makeTone ("tone a.wav", 220, 4), b = makeTone ("tone b.wav", 330, 4);
        expect ("test tones written", a.getSize() > 1000 && b.getSize() > 1000);

        auto* behaviour = new BasementBehaviour (true);   // no audio device: renders don't need one, and it leaves the interface to whatever else is using it
        auto* ui = new HeadlessUIBehaviour();
        te::Engine engine { "Encore Basement Selftest", std::unique_ptr<te::UIBehaviour> (ui), std::unique_ptr<te::EngineBehaviour> (behaviour) };
        registerEncorePlugins (engine);
        // plugin formats: VST3 and VST2 (through FST) are both compiled in
        {
            StringArray names;
            for (auto* f : engine.getPluginManager().pluginFormatManager.getFormats()) names.add (f->getName());
            expect ("VST3 and VST2 hosting", names.contains ("VST3") && names.contains ("VST"), names.joinIntoString (", "));
        }
        auto songFile = songDir.getChildFile ("Self Test.basement");
        behaviour->songFolder = [songFile] { return songFile.getParentDirectory(); };

        {
            auto edit = songs::create (engine, songFile);
            edit->ensureNumberOfAudioTracks (2);
            auto tracks = te::getAudioTracks (*edit);
            expect ("song has two tracks", tracks.size() == 2);
            int i = 0;
            for (auto& f : { a, b })
            {
                te::AudioFile af (engine, f);
                auto clip = tracks[i]->insertWaveClip (f.getFileNameWithoutExtension(), f, { { 0_tp, te::TimeDuration::fromSeconds (af.getLength()) }, {} }, false);
                expect ("import " + f.getFileName(), clip != nullptr && std::abs (clip->getPosition().getLength().inSeconds() - 4.0) < 0.01);
                ++i;
            }
            tracks[0]->getVolumePlugin()->setVolumeDb (-6.0f);
            tracks[1]->getVolumePlugin()->setPan (-1.0f);
            expect ("edit length", std::abs (edit->getLength().inSeconds() - 4.0) < 0.01, String (edit->getLength().inSeconds()));

            // where a recording on track 1 would go
            auto rec = behaviour->getFileForNewAudioRecording (*tracks[0], ".wav");
            expect ("recordings go in the song folder", rec.getParentDirectory() == songDir.getChildFile ("Recordings") && rec.getFileName() == "Track 1 take 1.wav", rec.getFullPathName());

            te::EditFileOperations (*edit).save (true, true, false);

            // export, through the same code as the Export mix button
            auto mix = songDir.getChildFile ("Self Test mix.wav");
            std::atomic<bool> done { false };
            juce::File rendered;
            juce::String err;
            ExportOptions eo;
            eo.tail = false;
            auto job = exportSong (engine, songFile, mix, eo, [&] (juce::File f, juce::String e) { rendered = f; err = e; done = true; });
            for (int i = 0; i < 600 && ! done; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            expect ("mix exported", rendered.existsAsFile(), rendered.getFullPathName() + " " + err);

            if (rendered.existsAsFile())
            {
                AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (rendered));
                expect ("mix is stereo, 24-bit, 4 s", r && r->numChannels == 2 && r->bitsPerSample == 24 && std::abs ((double) r->lengthInSamples / r->sampleRate - 4.0) < 0.05,
                        r ? String (r->numChannels) + " ch, " + String (r->bitsPerSample) + " bit, " + String ((double) r->lengthInSamples / r->sampleRate, 2) + " s" : String ("unreadable"));
                if (r)
                {
                    Range<float> lr[2];
                    r->readMaxLevels (r->sampleRate * 1, (int) r->sampleRate, lr, 2);
                    const float L = jmax (std::abs (lr[0].getStart()), lr[0].getEnd()), R = jmax (std::abs (lr[1].getStart()), lr[1].getEnd());
                    // track 1: 0.25 at -6 dB in both sides; track 2: 0.25 panned hard left
                    expect ("mix has sound", L > 0.05f && R > 0.05f, "L " + String (L, 3) + " R " + String (R, 3));
                    expect ("pan + volume land where expected (left louder)", L > R * 1.5f, "L " + String (L, 3) + " R " + String (R, 3));
                }
            }

            // a stem (track 2 alone, as FLAC) and a loudness-normalised mix
            {
                auto stem = songDir.getChildFile ("stem b.flac");
                ExportOptions so; so.flac = true; so.trackIndex = 1; so.tail = false;
                done = false; rendered = juce::File();
                auto stemJob = exportSong (engine, songFile, stem, so, [&] (juce::File f, juce::String e) { rendered = f; err = e; done = true; });
                for (int i = 0; i < 600 && ! done; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<AudioFormatReader> r (rendered.existsAsFile() ? fm.createReaderFor (rendered) : nullptr);
                Range<float> lr[2];
                if (r) r->readMaxLevels (r->sampleRate * 1, (int) r->sampleRate, lr, 2);
                // track 2 is panned hard left, so its stem has the left side only
                expect ("stem export (FLAC, one track)", r != nullptr && lr[0].getEnd() > 0.05f && lr[1].getEnd() < 0.01f, err);

                auto loud = songDir.getChildFile ("loud mix.wav");
                ExportOptions lo; lo.loudness = true; lo.tail = false;
                done = false; rendered = juce::File();
                auto loudJob = exportSong (engine, songFile, loud, lo, [&] (juce::File f, juce::String e) { rendered = f; err = e; done = true; });
                for (int i = 0; i < 600 && ! done; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                std::unique_ptr<AudioFormatReader> lr2 (rendered.existsAsFile() ? fm.createReaderFor (rendered) : nullptr);
                Range<float> ll[2];
                if (lr2) lr2->readMaxLevels (lr2->sampleRate * 1, (int) lr2->sampleRate, ll, 2);
                // loudness of steady tones: -0.691 + 10 log10 (sum of the channels' mean squares)
                // (K-weighting is flat around 220-330 Hz, and there's nothing quiet to gate)
                double lufs = -100.0;
                if (lr2)
                {
                    AudioBuffer<float> b ((int) lr2->numChannels, (int) lr2->sampleRate * 2);
                    lr2->read (&b, 0, b.getNumSamples(), (juce::int64) lr2->sampleRate, true, true);
                    double sum = 0;
                    for (int c = 0; c < b.getNumChannels(); ++c)
                    {
                        double ms = 0;
                        for (int i = 0; i < b.getNumSamples(); ++i) ms += (double) b.getSample (c, i) * b.getSample (c, i);
                        sum += ms / b.getNumSamples();
                    }
                    lufs = -0.691 + 10.0 * std::log10 (juce::jmax (1.0e-12, sum));
                }
                expect ("streaming-loud export lands at -14 LUFS", lr2 != nullptr && std::abs (lufs + 14.0) < 1.0, String (lufs, 1) + " LUFS");

                // an MP3 (LAME): plays back stereo with the left side louder, tagged, temp WAV cleaned up
                auto mp3File = songDir.getChildFile ("mix.mp3");
                ExportOptions mo; mo.mp3Kbps = 320; mo.tail = false; mo.tags.title = juce::CharPointer_UTF8 ("Selftest \xc3\xa9t\xc3\xa9");
                done = false; rendered = juce::File();
                auto mp3Job = exportSong (engine, songFile, mp3File, mo, [&] (juce::File f, juce::String e) { rendered = f; err = e; done = true; });
                for (int i = 0; i < 1200 && ! done; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                AudioFormatManager mp3s; mp3s.registerFormat (new MP3AudioFormat(), true);
                std::unique_ptr<AudioFormatReader> mr (rendered.existsAsFile() ? mp3s.createReaderFor (rendered) : nullptr);
                Range<float> ml[2];
                if (mr) mr->readMaxLevels ((juce::int64) mr->sampleRate, (int) mr->sampleRate, ml, 2);
                const bool tagged = rendered.existsAsFile() && rendered.loadFileAsString().startsWith ("ID3");
                const bool cleaned = ! songDir.getChildFile ("mix (making MP3).wav").exists();
                expect ("MP3 export plays back (stereo, left louder, tagged, temp WAV gone)",
                        mr != nullptr && mr->numChannels == 2 && ml[0].getEnd() > 0.05f && ml[0].getEnd() > ml[1].getEnd() * 1.5f && tagged && cleaned,
                        err + (mr ? " " + String (mr->sampleRate) + " Hz, L " + String (ml[0].getEnd(), 3) + " R " + String (ml[1].getEnd(), 3) : String (" unreadable"))
                            + (tagged ? "" : ", no ID3") + (cleaned ? "" : ", temp WAV left"));
            }

            te::EditFileOperations (*edit).save (true, true, false);
        }
        {
            auto reopened = songs::open (engine, songFile);
            auto tracks = te::getAudioTracks (*reopened);
            expect ("song reopens with its tracks and clips", tracks.size() == 2 && tracks[0]->getClips().size() == 1 && tracks[1]->getClips().size() == 1);
            if (tracks.size() == 2 && tracks[0]->getClips().size() == 1)
                if (auto wc = dynamic_cast<te::WaveAudioClip*> (tracks[0]->getClips()[0]))
                    expect ("reopened clip finds its audio file", wc->getAudioFile().isValid() && wc->getAudioFile().getFile() == a, wc->getAudioFile().getFile().getFullPathName());
            expect ("volume and pan saved", std::abs (tracks[0]->getVolumePlugin()->getVolumeDb() + 6.0f) < 0.1f && tracks[1]->getVolumePlugin()->getPan() < -0.99f);
        }

        // a song made by the New song form: details, tempo, tracks and planned inputs
        {
            SongPlan plan;
            plan.folder = dir.getChildFile ("Planned Song");
            plan.folder.deleteRecursively();
            plan.info.title = "Planned Song";
            plan.info.artist = "The Selftests";
            plan.info.album = "Basement Tapes";
            plan.info.genre = "Polka";
            plan.info.key = "A minor";
            plan.info.notes = "line one\nline two";
            plan.info.bpm = 92;
            plan.info.sigNum = 6; plan.info.sigDen = 8;
            plan.info.sampleRate = 44100;
            plan.info.bitDepth = 32;
            plan.tracks.add ({ "Vocal", "Input 1" });
            plan.tracks.add ({ "Guitar", "Input 2" });
            plan.tracks.add ({ "Spare", {} });
            { auto made = songs::createFromPlan (engine, plan); }
            expect ("planned song saved in its own folder", plan.songFile().existsAsFile(), plan.songFile().getFullPathName());
            if (auto e = songs::open (engine, plan.songFile()))
            {
                auto info = SongInfo::readFrom (*e, plan.songFile());
                expect ("song details saved", info.title == "Planned Song" && info.artist == "The Selftests" && info.album == "Basement Tapes"
                                               && info.genre == "Polka" && info.key == "A minor" && info.notes == "line one\nline two",
                        info.title + " / " + info.artist + " / " + info.key);
                expect ("tempo, time signature and format saved", std::abs (info.bpm - 92) < 0.01 && info.sigNum == 6 && info.sigDen == 8
                                                                   && info.sampleRate == 44100 && info.bitDepth == 32,
                        info.bpmText() + " " + info.sigText() + " " + info.formatText());
                auto tracks = te::getAudioTracks (*e);
                expect ("planned tracks with names and inputs", tracks.size() == 3 && tracks[0]->getName() == "Vocal" && tracks[2]->getName() == "Spare"
                                                                 && tracks[1]->state["basementInput"].toString() == "Input 2"
                                                                 && tracks[2]->state["basementInput"].toString() == "none",
                        String (tracks.size()) + " tracks");
            }
            else expect ("planned song reopens", false);

            // the template list includes the built-in starting points
            auto all = templates::all();
            auto find = [&] (const String& n) { for (auto& t : all) if (t.name == n) return t; return templates::Template {}; };
            expect ("built-in templates", find ("Full band, live").trackNames.size() == 10 && find ("Live multitrack").allInputs
                                           && find ("Mix stems").importStems && find ("Band, 4 inputs").needsInputs == 4);

            // a band template on a 4-input interface: drums on 1-4, overdubs back on input 1, and on a
            // 2-input interface the drum mics wrap round onto the inputs it has
            auto band4 = find ("Band, 4 inputs");
            StringArray four { "In 1", "In 2", "In 3", "In 4" }, two { "In 1", "In 2" }, used;
            expect ("template inputs: drums across all four, then overdubs on input 1",
                    band4.inputFor (3, four, used) == "In 4" && band4.inputFor (4, four, used) == "In 1" && band4.inputFor (8, four, used) == "In 2");
            expect ("template inputs wrap on a smaller interface", band4.inputFor (2, two, used) == "In 1" && band4.inputFor (3, two, used) == "In 2");

            SongPlan bandPlan;
            bandPlan.folder = dir.getChildFile ("Band Song");
            bandPlan.folder.deleteRecursively();
            bandPlan.info.title = "Band Song";
            StringArray usedInputs;
            for (int i = 0; i < band4.trackNames.size(); ++i)
            {
                auto in = band4.inputFor (i, four, usedInputs);
                usedInputs.add (in);
                bandPlan.tracks.add ({ band4.trackNames[i], in, band4.groups[i] });
            }
            { auto made = songs::createFromPlan (engine, bandPlan); }
            if (auto e = songs::open (engine, bandPlan.songFile()))
            {
                auto ts = te::getAudioTracks (*e);
                te::AudioTrack* drums = nullptr;
                te::AudioTrack* vocals = nullptr;
                te::AudioTrack* kick = nullptr;
                te::AudioTrack* bass = nullptr;
                te::AudioTrack* backing = nullptr;
                for (auto t : ts)
                {
                    if (groups::isGroup (*t) && t->getName() == "Drums") drums = t;
                    if (groups::isGroup (*t) && t->getName() == "Vocals") vocals = t;
                    if (t->getName() == "Kick") kick = t;
                    if (t->getName() == "Bass") bass = t;
                    if (t->getName() == "Backing vocals") backing = t;
                }
                expect ("band template makes its groups with the right tracks in them",
                        drums != nullptr && vocals != nullptr && groups::members (*drums).size() == 4 && groups::of (*kick) == drums
                            && groups::of (*backing) == vocals && bass != nullptr && groups::of (*bass) == nullptr && groups::all (*e).size() == 3,
                        String (groups::all (*e).size()) + " groups, " + String (ts.size()) + " tracks");
            }
            else expect ("band template song opens", false);
        }

        // the built-in Encore plugins: each loads on a track, passes audio, and keeps its settings
        {
            auto songFile2 = dir.getChildFile ("Encore Plugins").getChildFile ("Encore Plugins.basement");
            songFile2.getParentDirectory().deleteRecursively();
            songFile2.getParentDirectory().createDirectory();
            auto e = songs::create (engine, songFile2);
            e->ensureNumberOfAudioTracks (1);
            auto track = te::getAudioTracks (*e)[0];
            for (auto& entry : EncorePluginFormat::entries())
            {
                auto plugin = e->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, EncorePluginFormat::describe (entry));
                track->pluginList.insertPlugin (plugin, 0, nullptr);
                auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin.get());
                for (int i = 0; i < 100 && ext != nullptr && ext->getAudioPluginInstance() == nullptr; ++i)
                    MessageManager::getInstance()->runDispatchLoopUntil (20);
                auto* inst = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;
                bool passes = false, editor = false;
                if (inst != nullptr)
                {
                    inst->prepareToPlay (48000, 512);
                    AudioBuffer<float> buf (2, 512);
                    for (int i = 0; i < 512; ++i) buf.setSample (0, i, 0.2f * (float) std::sin (i * 0.05)), buf.setSample (1, i, buf.getSample (0, i));
                    MidiBuffer midi;
                    for (int block = 0; block < 20; ++block) inst->processBlock (buf, midi);
                    passes = std::isfinite (buf.getMagnitude (0, 512));
                    std::unique_ptr<AudioProcessorEditor> ed (inst->createEditorIfNeeded());
                    editor = ed != nullptr;
                    inst->editorBeingDeleted (ed.get());
                    ed = nullptr;
                    inst->releaseResources();
                }
                expect (String ("built-in ") + entry.name + " loads, runs and has its window", inst != nullptr && passes && editor,
                        ext != nullptr ? ext->getLoadError() : String());
            }
            te::EditFileOperations (*e).save (true, true, false);
            auto reopened = songs::open (engine, songFile2);
            int encoreOnTrack = 0, loaded = 0;
            StringArray ids;
            for (auto p : te::getAudioTracks (*reopened)[0]->pluginList.getPlugins())
                if (auto ext = dynamic_cast<te::ExternalPlugin*> (p); ext != nullptr && ext->desc.pluginFormatName == "Encore")
                {
                    ++encoreOnTrack;
                    ids.add (ext->desc.fileOrIdentifier);
                    for (int i = 0; i < 100 && ext->getAudioPluginInstance() == nullptr && ext->getLoadError().isEmpty(); ++i)
                        MessageManager::getInstance()->runDispatchLoopUntil (20);
                    if (ext->getAudioPluginInstance() != nullptr) ++loaded;
                }
            expect ("Encore plugins saved with the song and load again", encoreOnTrack == 4 && loaded == 4,
                    String (loaded) + " of " + String (encoreOnTrack) + " loaded: " + ids.joinIntoString (", "));
        }

        // an instrument track: the built-in synth plays a MIDI clip into the mix, and a MIDI file imports
        {
            auto synthSong = dir.getChildFile ("Synth Song").getChildFile ("Synth Song.basement");
            synthSong.getParentDirectory().deleteRecursively();
            synthSong.getParentDirectory().createDirectory();
            {
                auto e = songs::create (engine, synthSong);
                e->ensureNumberOfAudioTracks (1);
                auto t = te::getAudioTracks (*e)[0];
                instruments::makeInstrumentTrack (*t, e->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}));
                auto clip = t->insertMIDIClip ({ 0_tp, te::TimePosition::fromSeconds (2.0) }, nullptr);
                for (int i = 0; i < 4; ++i) clip->getSequence().addNote (60 + i * 4, te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (0.9), 100, 0, nullptr);
                expect ("instrument track has its synth first", instruments::isInstrumentTrack (*t));

                // a MIDI file made here, then imported onto the same track
                juce::MidiMessageSequence seq;
                for (int i = 0; i < 8; ++i)
                {
                    seq.addEvent (juce::MidiMessage::noteOn (1, 48 + i, (juce::uint8) 90), i * 480.0);
                    seq.addEvent (juce::MidiMessage::noteOff (1, 48 + i), i * 480.0 + 400.0);
                }
                seq.updateMatchedPairs();
                juce::MidiFile mf;
                mf.setTicksPerQuarterNote (480);
                mf.addTrack (seq);
                auto midiFile = dir.getChildFile ("test.mid");
                midiFile.deleteFile();
                { juce::FileOutputStream os (midiFile); mf.writeTo (os); }
                auto imported = te::createClipFromFile (midiFile, *t, false);
                expect ("MIDI file import", imported != nullptr && imported->getSequence().getNumNotes() == 8,
                        imported ? String (imported->getSequence().getNumNotes()) + " notes" : String ("no clip"));
                if (imported) imported->removeFromParent();

                // the piano roll's note edits (shared with the MIDI menu): quantize, transpose, velocity, undo
                {
                    auto* qc = t->insertMIDIClip ({ te::TimePosition::fromSeconds (4.0), te::TimePosition::fromSeconds (6.0) }, nullptr).get();
                    auto& seq = qc->getSequence();
                    seq.addNote (60, te::BeatPosition::fromBeats (0.07), te::BeatDuration::fromBeats (0.5), 90, 0, nullptr);
                    seq.addNote (64, te::BeatPosition::fromBeats (1.21), te::BeatDuration::fromBeats (0.5), 90, 0, nullptr);
                    auto notes = midiedit::allNotes (*qc);
                    e->getUndoManager().beginNewTransaction ("test quantize");
                    midiedit::quantize (*qc, notes, 0.25);
                    const double content = qc->getContentStartBeat().inBeats();
                    auto onGrid = [&] (te::MidiNote* n) { const double b = (content + n->getStartBeat().inBeats()) / 0.25; return std::abs (b - std::round (b)) < 1.0e-6; };
                    expect ("quantize puts notes on the grid", onGrid (notes[0]) && onGrid (notes[1]),
                            String (content + notes[0]->getStartBeat().inBeats(), 3) + ", " + String (content + notes[1]->getStartBeat().inBeats(), 3));
                    e->getUndoManager().beginNewTransaction ("test transpose");
                    midiedit::transpose (*qc, notes, 12);
                    midiedit::velocity (*qc, notes, -1, 50);
                    expect ("transpose and velocity", notes[0]->getNoteNumber() == 72 && notes[1]->getNoteNumber() == 76 && notes[0]->getVelocity() == 127,
                            String (notes[0]->getNoteNumber()) + " vel " + String (notes[0]->getVelocity()));
                    e->getUndoManager().undo();
                    auto after = midiedit::allNotes (*qc);
                    int lowest = 128;
                    for (auto* n : after) lowest = juce::jmin (lowest, n->getNoteNumber());
                    expect ("note edits undo", after.size() == 2 && lowest == 60, String (lowest));
                    qc->removeFromParent();
                }
                te::EditFileOperations (*e).save (true, true, false);
            }
            auto out2 = synthSong.getSiblingFile ("synth.wav");
            ExportOptions so; so.tail = false;
            std::atomic<bool> d2 { false };
            juce::File r2; juce::String e2;
            auto job2 = exportSong (engine, synthSong, out2, so, [&] (juce::File f, juce::String e) { r2 = f; e2 = e; d2 = true; });
            for (int i = 0; i < 600 && ! d2; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            AudioFormatManager fm2; fm2.registerBasicFormats();
            std::unique_ptr<AudioFormatReader> rr (r2.existsAsFile() ? fm2.createReaderFor (r2) : nullptr);
            Range<float> lv[2];
            if (rr) rr->readMaxLevels (0, (int) rr->lengthInSamples, lv, 2);
            expect ("the synth plays the MIDI clip into the mix", rr != nullptr && lv[0].getEnd() > 0.01f, rr ? String (lv[0].getEnd(), 3) : e2);
        }

        // group tracks: two tones play into a group; the group's fader controls them both,
        // a group stem has them, and a member's stem still has its own sound
        {
            auto groupSong = dir.getChildFile ("Group Song").getChildFile ("Group Song.basement");
            groupSong.getParentDirectory().deleteRecursively();
            groupSong.getParentDirectory().createDirectory();
            {
                auto e = songs::create (engine, groupSong);
                e->ensureNumberOfAudioTracks (2);
                auto ts = te::getAudioTracks (*e);
                int i = 0;
                for (auto& f : { a, b })
                {
                    te::AudioFile af (engine, f);
                    ts[i]->insertWaveClip (f.getFileNameWithoutExtension(), f, { { 0_tp, te::TimeDuration::fromSeconds (af.getLength()) }, {} }, false);
                    ++i;
                }
                auto g = groups::add (*e, "Drums", { ts[0], ts[1] });
                expect ("group track made with both tracks playing into it",
                        g != nullptr && groups::isGroup (*g) && groups::members (*g).size() == 2 && groups::of (*ts[0]) == g && groups::of (*ts[1]) == g);
                expect ("a group can't play into one of its own tracks (no loops)", g != nullptr && ! groups::canRoute (*g, *ts[0]));
                if (g != nullptr) g->getVolumePlugin()->setVolumeDb (-100.0f);   // group fader all the way down
                te::EditFileOperations (*e).save (true, true, false);
            }
            auto peakOf = [&] (int trackIndex, const String& name) -> float
            {
                auto out = groupSong.getSiblingFile (name);
                ExportOptions go; go.tail = false; go.trackIndex = trackIndex;
                std::atomic<bool> d { false };
                juce::File r; juce::String err;
                auto job = exportSong (engine, groupSong, out, go, [&] (juce::File f, juce::String e) { r = f; err = e; d = true; });
                for (int k = 0; k < 600 && ! d; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
                AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<AudioFormatReader> rd (r.existsAsFile() ? fm.createReaderFor (r) : nullptr);
                if (rd == nullptr) return -1.0f;
                Range<float> lv[2];
                rd->readMaxLevels (0, (int) rd->lengthInSamples, lv, 2);
                return juce::jmax (lv[0].getEnd(), lv[1].getEnd(), -lv[0].getStart());
            };
            const float mix = peakOf (-1, "group mix.wav");
            expect ("the group's fader controls its tracks (down = silent mix)", mix >= 0.0f && mix < 0.001f, String (mix, 4));
            // the group is the third track; its stem is it and its members (its fader is down, so turn it up in a copy first)
            {
                auto e = songs::open (engine, groupSong);
                if (e) { te::getAudioTracks (*e)[2]->getVolumePlugin()->setVolumeDb (0.0f); te::EditFileOperations (*e).save (true, true, false); }
            }
            const float groupStem = peakOf (2, "group stem.wav");
            expect ("a group's stem has its tracks in it", groupStem > 0.05f, String (groupStem, 3));
            {
                auto e = songs::open (engine, groupSong);
                if (e) { te::getAudioTracks (*e)[2]->getVolumePlugin()->setVolumeDb (-100.0f); te::EditFileOperations (*e).save (true, true, false); }
            }
            const float memberStem = peakOf (0, "member stem.wav");
            expect ("a grouped track's stem is that track on its own", memberStem > 0.05f, String (memberStem, 3));
            {
                auto e = songs::open (engine, groupSong);
                auto ts = e ? te::getAudioTracks (*e) : juce::Array<te::AudioTrack*>();
                expect ("groups are saved with the song", ts.size() == 3 && groups::isGroup (*ts[2]) && groups::of (*ts[0]) == ts[2] && groups::of (*ts[1]) == ts[2]);
            }
        }

        // the tuner's pitch reading: guitar low E (82.41 Hz), bass low E (41.20 Hz), A4 with harmonics
        {
            auto centsOff = [] (double hz, std::function<float (double)> wave)
            {
                const double fs = 24000.0;
                std::vector<float> x (2048);
                for (size_t i = 0; i < x.size(); ++i) x[i] = wave ((double) i / fs);
                const float f = TunerPitch::estimate (x.data(), (int) x.size(), fs);
                return f > 0 ? 1200.0 * std::log2 (f / hz) : 9999.0;
            };
            const double tau = MathConstants<double>::twoPi;
            const double e2 = centsOff (82.41, [tau] (double t) { return (float) (0.3 * std::sin (tau * 82.41 * t)); });
            const double e1 = centsOff (41.20, [tau] (double t) { return (float) (0.3 * std::sin (tau * 41.20 * t)); });
            const double a4 = centsOff (440.0, [tau] (double t) { return (float) (0.2 * std::sin (tau * 440 * t) + 0.12 * std::sin (tau * 880 * t) + 0.08 * std::sin (tau * 1320 * t)); });
            expect ("tuner reads guitar and bass low E and a bright A4 within 2 cents", std::abs (e2) < 2 && std::abs (e1) < 2 && std::abs (a4) < 2,
                    String (e2, 2) + " / " + String (e1, 2) + " / " + String (a4, 2) + " cents");
        }

        out.appendText (String (failures == 0 ? "ALL PASSED" : "FAILED") + ": " + String (failures) + " failure(s)\n");
        return failures == 0 ? 0 : 1;
    }
}

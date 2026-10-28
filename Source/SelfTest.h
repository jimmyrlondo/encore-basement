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

        auto* behaviour = new BasementBehaviour();
        auto* ui = new HeadlessUIBehaviour();
        te::Engine engine { "Encore Basement Selftest", std::unique_ptr<te::UIBehaviour> (ui), std::unique_ptr<te::EngineBehaviour> (behaviour) };
        registerEncorePlugins (engine);
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
            auto job = exportSong (engine, songFile, mix, 48000.0, [&] (juce::File f, juce::String e) { rendered = f; err = e; done = true; });
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
            expect ("built-in templates", all.size() >= 6 && all[2].trackNames.size() == 10 && all[3].allInputs && all[5].importStems);
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
            int encoreOnTrack = 0;
            for (auto p : te::getAudioTracks (*reopened)[0]->pluginList.getPlugins())
                if (auto ext = dynamic_cast<te::ExternalPlugin*> (p)) if (ext->desc.pluginFormatName == "Encore") ++encoreOnTrack;
            expect ("Encore plugins saved with the song", encoreOnTrack == 4, String (encoreOnTrack));
        }

        out.appendText (String (failures == 0 ? "ALL PASSED" : "FAILED") + ": " + String (failures) + " failure(s)\n");
        return failures == 0 ? 0 : 1;
    }
}

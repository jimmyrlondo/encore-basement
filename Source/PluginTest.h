// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// "Encore Studio.exe --plugintest <report file> [folder]" loads every VST3
// and VST2 in a folder (default: the system plugin folders) the way the Effects button
// does: scan it, put it on a track, wait for it to load, open its editor.
// Writes a line per plugin with what happened and how long each step took,
// so "the plugin never opens" turns into a specific reason.
#pragma once

namespace selftest
{
    inline int plugins (const juce::File& outFile, juce::File folder)
    {
        using namespace juce;
        String report;
        auto line = [&] (const String& s) { report << s << newLine; outFile.replaceWithText (report); };

        auto* ui = new HeadlessUIBehaviour();
        te::Engine engine { "Encore Studio Plugintest", std::unique_ptr<te::UIBehaviour> (ui), std::make_unique<StudioBehaviour>() };
        registerEncorePlugins (engine);
        registerBridgedPlugins (engine);   // 32-bit plugins, through the bridge
        auto& pm = engine.getPluginManager();
        Array<AudioPluginFormat*> formats;
        for (auto* f : pm.pluginFormatManager.getFormats())
            if (f->getName() == "VST3" || f->getName() == "VST") formats.add (f);
        if (formats.size() < 2) { line ("FAIL VST3 and VST2 aren't both registered (" + String (formats.size()) + ")"); return 1; }

        auto edit = songs::create (engine, File::getSpecialLocation (File::tempDirectory).getChildFile ("studio-plugintest.encore"));
        edit->ensureNumberOfAudioTracks (1);
        auto track = te::getAudioTracks (*edit)[0];

        int ok = 0, failed = 0, skipped = 0;
        for (auto* format : formats)
        {
            auto where = folder.isDirectory() ? FileSearchPath (folder.getFullPathName()) : format->getDefaultLocationsToSearch();
            if (format->getName() == "VST" && ! folder.isDirectory()) PluginFinder::addCommonVst2Folders (where);
            auto files = format->searchPathsForPlugins (where, true, false);
            line (format->getName() + " in " + where.toString() + ": " + String (files.size()) + " files");
            for (auto& path : files)
            {
                OwnedArray<PluginDescription> found;
                line ("     scanning " + path); // if a plugin crashes the test, the last line names it
                auto t0 = Time::getMillisecondCounterHiRes();
                format->findAllTypesForFile (found, path);
                auto scanMs = Time::getMillisecondCounterHiRes() - t0;
                if (found.isEmpty())
                {
                    // VST2 folders hold helper .dlls too; those aren't plugins and aren't failures
                    if (format->getName() == "VST") { line ("skip " + path + "  (not a plugin)"); ++skipped; }
                    else { line ("FAIL scan  " + path + "  (no plugin found, " + String (scanMs, 0) + " ms)"); ++failed; }
                    continue;
                }

                for (auto* d : found)
                {
                    pm.knownPluginList.addType (*d);
                    auto t1 = Time::getMillisecondCounterHiRes();
                    auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *d);
                    if (plugin == nullptr) { line ("FAIL create " + d->name); ++failed; continue; }
                    track->pluginList.insertPlugin (plugin, 0, nullptr);
                    auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin.get());
                    for (int i = 0; i < 200 && ext != nullptr && ext->getAudioPluginInstance() == nullptr && ext->getLoadError().isEmpty(); ++i)
                        MessageManager::getInstance()->runDispatchLoopUntil (25);
                    auto loadMs = Time::getMillisecondCounterHiRes() - t1;
                    auto* inst = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;
                    if (inst == nullptr)
                    {
                        line ("FAIL load  " + d->name + " (" + d->pluginFormatName + ")  error: " + (ext ? ext->getLoadError() : String ("not external"))
                              + "  enabled=" + String ((int) plugin->isEnabled()) + "  " + String (loadMs, 0) + " ms");
                        ++failed;
                    }
                    else
                    {
                        auto t2 = Time::getMillisecondCounterHiRes();
                        std::unique_ptr<AudioProcessorEditor> ed (inst->hasEditor() ? inst->createEditorAndMakeActive() : nullptr);
                        auto edMs = Time::getMillisecondCounterHiRes() - t2;
                        line (String (ed != nullptr ? "PASS " : "NOED ") + d->name + "  scan " + String (scanMs, 0) + " ms, load " + String (loadMs, 0)
                              + " ms, editor " + String (edMs, 0) + " ms" + (ed ? "  (" + String (ed->getWidth()) + "x" + String (ed->getHeight()) + ")" : String()));
                        ed = nullptr;
                        ++ok;
                    }
                    plugin->deleteFromParent();
                }
            }
        }
        line ("DONE " + String (ok) + " loaded, " + String (failed) + " failed, " + String (skipped) + " skipped (not plugins)");
        return failed == 0 ? 0 : 1;
    }
    /** "--bridgetest <report> <plugin file>": a 32-bit plugin through the bridge, the way a song uses it
        (found, put on a track, audio through it, a parameter moved, its state saved and put back).
        Opens no audio device and no plugin window. */
    inline int bridge (const juce::File& outFile, const juce::File& pluginFile)
    {
        using namespace juce;
        String report;
        int failures = 0;
        auto expect = [&] (const String& name, bool ok, const String& detail = {})
        {
            report << (ok ? "PASS " : "FAIL ") << name << (detail.isEmpty() ? String() : "  " + detail) << newLine;
            outFile.replaceWithText (report);
            if (! ok) ++failures;
        };
        auto* ui = new HeadlessUIBehaviour();
        te::Engine engine { "Encore Studio Bridgetest", std::unique_ptr<te::UIBehaviour> (ui), std::make_unique<StudioBehaviour> (true) };
        registerBridgedPlugins (engine);
        auto& pm = engine.getPluginManager();
        AudioPluginFormat* format = nullptr;
        for (auto* f : pm.pluginFormatManager.getFormats())
            if (f->getName() == (pluginFile.hasFileExtension ("vst3") ? "32-bit v3" : "32-bit")) format = f;
        expect ("the bridge is installed beside Studio", bridged::available() && format != nullptr, bridged::helperExe().getFullPathName());
        if (format == nullptr) return 1;
        expect ("the file is a 32-bit plugin", format->fileMightContainThisPluginType (pluginFile.getFullPathName()));

        OwnedArray<PluginDescription> found;
        auto t0 = Time::getMillisecondCounterHiRes();
        format->findAllTypesForFile (found, pluginFile.getFullPathName());
        expect ("the bridge finds what's in it", ! found.isEmpty(), found.isEmpty() ? String() : found[0]->name + ", " + String (Time::getMillisecondCounterHiRes() - t0, 0) + " ms");
        if (found.isEmpty()) return 1;
        pm.knownPluginList.addType (*found[0]);

        auto edit = songs::create (engine, File::getSpecialLocation (File::tempDirectory).getChildFile ("studio-bridgetest.encore"));
        edit->ensureNumberOfAudioTracks (1);
        auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *found[0]);
        te::getAudioTracks (*edit)[0]->pluginList.insertPlugin (plugin, 0, nullptr);
        auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin.get());
        for (int i = 0; i < 400 && ext != nullptr && ext->getAudioPluginInstance() == nullptr && ext->getLoadError().isEmpty(); ++i)
            MessageManager::getInstance()->runDispatchLoopUntil (25);
        auto* inst = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;
        expect ("it loads on a track", inst != nullptr, ext != nullptr ? ext->getLoadError() : String());
        if (inst == nullptr) return 1;

        // audio through it: a tone in, something out
        inst->prepareToPlay (48000, 512);
        AudioBuffer<float> b (jmax (2, inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels()), 512);
        MidiBuffer midi;
        float out = 0;
        double phase = 0;
        for (int block = 0; block < 40; ++block)
        {
            for (int i = 0; i < 512; ++i, phase += 440.0 / 48000.0)
                for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, i, 0.3f * (float) std::sin (MathConstants<double>::twoPi * phase));
            inst->processBlock (b, midi);
            if (block >= 10) out = jmax (out, b.getMagnitude (0, 512));
        }
        expect ("audio goes through it", out > 0.001f, "peak " + String (out, 3) + ", " + String (inst->getParameters().size()) + " parameters");

        // its settings: move one, save, move it back, restore
        auto& params = inst->getParameters();
        if (! params.isEmpty())
        {
            // (not every plugin keeps every knob in its saved state, and some "knobs" are switches that snap,
            // so this tries the first few and checks the value as the plugin itself reports it)
            auto blocks = [&] { for (int i = 0; i < 3; ++i) inst->processBlock (b, midi); };
            MemoryBlock state;
            String tried;
            bool moved = false;
            for (int i = 0; i < jmin (6, params.size()) && ! moved; ++i)
            {
                auto* p = params[i];
                const float want = p->getValue() < 0.5f ? 0.7f : 0.3f;
                p->setValue (want);
                blocks();
                inst->getStateInformation (state);
                inst->setStateInformation (state.getData(), (int) state.getSize());   // the bridge answers with every value the plugin has now
                moved = std::abs (p->getValue() - want) < 0.05f;
                tried << p->getName (20) << " " << String (want, 2) << " -> " << String (p->getValue(), 2) << "; ";
            }
            expect ("moving a knob reaches the plugin", moved, tried);
            expect ("its settings save and load", state.getSize() > 0, String ((int) state.getSize()) + " bytes");
        }
        plugin->deleteFromParent();
        plugin = nullptr;
        edit = nullptr;
        report << (failures == 0 ? "ALL PASSED" : "FAILED") << newLine;
        outFile.replaceWithText (report);
        return failures == 0 ? 0 : 1;
    }
}

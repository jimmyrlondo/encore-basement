// Encore: Basement
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// "Encore Basement.exe --plugintest <report file> [folder]" loads every VST3
// in a folder (default: the system VST3 folder) the way the Effects button
// does: scan it, put it on a track, wait for it to load, open its editor.
// Writes a line per plugin with what happened and how long each step took,
// so "the plugin never opens" turns into a specific reason.
#pragma once

namespace selftest
{
    inline int plugins (const juce::File& outFile, juce::File folder)
    {
        using namespace juce;
        if (! folder.isDirectory()) folder = File::getSpecialLocation (File::globalApplicationsDirectory).getChildFile ("Common Files").getChildFile ("VST3");
        String report;
        auto line = [&] (const String& s) { report << s << newLine; outFile.replaceWithText (report); };

        auto* ui = new HeadlessUIBehaviour();
        te::Engine engine { "Encore Basement Plugintest", std::unique_ptr<te::UIBehaviour> (ui), std::make_unique<BasementBehaviour>() };
        registerEncorePlugins (engine);
        auto& pm = engine.getPluginManager();
        VST3PluginFormat* vst3 = nullptr;
        for (auto* f : pm.pluginFormatManager.getFormats())
            if (auto* v = dynamic_cast<VST3PluginFormat*> (f)) vst3 = v;
        if (vst3 == nullptr) { line ("FAIL no VST3 format registered"); return 1; }

        auto edit = songs::create (engine, File::getSpecialLocation (File::tempDirectory).getChildFile ("basement-plugintest.basement"));
        edit->ensureNumberOfAudioTracks (1);
        auto track = te::getAudioTracks (*edit)[0];

        int ok = 0, failed = 0;
        auto files = vst3->searchPathsForPlugins (FileSearchPath (folder.getFullPathName()), true, false);
        line ("folder " + folder.getFullPathName() + ": " + String (files.size()) + " VST3 bundles");
        for (auto& path : files)
        {
            OwnedArray<PluginDescription> found;
            auto t0 = Time::getMillisecondCounterHiRes();
            vst3->findAllTypesForFile (found, path);
            auto scanMs = Time::getMillisecondCounterHiRes() - t0;
            if (found.isEmpty()) { line ("FAIL scan  " + path + "  (no plugin found, " + String (scanMs, 0) + " ms)"); ++failed; continue; }

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
        line ("DONE " + String (ok) + " loaded, " + String (failed) + " failed");
        return failed == 0 ? 0 : 1;
    }
}

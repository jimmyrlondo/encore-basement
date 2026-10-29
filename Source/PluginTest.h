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
}

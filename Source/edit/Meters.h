// Encore: Studio
// Copyright (C) 2026 Encore Studio. AGPLv3 (see LICENSE).
//
// Level meters: one on every track header (the input while the track is armed
// or monitoring, what the track plays otherwise) and a stereo one for the whole
// mix in the toolbar. Peak hold, and a red clip light that stays lit until you
// click it, so a clipped take never goes unnoticed.
#pragma once

class LevelMeter : public Component,
                   public SettableTooltipClient,
                   private Timer
{
public:
    enum class Orientation { vertical, horizontal };

    LevelMeter (Orientation o = Orientation::vertical) : orientation (o)
    {
        setInterceptsMouseClicks (true, false);
        setTooltip ("Level. Red at the end means it clipped: click to reset.");
        startTimerHz (30);
    }

    ~LevelMeter() override { setSource (nullptr); }

    /** What to measure (nullptr for nothing). Safe to call often: it only
        re-attaches when the source actually changes. */
    void setSource (te::LevelMeasurer* m)
    {
        if (m == source.get()) return;
        if (auto* old = source.get()) old->removeClient (client);
        source = m;
        if (m != nullptr) m->addClient (client);
        for (auto& c : chans) c = {};
        repaint();
    }

    void mouseDown (const MouseEvent&) override
    {
        for (auto& c : chans) { c.clipped = false; c.hold = -100.0f; }
        repaint();
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::base);
        g.fillRoundedRectangle (r, 2.0f);
        const int n = jlimit (1, 2, numChannels);
        const float gap = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            auto lane = orientation == Orientation::vertical
                            ? r.withWidth ((r.getWidth() - gap * (n - 1)) / n).withX (r.getX() + i * ((r.getWidth() + gap) / n))
                            : r.withHeight ((r.getHeight() - gap * (n - 1)) / n).withY (r.getY() + i * ((r.getHeight() + gap) / n));
            auto& c = chans[(size_t) i];
            const float clipLen = 3.0f;
            auto body = orientation == Orientation::vertical ? lane.withTrimmedTop (clipLen + 1) : lane.withTrimmedRight (clipLen + 1);
            auto clipLight = orientation == Orientation::vertical ? lane.removeFromTop (clipLen) : lane.removeFromRight (clipLen);

            auto fill = [&] (float db, Colour col, bool line)
            {
                const float p = proportion (db);
                if (p <= 0.0f) return;
                Rectangle<float> f = orientation == Orientation::vertical ? body.withTop (body.getBottom() - body.getHeight() * p)
                                                                          : body.withWidth (body.getWidth() * p);
                if (line) f = orientation == Orientation::vertical ? f.withHeight (1.5f) : f.withX (f.getRight() - 1.5f).withWidth (1.5f);
                g.setColour (col);
                g.fillRect (f);
            };
            // green up to -12, amber to -3, red above
            const float db = c.level;
            fillRange (g, body, -60.0f, jmin (db, -12.0f), Colour (0xff22c55e));
            fillRange (g, body, -12.0f, jmin (db, -3.0f), Colour (0xfff59e0b));
            fillRange (g, body, -3.0f, db, theme::record);
            if (c.hold > -60.0f) fill (c.hold, theme::paper.withAlpha (0.8f), true);
            g.setColour (c.clipped ? theme::record : theme::strong);
            g.fillRect (clipLight);
        }
    }

private:
    Orientation orientation;
    WeakReference<te::LevelMeasurer> source;
    te::LevelMeasurer::Client client;
    struct Chan { float level = -100.0f, hold = -100.0f; uint32 holdTime = 0; bool clipped = false; };
    std::array<Chan, 2> chans;
    int numChannels = 1;

    static float proportion (float db)
    {
        // -60..+6 dB, with more room near the top where it matters
        if (db <= -60.0f) return 0.0f;
        const float x = jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        return std::pow (x, 1.6f);
    }

    void fillRange (Graphics& g, Rectangle<float> body, float fromDb, float toDb, Colour col)
    {
        const float a = proportion (fromDb), b = proportion (toDb);
        if (b <= a) return;
        g.setColour (col);
        if (orientation == Orientation::vertical)
            g.fillRect (body.getX(), body.getBottom() - body.getHeight() * b, body.getWidth(), body.getHeight() * (b - a));
        else
            g.fillRect (body.getX() + body.getWidth() * a, body.getY(), body.getWidth() * (b - a), body.getHeight());
    }

    void timerCallback() override
    {
        const auto now = Time::getMillisecondCounter();
        bool changed = false;
        if (source.get() == nullptr)
        {
            for (auto& c : chans) if (c.level > -100.0f) { c.level = -100.0f; changed = true; }
        }
        else
        {
            numChannels = jlimit (1, 2, client.getNumChannelsUsed());
            for (int i = 0; i < 2; ++i)
            {
                auto& c = chans[(size_t) i];
                auto measured = client.getAndClearAudioLevel (i).dB;
                // fall back smoothly (about 20 dB a second)
                const float next = jmax (measured, c.level - 0.7f);
                if (std::abs (next - c.level) > 0.05f) { c.level = next; changed = true; }
                if (measured > c.hold || now - c.holdTime > 1500) { c.hold = measured; c.holdTime = now; changed = true; }
                if (measured >= -0.1f && ! c.clipped) { c.clipped = true; changed = true; }
            }
        }
        if (changed) repaint();
    }
};

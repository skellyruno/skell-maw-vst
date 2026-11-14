#include "MawEditor.h"
#include "MawArtData.h"

namespace
{
    // The butterflies were cut out of the illustration (Tools/make_layers.py) and are animated on top of it.
    // home = their place in the art (fractions of the image), head = direction the sprite's head points (radians).
    struct SpriteInfo { float homeX, homeY, head; };
    constexpr SpriteInfo kSprites[6] = { { 0.3765f, 0.1015f, -0.436f }, { 0.6420f, 0.1590f, -1.571f }, { 0.1350f, 0.1745f, -0.506f },
                                         { 0.0185f, 0.2377f, -1.571f }, { 0.0975f, 0.3247f, -1.571f }, { 0.6538f, 0.4700f, -2.531f } };
    constexpr int kCell = 200;   // sprite cell size in butterflies.png

    // The tongue sprite (tongue.png) was cut from the art at this origin; it swings about the pivot where it meets the jaw.
    constexpr float kTongueX = 934.0f, kTongueY = 1236.0f, kPivotX = 1205.0f, kPivotY = 1445.0f;

    // Pupil and blossoms, in art pixels.
    constexpr float kPupilX = 1739.0f, kPupilY = 688.0f, kPupilR = 44.0f, kIrisX = 1741.0f, kIrisY = 706.0f;
    constexpr float kBlossoms[5][2] = { { 1675, 60 }, { 1772, 60 }, { 1783, 98 }, { 1905, 122 }, { 1982, 104 } };

    float flapAmount (float phase) noexcept { return 0.22f + 0.78f * std::abs (std::cos (phase)); }   // 1 = wings open, 0.22 = nearly closed
}

namespace { constexpr int designSize = 720; }

MawEditor::MawEditor (MawProcessorBase& p)
    : AudioProcessorEditor (&p), proc (p), spec (p.getSpec()), look (juce::Colour (spec.accent))
{
    setLookAndFeel (&look);
    art = juce::ImageCache::getFromMemory (MawArtData::art_jpg, MawArtData::art_jpgSize);
    hero = proc.apvts.getParameter (spec.heroId);

    // fine film grain: unifies the art and hides softness from upscaling
    grain = juce::Image (juce::Image::ARGB, 192, 192, true);
    {
        juce::Random rnd (1234);
        for (int y = 0; y < 192; ++y)
            for (int x = 0; x < 192; ++x)
            {
                const auto v = (juce::uint8) rnd.nextInt (256);
                grain.setPixelAt (x, y, juce::Colour (v, v, v, (juce::uint8) 255));
            }
    }

    for (const auto& ps : spec.params)
    {
        switch (ps.kind)
        {
            case ParamSpec::Kind::Float:
            {
                Knob k;
                k.slider = std::make_unique<juce::Slider>();
                k.slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                k.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
                k.slider->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                               juce::MathConstants<float>::pi * 2.75f, true);
                addAndMakeVisible (*k.slider);
                k.label = std::make_unique<juce::Label>();
                k.label->setText (ps.name, juce::dontSendNotification);
                k.label->setJustificationType (juce::Justification::centred);
                k.label->setFont (Theme::font (13.0f, false, true));
                k.label->setInterceptsMouseClicks (false, false);
                addAndMakeVisible (*k.label);
                k.att = std::make_unique<SliderAtt> (proc.apvts, ps.id, *k.slider);
                knobs.push_back (std::move (k));
                break;
            }
            case ParamSpec::Kind::Choice:
            {
                Combo c;
                c.box = std::make_unique<juce::ComboBox>();
                c.box->addItemList (ps.choices, 1);
                c.box->setTooltip (ps.name);
                addAndMakeVisible (*c.box);
                c.att = std::make_unique<ComboAtt> (proc.apvts, ps.id, *c.box);
                combos.push_back (std::move (c));
                break;
            }
            case ParamSpec::Kind::Bool:
            {
                Toggle tg;
                tg.button = std::make_unique<juce::ToggleButton> (ps.name);
                addAndMakeVisible (*tg.button);
                tg.att = std::make_unique<ButtonAtt> (proc.apvts, ps.id, *tg.button);
                toggles.push_back (std::move (tg));
                break;
            }
        }
    }

    const auto atlas = juce::ImageCache::getFromMemory (MawArtData::butterflies_png, MawArtData::butterflies_pngSize);
    if (atlas.isValid())
        for (int i = 0; i < 6; ++i)
            sprites[(size_t) i] = atlas.getClippedImage ({ (i % 3) * kCell, (i / 3) * kCell, kCell, kCell });
    for (auto& f : flies)
    {
        f.flap = rng.nextFloat() * 6.28f;
        f.seed = rng.nextFloat() * 6.28f;
        f.rate = 0.8f + 0.4f * rng.nextFloat();
    }

    tongueImg = juce::ImageCache::getFromMemory (MawArtData::tongue_png, MawArtData::tongue_pngSize);

    motes.resize (spec.fx == PluginSpec::fxButterflies ? 5 : 16);
    for (auto& m : motes)
        spawnMote (m, true);
    lastTick = juce::Time::getMillisecondCounterHiRes();

    constrainer.setSizeLimits (520, 520, 1400, 1400);
    constrainer.setFixedAspectRatio (1.0);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize (designSize, designSize);
    startTimerHz (30);
}

MawEditor::~MawEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
    setConstrainer (nullptr);
}

void MawEditor::rebuildArtCache (int physicalSize)
{
    const float iw = (float) art.getWidth(), ih = (float) art.getHeight();
    artCrop = juce::Rectangle<int> (juce::roundToInt (spec.crop.getX() * iw), juce::roundToInt (spec.crop.getY() * ih),
                                    juce::roundToInt (spec.crop.getWidth() * iw), juce::roundToInt (spec.crop.getHeight() * ih))
                  .getIntersection (art.getBounds());

    // halve until close to the target, then finish with one smooth step: a single big downscale would alias the pencil texture
    auto img = art.getClippedImage (artCrop);
    while (img.getWidth() >= physicalSize * 2 && img.getHeight() >= physicalSize * 2)
        img = img.rescaled (img.getWidth() / 2, img.getHeight() / 2, juce::Graphics::highResamplingQuality);

    artCache = img.rescaled (physicalSize, physicalSize, juce::Graphics::highResamplingQuality);
    artCacheSize = physicalSize;
}

juce::Point<float> MawEditor::toWindow (juce::Point<float> p) const
{
    return { (float) sq.getX() + (p.x - spec.crop.getX()) / spec.crop.getWidth()  * (float) sq.getWidth(),
             (float) sq.getY() + (p.y - spec.crop.getY()) / spec.crop.getHeight() * (float) sq.getHeight() };
}

juce::Rectangle<float> MawEditor::toWindow (juce::Rectangle<float> r) const
{
    return juce::Rectangle<float> (toWindow (r.getTopLeft()), toWindow (r.getBottomRight()));
}

juce::Rectangle<float> MawEditor::heroWindowRect() const
{
    return toWindow (spec.heroRect).getIntersection (sq.toFloat());
}

void MawEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.reduceClipRegion (sq);
    g.setOrigin (sq.getPosition());
    const float W = (float) sq.getWidth(), H = (float) sq.getHeight();
    const auto off = sq.getPosition().toFloat();

    const float energy = energyNow();
    juce::AffineTransform artToLocal;   // art pixels -> this window, including the breathing motion

    if (art.isValid())
    {
        // The crop is scaled to the window's pixel size once (see rebuildArtCache), so each frame only moves it a little.
        const float pixelScale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int physical = juce::roundToInt (W * pixelScale);
        if (artCacheSize != physical)
            rebuildArtCache (physical);

        // slow breathing zoom + sway, livelier with the signal; the base zoom hides the edges while it moves
        const float zoom = 1.014f + 0.006f * std::sin (t * 0.45f) + 0.010f * level + 0.045f * squeezeSm;
        const auto motion = juce::AffineTransform::scale (zoom, zoom, W * 0.5f, H * 0.5f)
                                .translated (W * 0.004f * std::sin (t * 0.31f), H * 0.003f * std::cos (t * 0.27f));
        artToLocal = juce::AffineTransform::translation ((float) -artCrop.getX(), (float) -artCrop.getY())
                         .scaled (W / (float) artCrop.getWidth(), H / (float) artCrop.getHeight())
                         .followedBy (motion);

        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        g.drawImageTransformed (artCache, juce::AffineTransform::scale (1.0f / pixelScale).followedBy (motion));
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        if (tongueImg.isValid())   // the tongue is not part of art.jpg: it is drawn here, hanging still or flicking
        {
            const float angle = 0.025f * std::sin (t * 1.1f) + 0.16f * tongueFlick;
            const float stretch = 1.0f + 0.07f * tongueFlick;
            g.drawImageTransformed (tongueImg, juce::AffineTransform::translation (kTongueX - kPivotX, kTongueY - kPivotY)
                                                   .rotated (angle).scaled (stretch).translated (kPivotX, kPivotY)
                                                   .followedBy (artToLocal));
        }

        drawFlies (g, artToLocal, energy);
        if (spec.eye)      drawEye (g, artToLocal, proc.reaction.load());
        if (spec.sparkles) drawSparkles (g, artToLocal, juce::jlimit (0.0f, 1.0f, 0.25f + 0.75f * heroValue + energy));
    }

    g.setTiledImageFill (grain, 0, 0, 0.07f);
    g.fillRect (0.0f, 0.0f, W, H);

    // vignette + bottom scrim so controls stay readable on the busy art
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, W * 0.5f, H * 0.45f,
                                             juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true));
    g.fillRect (0.0f, 0.0f, W, H);
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::transparentBlack, H * 0.58f,
                                                       juce::Colour (0xee090507), H * 0.80f));
    g.fillRect (0.0f, H * 0.58f, W, H * 0.42f);

    // reactive glow
    const juce::Colour accent (spec.accent);
    const float flicker = 0.88f + 0.12f * std::sin (t * 2.3f);
    const float glow = juce::jlimit (0.0f, 1.0f, (heroValue * 0.45f + energy * 0.7f) * flicker);
    const auto gp = toWindow (spec.glowPos) - off;
    const float gr = spec.glowRadius / spec.crop.getWidth() * W;
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.55f * glow), gp.x, gp.y,
                                             accent.withAlpha (0.0f), gp.x + gr, gp.y, true));
    g.fillEllipse (gp.x - gr, gp.y - gr, gr * 2.0f, gr * 2.0f);

    if (spec.rings)   // rings leave the eye at the LFO rate
        for (int k = 0; k < 3; ++k)
        {
            const float ph = std::fmod (ringPhase + (float) k / 3.0f, 1.0f), r = gr * (0.15f + 1.1f * ph);
            g.setColour (accent.withAlpha (0.4f * (1.0f - ph) * (1.0f - ph)));
            g.drawEllipse (gp.x - r, gp.y - r, r * 2.0f, r * 2.0f, 1.6f);
        }

    const auto cream = juce::Colour (0xfff1e4d0);
    const float reaction = proc.reaction.load();

    if (spec.fog)      // mist rolls through the trees: thicker with Size and with the signal
    {
        const float amount = juce::jlimit (0.0f, 1.0f, 0.15f + 0.45f * heroValue + 0.5f * energy);
        for (int k = 0; k < 3; ++k)
        {
            const float cx = W * (std::fmod (t * (0.012f + 0.006f * (float) k) + 0.37f * (float) k, 1.5f) - 0.25f);
            const float cy = H * (0.40f + 0.13f * (float) k), r = W * (0.30f + 0.05f * (float) k);
            g.setGradientFill (juce::ColourGradient (accent.interpolatedWith (cream, 0.55f).withAlpha (0.20f * amount), cx, cy,
                                                     juce::Colours::transparentBlack, cx + r, cy, true));
            g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        }
    }

    if (spec.squeeze && squeezeSm > 0.01f)   // the frame closes in as the compressor works
    {
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, W * 0.5f, H * 0.45f,
                                                 juce::Colours::black.withAlpha (0.7f * squeezeSm), 0.0f, 0.0f, true));
        g.fillRect (0.0f, 0.0f, W, H);
    }

    if (spec.frost && reaction > 0.01f)      // frozen: ice creeps in from the corners
    {
        const auto ice = juce::Colour (0xffa8e0ff);
        g.setColour (ice.withAlpha (0.07f * reaction));
        g.fillRect (0.0f, 0.0f, W, H);
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, W * 0.5f, H * 0.5f,
                                                 ice.withAlpha (0.5f * reaction), 0.0f, 0.0f, true));
        g.fillRect (0.0f, 0.0f, W, H);
    }

    drawMotes (g, W, H, accent, energy);

    // title block
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.setFont (Theme::font (W * 0.05f, true, true));
    g.drawText (spec.name, juce::Rectangle<float> (W * 0.12f + 2.0f, H * 0.025f + 2.0f, W * 0.8f, H * 0.07f),
                juce::Justification::centredLeft);
    g.setColour (Theme::text);
    g.drawText (spec.name, juce::Rectangle<float> (W * 0.12f, H * 0.025f, W * 0.8f, H * 0.07f),
                juce::Justification::centredLeft);
    g.setColour (accent);
    g.setFont (Theme::font (W * 0.019f, true));
    g.drawText (spec.chapter.toUpperCase(), juce::Rectangle<float> (W * 0.12f, H * 0.088f, W * 0.8f, H * 0.03f),
                juce::Justification::centredLeft);

    // hamburger
    g.setColour (Theme::text.withAlpha (0.9f));
    for (int i = 0; i < 3; ++i)
        g.drawLine (W * 0.035f, H * 0.04f + (float) i * H * 0.014f, W * 0.085f, H * 0.04f + (float) i * H * 0.014f, 1.8f);

    // brand
    g.setColour (Theme::muted.withAlpha (0.8f));
    g.setFont (Theme::font (W * 0.017f, false, true));
    g.drawText ("skell audio  -  " + spec.tagline, juce::Rectangle<float> (0.0f, H * 0.965f, W, H * 0.03f),
                juce::Justification::centred);

    // hero hotspot hint + readout
    if ((hovering || dragging) && hero != nullptr)
    {
        auto rr = heroWindowRect().translated (-off.x, -off.y);
        g.setColour (accent.withAlpha (dragging ? 0.65f : 0.35f));
        g.drawRoundedRectangle (rr.reduced (2.0f), 14.0f, 1.6f);

        const juce::String txt = hero->getName (32) + "   " + hero->getCurrentValueAsText();
        auto pill = juce::Rectangle<float> (rr.getCentreX() - 95.0f, rr.getCentreY() - 14.0f, 190.0f, 28.0f);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.fillRoundedRectangle (pill, 14.0f);
        g.setColour (accent);
        g.drawRoundedRectangle (pill, 14.0f, 1.2f);
        g.setColour (Theme::text);
        g.setFont (Theme::font (14.0f, true));
        g.drawText (txt, pill.toNearestInt(), juce::Justification::centred);
    }
}

void MawEditor::resized()
{
    const int S = juce::jmin (getWidth(), getHeight());
    sq = juce::Rectangle<int> ((getWidth() - S) / 2, (getHeight() - S) / 2, S, S);

    // knob row: sits above the tagline so labels never collide with it
    const int n = (int) knobs.size();
    if (n > 0)
    {
        const int cell = juce::jmin ((int) ((float) S * 0.92f / (float) n), (int) ((float) S * 0.17f));
        const int sliderH = (int) ((float) cell * 1.12f);
        const int startX = sq.getX() + (S - cell * n) / 2;
        const int y = sq.getY() + (int) ((float) S * 0.952f) - 16 - sliderH;
        const int boxW = (int) ((float) cell * 0.80f);
        for (int i = 0; i < n; ++i)
        {
            auto& k = knobs[(size_t) i];
            k.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, boxW, 18);
            k.slider->setBounds (startX + i * cell, y, cell, sliderH);
            k.label->setBounds (startX + i * cell, y + sliderH - 2, cell, 18);
        }
    }

    // dropdowns and toggles stacked under the title
    int cy = sq.getY() + (int) ((float) S * 0.15f);
    const int cw = (int) ((float) S * 0.22f), ch = (int) ((float) S * 0.045f), gap = (int) ((float) S * 0.012f);
    const int cx = sq.getX() + (int) ((float) S * 0.03f);
    for (auto& c : combos)  { c.box->setBounds (cx, cy, cw, ch); cy += ch + gap; }
    for (auto& t : toggles) { t.button->setBounds (cx, cy, cw, ch); cy += ch + gap; }
}

void MawEditor::mouseMove (const juce::MouseEvent& e)
{
    const bool now = heroWindowRect().contains (e.position);
    if (now != hovering) { hovering = now; repaint(); }
}

void MawEditor::mouseExit (const juce::MouseEvent&)
{
    if (hovering) { hovering = false; repaint(); }
}

void MawEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto rel = e.position - sq.getPosition().toFloat();
    if (rel.x >= 0.0f && rel.y >= 0.0f && rel.x < (float) sq.getWidth() * 0.11f && rel.y < (float) sq.getHeight() * 0.11f)
    {
        popupMainMenu();
        return;
    }
    if (hero != nullptr && heroWindowRect().contains (e.position))
    {
        dragging = true;
        moved = false;
        heroStart = hero->getValue();
        hero->beginChangeGesture();
    }
}

void MawEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || hero == nullptr)
        return;

    const float dist = (spec.heroAxis == 0) ? (float) e.getDistanceFromDragStartX()
                                            : -(float) e.getDistanceFromDragStartY();
    if (std::abs (dist) > 3.0f)
        moved = true;
    if (! moved)
        return;

    const float scale = (float) sq.getWidth() * 0.35f * (e.mods.isShiftDown() ? 10.0f : 1.0f);
    hero->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, heroStart + (float) spec.heroSign * dist / scale));
}

void MawEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    dragging = false;
    hero->endChangeGesture();

    if (! moved && e.getNumberOfClicks() == 1 && spec.clickToggleId.isNotEmpty())
        if (auto* p = proc.apvts.getParameter (spec.clickToggleId))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
    repaint();
}

void MawEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (hero != nullptr && heroWindowRect().contains (e.position))
    {
        hero->beginChangeGesture();
        hero->setValueNotifyingHost (hero->getDefaultValue());
        hero->endChangeGesture();
    }
}

void MawEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    float dt = juce::jlimit (0.0f, 0.1f, (float) ((now - lastTick) * 0.001));
    lastTick = now;
    if (spec.frost)
        dt *= 1.0f - 0.97f * juce::jlimit (0.0f, 1.0f, proc.reaction.load());   // frozen: the picture almost stops
    t = std::fmod (t + dt, 1000.0f * juce::MathConstants<float>::twoPi);
    updateMotes (dt);
    const float energy = energyNow();
    if (spec.squeeze)
        squeezeSm += 0.2f * (juce::jlimit (0.0f, 1.0f, proc.reaction.load()) - squeezeSm);

    if (spec.tongue)   // a flick every few seconds, much more often when the signal is hot
    {
        flickT += dt;
        if (flickT > nextFlick)
        {
            flickT = 0.0f;
            nextFlick = juce::jmax (0.35f, 2.6f - 2.0f * energy + 1.4f * rng.nextFloat());
        }
        tongueFlick = flickT < 0.6f ? std::sin (flickT * 30.0f) * std::exp (-flickT * 7.0f) : 0.0f;
    }

    for (auto& f : flies)
    {
        f.flap += dt * f.rate * (7.0f + 18.0f * energy);
        f.path += dt * f.rate * (0.6f + 1.2f * energy);
    }
    if (spec.rings && hero != nullptr)
        ringPhase = std::fmod (ringPhase + dt * hero->convertFrom0to1 (heroValue) * 0.35f, 1.0f);

    level += 0.3f * (juce::jlimit (0.0f, 1.0f, proc.inputLevel.load()) - level);
    if (hero != nullptr)
        heroValue = hero->getValue();
    repaint();
}

void MawEditor::popupMainMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (spec.name.toUpperCase());
    m.addItem (1, "Reset controls");
    m.addSeparator();
    m.addItem (2, "About " + spec.name + " " + spec.version);

    juce::Component::SafePointer<MawEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [safe] (int result)
        {
            if (safe == nullptr) return;
            if (result == 1)
            {
                for (const auto& ps : safe->spec.params)
                    if (auto* p = safe->proc.apvts.getParameter (ps.id))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (p->getDefaultValue());
                        p->endChangeGesture();
                    }
            }
            else if (result == 2)
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                    safe->spec.name,
                    safe->spec.name + " " + safe->spec.version + "\n" + safe->spec.chapter
                        + "\nPart of The Girl & The Serpent collection.\nArt and design by Skell Audio.",
                    "OK", safe.getComponent());
            }
        });
}

void MawEditor::spawnMote (Mote& m, bool anywhere)
{
    const float pi = juce::MathConstants<float>::pi;
    m.size = rng.nextFloat();
    m.phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    m.x = rng.nextFloat();
    m.y = rng.nextFloat();
    switch (spec.fx)
    {
        case PluginSpec::fxEmbers:      m.a = -pi * 0.5f; m.v = 0.03f + 0.04f * m.size; if (! anywhere) m.y = 1.04f; break;
        case PluginSpec::fxPetals:
        case PluginSpec::fxFlecks:      m.a = pi * 0.5f;  m.v = 0.02f + 0.03f * m.size; if (! anywhere) m.y = -0.04f; break;
        case PluginSpec::fxButterflies: m.a = rng.nextFloat() * 2.0f * pi; m.v = 0.04f + 0.03f * m.size; m.sprite = rng.nextInt (6); break;
        default:                        m.a = rng.nextFloat() * 2.0f * pi; m.v = 0.012f + 0.014f * m.size; break;
    }
}

float MawEditor::energyNow() const
{
    return spec.frost ? level : juce::jmax (level, proc.reaction.load());   // for Shed Skin the reaction is the freeze, not loudness
}

void MawEditor::updateMotes (float dt)
{
    const float speed = 1.0f + 1.5f * energyNow();
    const bool wanders = spec.fx == PluginSpec::fxFireflies || spec.fx == PluginSpec::fxButterflies;

    for (auto& m : motes)
    {
        m.phase += dt * (1.2f + 2.0f * m.size);
        if (spec.fx == PluginSpec::fxButterflies)   m.a += std::sin (m.phase * 0.7f) * dt * 2.5f;
        else if (wanders)                           m.a += (rng.nextFloat() - 0.5f) * dt * 4.0f;

        const float sway = wanders ? 0.0f : 0.012f * std::sin (m.phase);   // petals, embers and flecks drift side to side
        m.x += (std::cos (m.a) * m.v + sway) * dt * speed;
        m.y += std::sin (m.a) * m.v * dt * speed;

        if (m.x < -0.06f || m.x > 1.06f || m.y < -0.06f || m.y > 1.06f)
        {
            if (wanders)   // turn back toward the middle
            {
                m.a = std::atan2 (0.5f - m.y, 0.5f - m.x) + (rng.nextFloat() - 0.5f);
                m.x = juce::jlimit (-0.05f, 1.05f, m.x);
                m.y = juce::jlimit (-0.05f, 1.05f, m.y);
            }
            else
                spawnMote (m, false);
        }
    }
}

void MawEditor::drawMotes (juce::Graphics& g, float W, float H, juce::Colour accent, float energy) const
{
    const auto cream = juce::Colour (0xfff1e4d0);
    for (const auto& m : motes)
    {
        const float px = m.x * W, py = m.y * H;
        const float twinkle = 0.5f + 0.5f * std::sin (m.phase * 2.0f);

        switch (spec.fx)
        {
            case PluginSpec::fxButterflies:   // extra butterflies, cut from the art, flying free across the whole window
            {
                if (! art.isValid())
                    break;
                const float k = W / (spec.crop.getWidth() * (float) art.getWidth()) * (0.42f + 0.3f * m.size);
                drawSprite (g, m.sprite, juce::AffineTransform::scale (k).translated (px, py), m.a, flapAmount (m.phase * (5.0f + 8.0f * energy)));
                break;
            }
            case PluginSpec::fxPetals:
            case PluginSpec::fxFlecks:
            {
                const float r = (0.007f + 0.007f * m.size) * W;
                juce::Path p;
                p.addEllipse (px - r, py - r * 0.5f, r * 2.0f, r);
                g.setColour ((spec.fx == PluginSpec::fxPetals ? accent.interpolatedWith (cream, 0.3f) : accent)
                                 .withAlpha (0.55f + 0.25f * twinkle));
                g.fillPath (p, juce::AffineTransform::rotation (m.phase, px, py));
                break;
            }
            default:   // fireflies and embers: soft glowing dots
            {
                const float r = (0.005f + 0.008f * m.size) * W * (1.0f + energy);
                const float a = (0.25f + 0.55f * twinkle) * (0.6f + 0.6f * energy);
                const auto c = accent.interpolatedWith (cream, 0.45f);
                g.setGradientFill (juce::ColourGradient (c.withAlpha (juce::jlimit (0.0f, 1.0f, a)), px, py,
                                                         c.withAlpha (0.0f), px + r * 2.5f, py, true));
                g.fillEllipse (px - r * 2.5f, py - r * 2.5f, r * 5.0f, r * 5.0f);
                break;
            }
        }
    }
}

void MawEditor::drawSprite (juce::Graphics& g, int sprite, const juce::AffineTransform& place, float heading, float flap) const
{
    const auto& img = sprites[(size_t) sprite];
    if (! img.isValid())
        return;

    // put the body along +x, squash across it to flap the wings, then turn the head to 'heading'
    const float c = (float) kCell * 0.5f;
    g.drawImageTransformed (img, juce::AffineTransform::translation (-c, -c)
                                     .rotated (-kSprites[sprite].head)
                                     .scaled (1.0f, flap)
                                     .rotated (heading)
                                     .followedBy (place));
}

void MawEditor::drawFlies (juce::Graphics& g, const juce::AffineTransform& artToLocal, float energy) const
{
    const float iw = (float) art.getWidth();
    const float reach = 1.0f + 0.8f * energy;                 // they roam further when the plugin is busy
    const float lag = 0.3f + 0.7f * heroValue;                // echo spacing follows the hero value (delay time)

    for (size_t i = 0; i < flies.size(); ++i)
    {
        const auto& f = flies[i];
        for (int e = spec.echoes; e >= 0; --e)                // oldest echo first, the real butterfly last, on top
        {
            const float path = f.path - (float) e * lag, flap = f.flap - (float) e * lag * 6.0f;
            const float x = kSprites[i].homeX * iw + reach * (0.019f * iw * std::sin (path * 0.9f + f.seed) + 0.007f * iw * std::sin (path * 2.1f));
            const float y = kSprites[i].homeY * iw + reach * (0.013f * iw * std::sin (path * 0.7f + f.seed * 1.7f));
            g.setOpacity (e == 0 ? 1.0f : 0.4f / (float) e);
            drawSprite (g, (int) i, juce::AffineTransform::translation (x, y).followedBy (artToLocal),
                        kSprites[i].head + 0.18f * std::sin (path * 1.3f + f.seed), flapAmount (flap));
        }
        g.setOpacity (1.0f);
    }
}

void MawEditor::drawEye (juce::Graphics& g, const juce::AffineTransform& artToLocal, float pulse) const
{
    const float s = artToLocal.getScaleFactor();
    const auto c  = juce::Point<float> (kPupilX, kPupilY).transformedBy (artToLocal);
    const auto ic = juce::Point<float> (kIrisX, kIrisY).transformedBy (artToLocal);
    const auto hl = juce::Point<float> (1740.0f, 654.0f).transformedBy (artToLocal);   // the painted highlight

    const auto orange = juce::Colour (0xffff9a3c);
    const float glowR = 120.0f * s;
    g.setGradientFill (juce::ColourGradient (orange.withAlpha (0.30f * pulse), ic.x, ic.y, orange.withAlpha (0.0f), ic.x + glowR, ic.y, true));
    g.fillEllipse (ic.x - glowR, ic.y - glowR, glowR * 2.0f, glowR * 2.0f);

    // the pupil opens beyond the painted one; it fades in so nothing shows while it is at rest
    const float r = (kPupilR + 20.0f * pulse) * s;
    const auto rim = juce::Colour (0xff263256);
    juce::ColourGradient pupil (juce::Colour (0xff0e1634), c.x, c.y, rim.withAlpha (0.0f), c.x + r, c.y, true);
    pupil.addColour (0.8, rim);
    g.setGradientFill (pupil);
    g.setOpacity (juce::jlimit (0.0f, 0.9f, pulse * 3.0f));   // a little see-through so the painted texture still shows
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

    g.setColour (juce::Colour (0xfffff3e0).withAlpha (0.95f * juce::jlimit (0.0f, 1.0f, pulse * 3.0f)));
    g.fillEllipse (hl.x - 10.0f * s, hl.y - 7.0f * s, 20.0f * s, 14.0f * s);
}

void MawEditor::drawSparkles (juce::Graphics& g, const juce::AffineTransform& artToLocal, float strength) const
{
    static const juce::Path star = []
    {
        juce::Path p;   // four-point star, unit radius
        p.startNewSubPath (0.0f, -1.0f);   p.lineTo (0.16f, -0.16f);
        p.lineTo (1.0f, 0.0f);             p.lineTo (0.16f, 0.16f);
        p.lineTo (0.0f, 1.0f);             p.lineTo (-0.16f, 0.16f);
        p.lineTo (-1.0f, 0.0f);            p.lineTo (-0.16f, -0.16f);
        p.closeSubPath();
        return p;
    }();

    for (int i = 0; i < 5; ++i)
    {
        const float tw = 0.5f + 0.5f * std::sin (t * (1.6f + 0.5f * (float) i) + (float) i * 2.1f);
        const float a = juce::jlimit (0.0f, 1.0f, strength * tw * tw);
        const float r = (18.0f + 22.0f * tw) * (0.6f + 0.6f * strength);
        const float x = kBlossoms[i][0], y = kBlossoms[i][1];

        const auto c = juce::Point<float> (x, y).transformedBy (artToLocal);
        const float gr = r * 1.6f * artToLocal.getScaleFactor();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffd9e8).withAlpha (0.45f * a), c.x, c.y,
                                                 juce::Colour (0xffffd9e8).withAlpha (0.0f), c.x + gr, c.y, true));
        g.fillEllipse (c.x - gr, c.y - gr, gr * 2.0f, gr * 2.0f);
        g.setColour (juce::Colour (0xfffff6fa).withAlpha (0.9f * a));
        g.fillPath (star, juce::AffineTransform::scale (r).rotated (0.3f * std::sin (t + (float) i)).translated (x, y).followedBy (artToLocal));
    }
}

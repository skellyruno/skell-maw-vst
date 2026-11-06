#include "MawEditor.h"
#include "MawArtData.h"

namespace { constexpr int designSize = 720; }

MawEditor::MawEditor (MawProcessorBase& p)
    : AudioProcessorEditor (&p), proc (p), spec (p.getSpec()), look (juce::Colour (spec.accent))
{
    setLookAndFeel (&look);
    art = juce::ImageCache::getFromMemory (MawArtData::art_jpg, MawArtData::art_jpgSize);
    hero = proc.apvts.getParameter (spec.heroId);

    for (const auto& ps : spec.params)
    {
        if (ps.id == spec.heroId)
            continue;

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
                Toggle t;
                t.button = std::make_unique<juce::ToggleButton> (ps.name);
                addAndMakeVisible (*t.button);
                t.att = std::make_unique<ButtonAtt> (proc.apvts, ps.id, *t.button);
                toggles.push_back (std::move (t));
                break;
            }
        }
    }

    constrainer.setSizeLimits (420, 420, 1200, 1200);
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

juce::Point<float> MawEditor::toWindow (juce::Point<float> p) const
{
    return { (p.x - spec.crop.getX()) / spec.crop.getWidth()  * (float) getWidth(),
             (p.y - spec.crop.getY()) / spec.crop.getHeight() * (float) getHeight() };
}

juce::Rectangle<float> MawEditor::toWindow (juce::Rectangle<float> r) const
{
    return juce::Rectangle<float> (toWindow (r.getTopLeft()), toWindow (r.getBottomRight()));
}

juce::Rectangle<float> MawEditor::heroWindowRect() const
{
    return toWindow (spec.heroRect).getIntersection (getLocalBounds().toFloat());
}

void MawEditor::paint (juce::Graphics& g)
{
    const float W = (float) getWidth(), H = (float) getHeight();
    g.fillAll (juce::Colours::black);

    if (art.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const int iw = art.getWidth(), ih = art.getHeight();
        g.drawImage (art, 0, 0, getWidth(), getHeight(),
                     juce::roundToInt (spec.crop.getX() * (float) iw), juce::roundToInt (spec.crop.getY() * (float) ih),
                     juce::roundToInt (spec.crop.getWidth() * (float) iw), juce::roundToInt (spec.crop.getHeight() * (float) ih));
    }

    // vignette + bottom scrim so controls stay readable on the busy art
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, W * 0.5f, H * 0.45f,
                                             juce::Colours::black.withAlpha (0.55f), 0.0f, 0.0f, true));
    g.fillRect (0.0f, 0.0f, W, H);
    g.setGradientFill (juce::ColourGradient::vertical (juce::Colours::transparentBlack, H * 0.58f,
                                                       juce::Colour (0xee090507), H * 0.80f));
    g.fillRect (0.0f, H * 0.58f, W, H * 0.42f);

    // reactive glow
    const juce::Colour accent (spec.accent);
    const float glow = juce::jlimit (0.0f, 1.0f, heroValue * 0.45f + juce::jmax (level, proc.reaction.load()) * 0.7f);
    const auto gp = toWindow (spec.glowPos);
    const float gr = spec.glowRadius / spec.crop.getWidth() * W;
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.55f * glow), gp.x, gp.y,
                                             accent.withAlpha (0.0f), gp.x + gr, gp.y, true));
    g.fillEllipse (gp.x - gr, gp.y - gr, gr * 2.0f, gr * 2.0f);

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
        auto rr = heroWindowRect();
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
    const int W = getWidth(), H = getHeight();

    // knob row
    const int n = (int) knobs.size();
    if (n > 0)
    {
        const int cell = juce::jmin ((int) ((float) W * 0.9f / (float) n), (int) ((float) W * 0.19f));
        const int sliderH = (int) ((float) cell * 1.12f);
        const int startX = (W - cell * n) / 2;
        const int y = (int) ((float) H * 0.74f);
        for (int i = 0; i < n; ++i)
        {
            knobs[(size_t) i].slider->setBounds (startX + i * cell, y, cell, sliderH);
            knobs[(size_t) i].label->setBounds (startX + i * cell, y + sliderH - 2, cell, 18);
        }
    }

    // dropdowns and toggles stacked under the title
    int cy = (int) ((float) H * 0.15f);
    const int cw = (int) ((float) W * 0.22f), ch = (int) ((float) H * 0.045f), gap = (int) ((float) H * 0.012f);
    const int cx = (int) ((float) W * 0.03f);
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
    if (e.position.x < (float) getWidth() * 0.11f && e.position.y < (float) getHeight() * 0.11f)
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

    const float scale = (float) getWidth() * 0.35f * (e.mods.isShiftDown() ? 10.0f : 1.0f);
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

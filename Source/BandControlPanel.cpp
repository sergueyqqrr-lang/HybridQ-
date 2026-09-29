#include "BandControlPanel.h"

//==============================================================================
BandControlPanel::BandControlPanel (HybridQAudioProcessor& processor)
    : processorRef (processor)
{
    bandTitleLabel.setJustificationType (juce::Justification::centredLeft);
    bandTitleLabel.setFont (juce::Font (16.0f, juce::Font::bold));
    bandTitleLabel.setColour (juce::Label::textColourId, juce::Colours::whitesmoke);
    addAndMakeVisible (bandTitleLabel);

    auto setupSlider = [this] (juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
        addAndMakeVisible (slider);

        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        label.setFont (juce::Font (11.0f));
        addAndMakeVisible (label);
    };

    setupSlider (freqSlider, freqLabel, "Freq");
    setupSlider (gainSlider, gainLabel, "Gain");
    setupSlider (qSlider,    qLabel,    "Q");

    freqSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff4fa8ff));
    gainSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffff9c40));
    qSlider.setColour    (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff6fd88f));

    auto setupCombo = [this] (juce::ComboBox& box, juce::Label& label, const juce::String& text,
                               const juce::StringArray& choices)
    {
        box.addItemList (choices, 1);
        addAndMakeVisible (box);

        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        label.setFont (juce::Font (11.0f));
        addAndMakeVisible (label);
    };

    setupCombo (typeBox, typeLabel, "Shape", getFilterTypeChoices());
    setupCombo (channelBox, channelLabel, "Channel", getChannelModeChoices());

    activeToggle.setColour (juce::ToggleButton::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (activeToggle);

    deleteButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a1e1e));
    deleteButton.onClick = [this]
    {
        if (currentBandIndex < 0)
            return;
        if (auto* activeParam = processorRef.apvts.getParameter (
                bandParamID (currentBandIndex, BandParamSuffix::active)))
            activeParam->setValueNotifyingHost (0.0f);
    };
    addAndMakeVisible (deleteButton);

    setSelectedBand (-1);
}

//==============================================================================
void BandControlPanel::setSelectedBand (int bandIndex)
{
    currentBandIndex = bandIndex;
    setVisible (bandIndex >= 0);

    if (bandIndex >= 0)
    {
        bandTitleLabel.setText ("Band " + juce::String (bandIndex + 1), juce::dontSendNotification);
        rebuildAttachments();
    }
    else
    {
        freqAttachment.reset();
        gainAttachment.reset();
        qAttachment.reset();
        typeAttachment.reset();
        channelAttachment.reset();
        activeAttachment.reset();
    }
}

void BandControlPanel::rebuildAttachments()
{
    jassert (currentBandIndex >= 0);

    // IMPORTANTE: destruir las attachments VIEJAS antes de construir las
    // nuevas, en vez de dejar que la asignación lo haga implícitamente.
    // `freqAttachment = std::make_unique<...>(...)` construye el objeto
    // nuevo PRIMERO (mientras el viejo sigue vivo y escuchando el mismo
    // slider compartido, ya que estos sliders se reutilizan para todas las
    // bandas) y solo DESPUÉS destruye el viejo. Como el constructor de la
    // attachment nueva llama a sendInitialUpdate() para poner el slider en
    // el valor de la banda recién seleccionada, esa notificación era
    // recibida todavía por la attachment vieja -- que la interpretaba como
    // una edición del usuario y la escribía en el parámetro de la banda
    // ANTERIOR. Esto corrompía silenciosamente (sobre todo) el Q de la
    // banda que se acababa de abandonar cada vez que se seleccionaba otra,
    // deformando su curva sin mover su punto (freq/gain quedaban intactos).
    freqAttachment.reset();
    gainAttachment.reset();
    qAttachment.reset();
    typeAttachment.reset();
    channelAttachment.reset();
    activeAttachment.reset();

    freqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, bandParamID (currentBandIndex, BandParamSuffix::freq), freqSlider);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, bandParamID (currentBandIndex, BandParamSuffix::gain), gainSlider);
    qAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, bandParamID (currentBandIndex, BandParamSuffix::q), qSlider);

    typeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.apvts, bandParamID (currentBandIndex, BandParamSuffix::type), typeBox);
    channelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processorRef.apvts, bandParamID (currentBandIndex, BandParamSuffix::channelMode), channelBox);

    activeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.apvts, bandParamID (currentBandIndex, BandParamSuffix::active), activeToggle);
}

//==============================================================================
void BandControlPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1f2226));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawLine (0, 0, (float) getWidth(), 0, 1.5f);
}

void BandControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced (12);

    bandTitleLabel.setBounds (bounds.removeFromLeft (90));

    auto makeKnobArea = [&bounds] (int width) { return bounds.removeFromLeft (width); };

    auto freqArea = makeKnobArea (70);
    freqLabel.setBounds (freqArea.removeFromBottom (14));
    freqSlider.setBounds (freqArea);

    bounds.removeFromLeft (8);
    auto gainArea = makeKnobArea (70);
    gainLabel.setBounds (gainArea.removeFromBottom (14));
    gainSlider.setBounds (gainArea);

    bounds.removeFromLeft (8);
    auto qArea = makeKnobArea (70);
    qLabel.setBounds (qArea.removeFromBottom (14));
    qSlider.setBounds (qArea);

    bounds.removeFromLeft (16);

    // --- Hueco reservado para Fase 2/3 -------------------------------------
    // Aquí es donde, según la sección 4.2 del brief, irán el selector
    // Full/Transient/Sustain (segmented control de 3 posiciones) y el
    // slider "Analog <-> Linear" de Phase Blend. Se dejan ~160px libres
    // para no tener que rehacer este layout cuando lleguen.
    bounds.removeFromLeft (160);
    bounds.removeFromLeft (16);
    // ------------------------------------------------------------------------

    auto typeArea = makeKnobArea (110);
    typeLabel.setBounds (typeArea.removeFromTop (14));
    typeBox.setBounds (typeArea.removeFromTop (24));

    bounds.removeFromLeft (8);
    auto channelArea = makeKnobArea (100);
    channelLabel.setBounds (channelArea.removeFromTop (14));
    channelBox.setBounds (channelArea.removeFromTop (24));

    bounds.removeFromLeft (16);
    activeToggle.setBounds (bounds.removeFromLeft (70).withSizeKeepingCentre (70, 24));

    deleteButton.setBounds (bounds.removeFromRight (110).withSizeKeepingCentre (100, 28));
}

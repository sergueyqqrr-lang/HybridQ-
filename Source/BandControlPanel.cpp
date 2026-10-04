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

    // Phase Blend: slider lineal horizontal (no rotatorio como los knobs)
    // con etiquetas de texto en los extremos en vez de valores numéricos --
    // "más intuitivo para el usuario no técnico, sin perder precisión"
    // (sección 4.2). El valor exacto en % sigue visible en el text box.
    phaseBlendSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    phaseBlendSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 18);
    phaseBlendSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    phaseBlendSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff8a6fd8));
    addAndMakeVisible (phaseBlendSlider);

    phaseBlendTitleLabel.setText ("Phase Blend", juce::dontSendNotification);
    phaseBlendTitleLabel.setJustificationType (juce::Justification::centred);
    phaseBlendTitleLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    phaseBlendTitleLabel.setFont (juce::Font (11.0f));
    addAndMakeVisible (phaseBlendTitleLabel);

    phaseBlendAnalogLabel.setText ("Analog", juce::dontSendNotification);
    phaseBlendAnalogLabel.setJustificationType (juce::Justification::centredLeft);
    phaseBlendAnalogLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    phaseBlendAnalogLabel.setFont (juce::Font (10.0f));
    addAndMakeVisible (phaseBlendAnalogLabel);

    phaseBlendLinearLabel.setText ("Linear", juce::dontSendNotification);
    phaseBlendLinearLabel.setJustificationType (juce::Justification::centredRight);
    phaseBlendLinearLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    phaseBlendLinearLabel.setFont (juce::Font (10.0f));
    addAndMakeVisible (phaseBlendLinearLabel);

    // Full/Transient/Sustain: segmented control de 3 botones con radio-group
    // nativo de JUCE (setRadioGroupId hace que solo uno quede "presionado"
    // a la vez y des-presiona los otros automáticamente). Colores acordes a
    // la codificación de la sección 4.2: naranja=Transient, azul=Sustain,
    // neutro=Full.
    auto setupSplitButton = [this] (juce::TextButton& button, juce::Colour onColour)
    {
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1001, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonOnColourId, onColour);
        addAndMakeVisible (button);
    };

    setupSplitButton (splitFullButton,      juce::Colours::grey);
    setupSplitButton (splitTransientButton, juce::Colour (0xffff9c40));
    setupSplitButton (splitSustainButton,   juce::Colour (0xff4fa8ff));

    splitFullButton.onClick      = [this] { setSplitMode (SplitMode::Full); };
    splitTransientButton.onClick = [this] { setSplitMode (SplitMode::Transient); };
    splitSustainButton.onClick   = [this] { setSplitMode (SplitMode::Sustain); };

    splitModeTitleLabel.setText ("Split", juce::dontSendNotification);
    splitModeTitleLabel.setJustificationType (juce::Justification::centred);
    splitModeTitleLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    splitModeTitleLabel.setFont (juce::Font (11.0f));
    addAndMakeVisible (splitModeTitleLabel);

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

    // Refresco ligero para reflejar cambios de splitMode hechos por
    // automatización del host mientras el panel está abierto (el selector
    // no usa un Attachment estándar que haga esto solo -- ver comentario
    // de la clase en el header).
    startTimerHz (15);
}

BandControlPanel::~BandControlPanel()
{
    stopTimer();
}

//==============================================================================
void BandControlPanel::setSplitMode (SplitMode mode)
{
    if (currentBandIndex < 0)
        return;

    if (auto* param = processorRef.apvts.getParameter (
            bandParamID (currentBandIndex, BandParamSuffix::splitMode)))
        param->setValueNotifyingHost (param->convertTo0to1 ((float) static_cast<int> (mode)));
}

void BandControlPanel::refreshSplitModeButtons()
{
    if (currentBandIndex < 0)
        return;

    const auto mode = static_cast<SplitMode> ((int) processorRef.apvts.getRawParameterValue (
        bandParamID (currentBandIndex, BandParamSuffix::splitMode))->load());

    splitFullButton.setToggleState      (mode == SplitMode::Full,      juce::dontSendNotification);
    splitTransientButton.setToggleState (mode == SplitMode::Transient, juce::dontSendNotification);
    splitSustainButton.setToggleState   (mode == SplitMode::Sustain,   juce::dontSendNotification);
}

void BandControlPanel::timerCallback()
{
    refreshSplitModeButtons();
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
        refreshSplitModeButtons();
    }
    else
    {
        freqAttachment.reset();
        gainAttachment.reset();
        qAttachment.reset();
        phaseBlendAttachment.reset();
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
    phaseBlendAttachment.reset();
    typeAttachment.reset();
    channelAttachment.reset();
    activeAttachment.reset();

    freqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, bandParamID (currentBandIndex, BandParamSuffix::freq), freqSlider);
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, bandParamID (currentBandIndex, BandParamSuffix::gain), gainSlider);
    qAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, bandParamID (currentBandIndex, BandParamSuffix::q), qSlider);
    phaseBlendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, bandParamID (currentBandIndex, BandParamSuffix::phaseBlend), phaseBlendSlider);

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

    // Phase Blend (Fase 2).
    auto phaseBlendArea = makeKnobArea (150);
    phaseBlendTitleLabel.setBounds (phaseBlendArea.removeFromTop (14));
    auto phaseBlendRow = phaseBlendArea.removeFromTop (24);
    phaseBlendAnalogLabel.setBounds (phaseBlendRow.removeFromLeft (45));
    phaseBlendLinearLabel.setBounds (phaseBlendRow.removeFromRight (45));
    phaseBlendSlider.setBounds (phaseBlendRow);

    bounds.removeFromLeft (16);

    // Full/Transient/Sustain -- segmented control de 3 botones (sección 4.2).
    auto splitModeArea = makeKnobArea (108);
    splitModeTitleLabel.setBounds (splitModeArea.removeFromTop (14));
    auto splitButtonsRow = splitModeArea.removeFromTop (24);
    const auto splitButtonWidth = splitButtonsRow.getWidth() / 3;
    splitFullButton.setBounds      (splitButtonsRow.removeFromLeft (splitButtonWidth));
    splitTransientButton.setBounds (splitButtonsRow.removeFromLeft (splitButtonWidth));
    splitSustainButton.setBounds   (splitButtonsRow);

    bounds.removeFromLeft (16);

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

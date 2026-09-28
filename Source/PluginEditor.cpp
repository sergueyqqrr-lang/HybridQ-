#include "PluginEditor.h"

//==============================================================================
HybridQAudioProcessorEditor::HybridQAudioProcessorEditor (HybridQAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p),
      spectrumDisplay (p), bandControlPanel (p)
{
    titleLabel.setText ("HybridQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::whitesmoke);
    addAndMakeVisible (titleLabel);

    masterGainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    masterGainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 55, 20);
    masterGainSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible (masterGainSlider);

    masterGainLabel.setText ("Output", juce::dontSendNotification);
    masterGainLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    masterGainLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (masterGainLabel);

    masterGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, HybridQAudioProcessor::masterGainParamID, masterGainSlider);

    saveButton.onClick = [this] { savePreset(); };
    loadButton.onClick = [this] { loadPreset(); };
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);

    addAndMakeVisible (spectrumDisplay);
    spectrumDisplay.onBandSelected = [this] (int bandIndex)
    {
        // IMPORTANTE: no reacomodar el layout de forma síncrona dentro del
        // propio evento de mouseDown del handle que disparó esto -- eso
        // reposiciona (entre otras cosas) al mismo componente que todavía
        // está procesando su click, un patrón de reentrancia clásico en
        // JUCE que puede hacer que el click "se pierda" (el panel nunca
        // llega a mostrarse, o el arrastre que sigue no engancha bien).
        // Se difiere al siguiente ciclo del message loop, ya con el evento
        // de mouseDown completamente terminado.
        juce::MessageManager::callAsync ([this, bandIndex]
        {
            bandControlPanel.setSelectedBand (bandIndex);
            resized();
        });
    };

    addAndMakeVisible (bandControlPanel);

    setResizable (true, true);
    setResizeLimits (700, 480, 2000, 1300);
    setSize (980, 640);
}

HybridQAudioProcessorEditor::~HybridQAudioProcessorEditor() = default;

//==============================================================================
void HybridQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1c1f));
}

void HybridQAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    auto topBar = bounds.removeFromTop (44).reduced (12, 6);
    titleLabel.setBounds (topBar.removeFromLeft (100));

    loadButton.setBounds (topBar.removeFromRight (70));
    topBar.removeFromRight (8);
    saveButton.setBounds (topBar.removeFromRight (70));
    topBar.removeFromRight (16);

    masterGainLabel.setBounds (topBar.removeFromLeft (60));
    masterGainSlider.setBounds (topBar.removeFromLeft (180));

    // El panel de banda solo ocupa espacio cuando hay una banda seleccionada
    // (BandControlPanel::setVisible se controla desde setSelectedBand).
    if (bandControlPanel.isVisible())
        bandControlPanel.setBounds (bounds.removeFromBottom (100));

    spectrumDisplay.setBounds (bounds.reduced (8));
}

//==============================================================================
void HybridQAudioProcessorEditor::savePreset()
{
    // Fase 1: save/load básico a un archivo .hybridq (XML del APVTS). Un
    // PresetManager con navegador de presets/categorías queda para el
    // pulido de fases posteriores -- esto cumple el criterio de "Presets
    // (guardar/cargar)" de la sección 2.4 al nivel de MVP.
    activeFileChooser = std::make_unique<juce::FileChooser> (
        "Save HybridQ preset", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.hybridq");

    activeFileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                         | juce::FileBrowserComponent::canSelectFiles
                                         | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File {})
                return;

            if (auto state = processorRef.apvts.copyState(); state.isValid())
                if (auto xml = state.createXml())
                    xml->writeTo (file);
        });
}

void HybridQAudioProcessorEditor::loadPreset()
{
    activeFileChooser = std::make_unique<juce::FileChooser> (
        "Load HybridQ preset", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.hybridq");

    activeFileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                         | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File {})
                return;

            if (auto xml = juce::XmlDocument::parse (file))
                if (xml->hasTagName (processorRef.apvts.state.getType()))
                    processorRef.apvts.replaceState (juce::ValueTree::fromXml (*xml));

            bandControlPanel.setSelectedBand (-1);
            resized();
        });
}

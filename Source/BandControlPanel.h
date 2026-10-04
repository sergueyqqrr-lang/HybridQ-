#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "BandModel.h"

//==============================================================================
/**
    BandControlPanel

    Panel contextual descrito en la sección 4.1/4.2. Desde la Fase 2 incluye
    el slider "Analog <-> Linear" de Phase Blend. Desde la Fase 3 incluye el
    selector Full/Transient/Sustain (segmented control de 3 botones con
    radio-group nativo de JUCE, no un dropdown -- sección 4.2: "debe
    sentirse tan rápido de cambiar como el shape selector").

    El selector no usa un AudioProcessorValueTreeState::Attachment estándar
    (JUCE no trae uno para grupos de botones) -- se sincroniza manualmente:
    escritura directa al parámetro en cada click, y un Timer ligero que
    relee el valor actual para reflejar cambios externos (automatización
    del host) mientras el panel está abierto.
*/
class BandControlPanel : public juce::Component,
                          private juce::Timer
{
public:
    explicit BandControlPanel (HybridQAudioProcessor& processor);
    ~BandControlPanel() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Cambia qué banda muestra el panel; -1 = ninguna (panel oculto). */
    void setSelectedBand (int bandIndex);

private:
    void rebuildAttachments();
    void refreshSplitModeButtons();
    void setSplitMode (SplitMode mode);
    void timerCallback() override;

    HybridQAudioProcessor& processorRef;
    int currentBandIndex = -1;

    juce::Label bandTitleLabel;

    juce::Slider freqSlider, gainSlider, qSlider;
    juce::Label  freqLabel, gainLabel, qLabel;

    // Phase Blend: slider horizontal con etiquetas "Analog"/"Linear" en vez
    // de 0%/100% (sección 4.2 -- más intuitivo para quien no es técnico,
    // sin perder precisión; el valor exacto se ve en el text box del propio
    // slider al hacer click).
    juce::Slider phaseBlendSlider;
    juce::Label  phaseBlendTitleLabel, phaseBlendAnalogLabel, phaseBlendLinearLabel;

    // Full/Transient/Sustain -- segmented control de 3 botones con
    // radio-group nativo (ver nota de la clase sobre por qué no hay
    // Attachment estándar aquí).
    juce::TextButton splitFullButton { "Full" }, splitTransientButton { "Trans" }, splitSustainButton { "Sustain" };
    juce::Label splitModeTitleLabel;

    juce::ComboBox typeBox, channelBox;
    juce::Label typeLabel, channelLabel;

    juce::ToggleButton activeToggle { "Active" };
    juce::TextButton deleteButton { "Delete Band" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAttachment, gainAttachment, qAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> phaseBlendAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment, channelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> activeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandControlPanel)
};

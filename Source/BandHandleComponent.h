#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "BandModel.h"

//==============================================================================
/**
    BandHandleComponent

    Un círculo arrastrable sobre el SpectrumDisplayComponent, ligado a una
    banda concreta. Arrastrar horizontalmente cambia frecuencia (escala
    log), verticalmente cambia gain (dB, salvo en Low/High Cut donde el
    gain no aplica y solo se mueve la frecuencia). Rueda del ratón sobre el
    handle ajusta Q. Click lo selecciona (abre el panel contextual).

    La codificación visual (color por tipo de canal) es la base sobre la
    que desde Fase 3 incluye también la anilla partida Transient/Sustain descrita
    en la sección 4.2 del brief -- aquí todavía no existe esa distinción.
*/
class BandHandleComponent : public juce::Component
{
public:
    BandHandleComponent (HybridQAudioProcessor& processor, int bandIndex);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Reposiciona el handle dentro de `parentBounds` según los valores
        actuales de freq/gain de su banda. Llamado desde el timer del
        display padre en cada refresco. */
    void updatePosition (juce::Rectangle<int> parentBounds);

    int getBandIndex() const { return bandIndex; }

    std::function<void (int)> onSelected;

private:
    juce::Colour getColourForChannelMode() const;

    HybridQAudioProcessor& processorRef;
    const int bandIndex;

    std::unique_ptr<juce::ParameterAttachment> freqAttachment;
    std::unique_ptr<juce::ParameterAttachment> gainAttachment;
    std::unique_ptr<juce::ParameterAttachment> qAttachment;

    static constexpr int handleDiameter = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandHandleComponent)
};

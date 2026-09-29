#include "BandHandleComponent.h"
#include "EQCoordinates.h"

//==============================================================================
BandHandleComponent::BandHandleComponent (HybridQAudioProcessor& processor, int bandIdx)
    : processorRef (processor), bandIndex (bandIdx)
{
    setSize (handleDiameter, handleDiameter);

    auto makeAttachment = [this] (const juce::String& suffix)
    {
        auto* param = processorRef.apvts.getParameter (bandParamID (bandIndex, suffix));
        jassert (param != nullptr);
        return std::make_unique<juce::ParameterAttachment> (
            *param, [] (float) {}, nullptr);
    };

    freqAttachment = makeAttachment (BandParamSuffix::freq);
    gainAttachment = makeAttachment (BandParamSuffix::gain);
    qAttachment    = makeAttachment (BandParamSuffix::q);
}

//==============================================================================
juce::Colour BandHandleComponent::getColourForChannelMode() const
{
    const auto modeValue = processorRef.apvts.getRawParameterValue (
        bandParamID (bandIndex, BandParamSuffix::channelMode))->load();
    const auto mode = static_cast<ChannelMode> ((int) modeValue);

    switch (mode)
    {
        case ChannelMode::Mid:   return juce::Colour (0xff4fa8ff); // azul frío
        case ChannelMode::Side:  return juce::Colour (0xffff9c40); // naranja cálido
        case ChannelMode::Left:  return juce::Colour (0xff6fd88f);
        case ChannelMode::Right: return juce::Colour (0xffd86fd0);
        default:                 return juce::Colours::whitesmoke; // Stereo
    }
}

void BandHandleComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto colour = getColourForChannelMode();

    g.setColour (colour.withAlpha (0.85f));
    g.drawEllipse (bounds, 2.0f);

    g.setColour (colour.withAlpha (0.18f));
    g.fillEllipse (bounds);

    // --- DIAGNÓSTICO TEMPORAL --------------------------------------------
    if (isBeingClicked)
    {
        g.setColour (juce::Colours::red);
        g.drawEllipse (bounds.expanded (4.0f), 3.0f);
    }
    // ----------------------------------------------------------------------

    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText (juce::String (bandIndex + 1), getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
void BandHandleComponent::mouseDown (const juce::MouseEvent&)
{
    isBeingClicked = true;
    repaint();

    if (onSelected != nullptr)
        onSelected (bandIndex);

    freqAttachment->beginGesture();
    gainAttachment->beginGesture();
}

void BandHandleComponent::mouseDrag (const juce::MouseEvent& event)
{
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;

    const auto posInParent = event.getEventRelativeTo (parent).position;
    const auto bounds = parent->getLocalBounds().toFloat();

    const auto newFreq = EQCoordinates::xToFreq (posInParent.x, bounds.getWidth());
    const auto newGainDb = EQCoordinates::yToDb (posInParent.y, bounds.getHeight());

    // IMPORTANTE: juce::ParameterAttachment::setValueAsPartOfGesture espera
    // el valor REAL (Hz, dB), no normalizado -- la normalización la hace
    // internamente. Pasarle un valor ya convertido con convertTo0to1() lo
    // normaliza dos veces, colapsando cualquier frecuencia a un valor
    // pegado al extremo inferior del rango (este era exactamente el bug
    // por el que el punto "saltaba al principio" al arrastrar).
    freqAttachment->setValueAsPartOfGesture (newFreq);
    gainAttachment->setValueAsPartOfGesture (newGainDb);

    updatePosition (parent->getLocalBounds());
}

void BandHandleComponent::mouseUp (const juce::MouseEvent&)
{
    isBeingClicked = false;
    repaint();

    freqAttachment->endGesture();
    gainAttachment->endGesture();
}

void BandHandleComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto currentQ = processorRef.apvts.getRawParameterValue (
        bandParamID (bandIndex, BandParamSuffix::q))->load();

    // Paso multiplicativo (no aditivo) para que el ajuste se sienta igual
    // de fino en Q bajos que en Q altos.
    const auto factor = std::pow (1.05f, wheel.deltaY * 10.0f);
    const auto newQ = juce::jlimit (0.1f, 18.0f, currentQ * factor);

    // Valor real (no normalizado) -- ver nota en mouseDrag().
    qAttachment->setValueAsCompleteGesture (newQ);
}

void BandHandleComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    // Reset rápido de gain a 0 dB -- gesto estándar en EQs tipo Pro-Q.
    // Valor real (no normalizado) -- ver nota en mouseDrag().
    gainAttachment->setValueAsCompleteGesture (0.0f);
}

//==============================================================================
void BandHandleComponent::updatePosition (juce::Rectangle<int> parentBounds)
{
    const auto freq = processorRef.apvts.getRawParameterValue (
        bandParamID (bandIndex, BandParamSuffix::freq))->load();
    const auto gainDb = processorRef.apvts.getRawParameterValue (
        bandParamID (bandIndex, BandParamSuffix::gain))->load();

    const auto x = EQCoordinates::freqToX (freq, (float) parentBounds.getWidth());
    const auto y = EQCoordinates::dbToY (gainDb, (float) parentBounds.getHeight());

    setCentrePosition ((int) x, (int) y);
}

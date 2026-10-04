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

    // Codificación visual pasiva del Phase Blend (sección 4.2): un halo
    // alrededor del handle cuya opacidad crece con el blend -- un handle
    // "sólido" (sin halo) es 100% minimum-phase; un halo marcado indica
    // blend alto hacia linear-phase. Da información de un vistazo sin
    // necesitar abrir el panel de banda.
    const auto blend = processorRef.apvts.getRawParameterValue (
        bandParamID (bandIndex, BandParamSuffix::phaseBlend))->load();
    if (blend > 0.01f)
    {
        g.setColour (juce::Colour (0xff8a6fd8).withAlpha (0.12f + 0.35f * blend));
        g.drawEllipse (bounds.expanded (3.0f + 3.0f * blend), 2.0f + 2.0f * blend);
    }

    // Codificación visual pasiva del Split Mode (sección 4.2): "anilla
    // partida" -- medio arco naranja arriba = Transient, medio arco azul
    // abajo = Sustain. En modo Full no se dibuja nada extra (anilla
    // completa normal de abajo, sin partir).
    const auto splitMode = static_cast<SplitMode> ((int) processorRef.apvts.getRawParameterValue (
        bandParamID (bandIndex, BandParamSuffix::splitMode))->load());
    if (splitMode != SplitMode::Full)
    {
        const auto isTransient = (splitMode == SplitMode::Transient);
        const auto splitColour = isTransient ? juce::Colour (0xffff9c40) : juce::Colour (0xff4fa8ff);

        // Convención de ángulos de JUCE: 0 rad = 12 en punto, sentido horario.
        // Transient: arco superior, de -90° (9 en punto) a +90° (3 en punto)
        // pasando por arriba. Sustain: arco inferior, de +90° a +270°
        // pasando por abajo.
        const auto half = juce::MathConstants<float>::halfPi;
        const auto fromAngle = isTransient ? -half : half;
        const auto toAngle   = isTransient ?  half : half * 3.0f;

        const auto arcBounds = bounds.expanded (2.5f);
        juce::Path arcPath;
        arcPath.addCentredArc (arcBounds.getCentreX(), arcBounds.getCentreY(),
                                arcBounds.getWidth() * 0.5f, arcBounds.getHeight() * 0.5f,
                                0.0f, fromAngle, toAngle, true);
        g.setColour (splitColour);
        g.strokePath (arcPath, juce::PathStrokeType (3.0f));
    }

    g.setColour (colour.withAlpha (0.85f));
    g.drawEllipse (bounds, 2.0f);

    g.setColour (colour.withAlpha (0.18f));
    g.fillEllipse (bounds);

    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.setFont (juce::Font (10.0f, juce::Font::bold));
    g.drawText (juce::String (bandIndex + 1), getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
void BandHandleComponent::mouseDown (const juce::MouseEvent&)
{
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

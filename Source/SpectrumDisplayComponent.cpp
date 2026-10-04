#include "SpectrumDisplayComponent.h"
#include "EQCoordinates.h"
#include <algorithm>

//==============================================================================
SpectrumDisplayComponent::SpectrumDisplayComponent (HybridQAudioProcessor& processor)
    : processorRef (processor)
{
    lastKnownActiveState.fill (false);
    startTimerHz (30);
}

SpectrumDisplayComponent::~SpectrumDisplayComponent()
{
    stopTimer();
}

//==============================================================================
void SpectrumDisplayComponent::timerCallback()
{
    rebuildHandlesIfNeeded();

    if (processorRef.nextFFTBlockReady.load())
    {
        std::copy (processorRef.fftData.begin(), processorRef.fftData.end(), fftWorkspace.begin());
        processorRef.nextFFTBlockReady = false;

        window.multiplyWithWindowingTable (fftWorkspace.data(), (size_t) HybridQAudioProcessor::fftSize);
        fft.performFrequencyOnlyForwardTransform (fftWorkspace.data());

        constexpr float minVisibleDb = -80.0f;
        constexpr float maxVisibleDb = 0.0f;
        // Normalización aproximada por el tamaño de la FFT (no es una
        // calibración SPL exacta -- es un display relativo, no un medidor).
        const auto fftSizeCorrectionDb = juce::Decibels::gainToDecibels ((float) HybridQAudioProcessor::fftSize);

        for (size_t i = 0; i < scopeNormalized.size(); ++i)
        {
            const auto db = juce::Decibels::gainToDecibels (fftWorkspace[i], minVisibleDb) - fftSizeCorrectionDb;
            scopeNormalized[i] = juce::jmap (juce::jlimit (minVisibleDb, maxVisibleDb, db),
                                              minVisibleDb, maxVisibleDb, 0.0f, 1.0f);
        }
        haveSpectrumData = true;
    }

    repaint();
}

//==============================================================================
void SpectrumDisplayComponent::rebuildHandlesIfNeeded()
{
    // Actualiza solo lo que cambió: antes esta función destruía y volvía a
    // crear TODOS los handles cada vez que CUALQUIER banda cambiaba de
    // activa/inactiva -- si eso ocurría mientras el usuario estaba a mitad
    // de un arrastre sobre un handle completamente distinto, ese handle
    // podía ser destruido en medio de su propio gesto. Ahora solo se toca
    // el handle de la banda que realmente cambió.
    for (int i = 0; i < maxBands; ++i)
    {
        const bool active = processorRef.isBandActive (i);
        if (active == lastKnownActiveState[(size_t) i])
            continue;

        lastKnownActiveState[(size_t) i] = active;

        if (active)
        {
            auto handle = std::make_unique<BandHandleComponent> (processorRef, i);
            handle->onSelected = [this] (int bandIndex)
            {
                if (onBandSelected != nullptr)
                    onBandSelected (bandIndex);
            };
            addAndMakeVisible (*handle);
            handle->updatePosition (getLocalBounds());
            handles.push_back (std::move (handle));
        }
        else
        {
            handles.erase (std::remove_if (handles.begin(), handles.end(),
                                            [i] (const std::unique_ptr<BandHandleComponent>& h)
                                            { return h->getBandIndex() == i; }),
                           handles.end());
        }
    }
}

//==============================================================================
void SpectrumDisplayComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (juce::Colour (0xff121316));

    drawGrid (g, bounds);
    if (haveSpectrumData)
        drawSpectrum (g, bounds);
    drawEqCurve (g, bounds);
}

void SpectrumDisplayComponent::resized()
{
    for (auto& handle : handles)
        handle->updatePosition (getLocalBounds());
}

void SpectrumDisplayComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    // Doble click en zona vacía del display (no sobre un handle, que
    // intercepta su propio mouseDoubleClick) crea una banda nueva: activa
    // la primera banda inactiva disponible en la frecuencia/gain del click.
    const auto bounds = getLocalBounds().toFloat();
    const auto freq = EQCoordinates::xToFreq ((float) event.position.x, bounds.getWidth());
    const auto gainDb = EQCoordinates::yToDb ((float) event.position.y, bounds.getHeight());

    for (int i = 0; i < maxBands; ++i)
    {
        if (processorRef.isBandActive (i))
            continue;

        if (auto* freqParam = processorRef.apvts.getParameter (bandParamID (i, BandParamSuffix::freq)))
            freqParam->setValueNotifyingHost (
                dynamic_cast<juce::RangedAudioParameter*> (freqParam)->convertTo0to1 (freq));

        if (auto* gainParam = processorRef.apvts.getParameter (bandParamID (i, BandParamSuffix::gain)))
            gainParam->setValueNotifyingHost (
                dynamic_cast<juce::RangedAudioParameter*> (gainParam)->convertTo0to1 (gainDb));

        if (auto* activeParam = processorRef.apvts.getParameter (bandParamID (i, BandParamSuffix::active)))
            activeParam->setValueNotifyingHost (1.0f);

        if (onBandSelected != nullptr)
            onBandSelected (i);

        break;
    }
}

//==============================================================================
void SpectrumDisplayComponent::drawGrid (juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    g.setColour (juce::Colours::white.withAlpha (0.08f));

    const float freqLines[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (auto freq : freqLines)
    {
        const auto x = EQCoordinates::freqToX (freq, bounds.getWidth());
        g.drawVerticalLine ((int) x, bounds.getY(), bounds.getBottom());
    }

    const float dbLines[] = { -24, -12, 0, 12, 24 };
    for (auto db : dbLines)
    {
        const auto y = EQCoordinates::dbToY (db, bounds.getHeight());
        g.setColour (db == 0.0f ? juce::Colours::white.withAlpha (0.18f)
                                 : juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) y, bounds.getX(), bounds.getRight());
    }
}

void SpectrumDisplayComponent::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    juce::Path path;
    const auto sampleRate = processorRef.getSampleRate() > 0.0 ? processorRef.getSampleRate() : 44100.0;
    bool started = false;

    for (size_t i = 1; i < scopeNormalized.size(); ++i)
    {
        const auto freq = (float) (i * sampleRate / (double) HybridQAudioProcessor::fftSize);
        if (freq < EQCoordinates::minFreq || freq > EQCoordinates::maxFreq)
            continue;

        const auto x = EQCoordinates::freqToX (freq, bounds.getWidth());
        const auto y = bounds.getHeight() * (1.0f - scopeNormalized[i]);

        if (! started)
        {
            path.startNewSubPath (x, bounds.getHeight());
            path.lineTo (x, y);
            started = true;
        }
        else
        {
            path.lineTo (x, y);
        }
    }
    path.lineTo (bounds.getWidth(), bounds.getHeight());
    path.closeSubPath();

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillPath (path);
}

void SpectrumDisplayComponent::drawEqCurve (juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    juce::Path path;
    constexpr int step = 2; // px -- suficiente resolución sin recalcular por sample
    bool started = false;

    for (float x = 0.0f; x <= bounds.getWidth(); x += (float) step)
    {
        const auto freq = EQCoordinates::xToFreq (x, bounds.getWidth());
        const auto db = processorRef.getFrequencyResponseDb (freq);
        const auto y = EQCoordinates::dbToY (db, bounds.getHeight());

        if (! started) { path.startNewSubPath (x, y); started = true; }
        else            { path.lineTo (x, y); }
    }

    g.setColour (juce::Colour (0xfff5f0e6));
    g.strokePath (path, juce::PathStrokeType (2.0f));
}

// --- DIAGNÓSTICO TEMPORAL (retirado tras confirmar el fix) -------------
// La implementación de drawDebugBandValues() se deja fuera pero el método
// ya no se declara ni se llama; si hace falta reactivarla, revisar el
// historial de este archivo.
// -------------------------------------------------------------------------

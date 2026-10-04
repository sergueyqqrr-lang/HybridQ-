#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
HybridQAudioProcessor::HybridQAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (int i = 0; i < maxBands; ++i)
    {
        auto& ptrs = bandParamPtrs[(size_t) i];
        ptrs.active      = apvts.getRawParameterValue (bandParamID (i, BandParamSuffix::active));
        ptrs.freq        = apvts.getRawParameterValue (bandParamID (i, BandParamSuffix::freq));
        ptrs.gain        = apvts.getRawParameterValue (bandParamID (i, BandParamSuffix::gain));
        ptrs.q           = apvts.getRawParameterValue (bandParamID (i, BandParamSuffix::q));
        ptrs.type        = apvts.getRawParameterValue (bandParamID (i, BandParamSuffix::type));
        ptrs.channelMode = apvts.getRawParameterValue (bandParamID (i, BandParamSuffix::channelMode));
        ptrs.phaseBlend  = apvts.getRawParameterValue (bandParamID (i, BandParamSuffix::phaseBlend));
        ptrs.splitMode   = apvts.getRawParameterValue (bandParamID (i, BandParamSuffix::splitMode));
        jassert (ptrs.active != nullptr && ptrs.freq != nullptr && ptrs.gain != nullptr
                 && ptrs.q != nullptr && ptrs.type != nullptr && ptrs.channelMode != nullptr
                 && ptrs.phaseBlend != nullptr && ptrs.splitMode != nullptr);
    }

    masterGainParam = apvts.getRawParameterValue (masterGainParamID);
    jassert (masterGainParam != nullptr);
}

HybridQAudioProcessor::~HybridQAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout HybridQAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    const auto filterTypeChoices  = getFilterTypeChoices();
    const auto channelModeChoices = getChannelModeChoices();

    // Rango log-friendly para frecuencia: skew centrado de forma que el
    // punto medio del slider caiga alrededor de ~1 kHz, igual que en la
    // mayoría de EQs de referencia.
    juce::NormalisableRange<float> freqRange (20.0f, 20000.0f, 0.0f, 0.25f);
    juce::NormalisableRange<float> qRange (0.1f, 18.0f, 0.0f, 0.3f);
    juce::NormalisableRange<float> gainRange (-24.0f, 24.0f, 0.01f);

    for (int i = 0; i < maxBands; ++i)
    {
        // Solo las 6 primeras bandas activas por defecto (espaciadas en
        // frecuencia) para que el plugin no arranque con 24 handles
        // superpuestos en 1 kHz; el resto quedan inactivas hasta que el
        // usuario las cree desde la UI.
        const bool defaultActive = i < 6;
        const float defaultFreq  = 100.0f * std::pow (2.0f, (float) i * 1.4f);

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { bandParamID (i, BandParamSuffix::active), 1 },
            "Band " + juce::String (i + 1) + " Active",
            defaultActive));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { bandParamID (i, BandParamSuffix::freq), 1 },
            "Band " + juce::String (i + 1) + " Freq",
            freqRange,
            juce::jlimit (20.0f, 20000.0f, defaultFreq),
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { bandParamID (i, BandParamSuffix::gain), 1 },
            "Band " + juce::String (i + 1) + " Gain",
            gainRange,
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { bandParamID (i, BandParamSuffix::q), 1 },
            "Band " + juce::String (i + 1) + " Q",
            qRange,
            0.707f));

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { bandParamID (i, BandParamSuffix::type), 1 },
            "Band " + juce::String (i + 1) + " Type",
            filterTypeChoices,
            0)); // Bell por defecto

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { bandParamID (i, BandParamSuffix::channelMode), 1 },
            "Band " + juce::String (i + 1) + " Channel",
            channelModeChoices,
            0)); // Stereo por defecto

        // Fase 2: 0 = 100% minimum-phase/analógico (idéntico a la Fase 1,
        // zero-latency en su propio procesamiento), 1 = 100% linear-phase.
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { bandParamID (i, BandParamSuffix::phaseBlend), 1 },
            "Band " + juce::String (i + 1) + " Phase Blend",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.0f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return juce::String ((int) (v * 100.0f)); })));

        // Fase 3: Full / Transient / Sustain (sección 2.2 del brief).
        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { bandParamID (i, BandParamSuffix::splitMode), 1 },
            "Band " + juce::String (i + 1) + " Split Mode",
            getSplitModeChoices(),
            0)); // Full por defecto
    }

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { masterGainParamID, 1 },
        "Master Gain",
        gainRange,
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return { params.begin(), params.end() };
}

//==============================================================================
void HybridQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = 2;

    for (auto& band : bands)
        band.prepare (spec);

    splitter.prepare (spec);
    transientBuffer.setSize (2, samplesPerBlock);
    sustainBuffer.setSize (2, samplesPerBlock);

    // Latencia GLOBAL FIJA (ver nota extensa en BandProcessor.h): se
    // reporta siempre, exista o no una banda con blend > 0 en este
    // momento, para evitar cambiar la latencia en caliente (lo cual
    // obligaría a vaciar/realinear el PDC del host cada vez que el
    // usuario mueve un Phase Blend, mucho más invasivo que el pequeño
    // costo fijo de "regalar" esta latencia siempre).
    setLatencySamples (BandProcessor::firLatencySamples);

    masterGainSmoothed.reset (sampleRate, 0.02);
    masterGainSmoothed.setCurrentAndTargetValue (masterGainParam != nullptr ? masterGainParam->load() : 0.0f);

    fifoIndex = 0;
    fifo.fill (0.0f);
    fftData.fill (0.0f);
    nextFFTBlockReady = false;
}

void HybridQAudioProcessor::releaseResources()
{
    for (auto& band : bands)
        band.reset();
    splitter.reset();
}

bool HybridQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    const auto& mainIn  = layouts.getMainInputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainOut == mainIn;
}

void HybridQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // 1) Actualizar coeficientes y estado activo de cada banda a partir de
    //    los parámetros actuales. Se hace una vez por bloque (no por
    //    sample) -- suficientemente rápido para automatización normal;
    //    si en el futuro se necesita sample-accurate se puede subdividir
    //    el bloque en el punto de cada cambio de parámetro.
    for (int i = 0; i < maxBands; ++i)
    {
        auto& band = bands[(size_t) i];
        auto& ptrs = bandParamPtrs[(size_t) i];

        band.isActive = ptrs.active->load() > 0.5f;

        if (band.isActive)
        {
            const auto type = static_cast<FilterType> ((int) ptrs.type->load());
            band.updateCoefficients (currentSampleRate, type, ptrs.freq->load(),
                                      ptrs.gain->load(), ptrs.q->load(), ptrs.phaseBlend->load());
        }
    }

    // 1.5) Fase 3: ¿hace falta el split transient/sustain? Solo si AL MENOS
    //      una banda ACTIVA está en modo Transient o Sustain -- si todas
    //      están en Full, el costo es idéntico a Fase 2 (ni se calcula el
    //      split ni se duplica el procesamiento de ninguna banda).
    bool needsSplit = false;
    for (int i = 0; i < maxBands; ++i)
    {
        if (! bands[(size_t) i].isActive)
            continue;
        const auto mode = static_cast<SplitMode> ((int) bandParamPtrs[(size_t) i].splitMode->load());
        if (mode != SplitMode::Full)
        {
            needsSplit = true;
            break;
        }
    }

    if (needsSplit)
    {
        // `avoidReallocating=true`: como ya se reservó el tamaño máximo en
        // prepareToPlay, esto nunca reasigna memoria en el audio thread.
        transientBuffer.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        sustainBuffer.setSize (buffer.getNumChannels(), buffer.getNumSamples(), false, false, true);
        splitter.split (buffer, transientBuffer, sustainBuffer);
    }

    // 2) Cadena de bandas en serie, en orden de índice (0 primero). El
    //    orden importa cuando varias bandas comparten la misma región de
    //    frecuencia o el mismo lane M/S -- es el comportamiento esperado
    //    de un EQ tipo Pro-Q (las bandas se procesan en el orden en que
    //    existen, no por frecuencia). Mientras needsSplit es true, las
    //    bandas operan exclusivamente sobre transientBuffer/sustainBuffer
    //    (buffer se ignora dentro de BandProcessor::process hasta el paso 2.5).
    for (int i = 0; i < maxBands; ++i)
    {
        auto& ptrs = bandParamPtrs[(size_t) i];
        const auto channelMode = static_cast<ChannelMode> ((int) ptrs.channelMode->load());
        const auto splitMode = static_cast<SplitMode> ((int) ptrs.splitMode->load());
        bands[(size_t) i].process (buffer, needsSplit ? &transientBuffer : nullptr,
                                    needsSplit ? &sustainBuffer : nullptr, channelMode, splitMode);
    }

    // 2.5) Recombinar: buffer = transientBuffer + sustainBuffer. Si
    //      needsSplit es false, `buffer` ya quedó completamente procesado
    //      en el paso 2 (las bandas escribieron directo ahí), así que no
    //      hace falta ningún paso adicional.
    if (needsSplit)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            buffer.copyFrom (ch, 0, transientBuffer, ch, 0, buffer.getNumSamples());
            buffer.addFrom  (ch, 0, sustainBuffer,   ch, 0, buffer.getNumSamples());
        }
    }

    // 3) Master gain de salida.
    masterGainSmoothed.setTargetValue (masterGainParam != nullptr ? masterGainParam->load() : 0.0f);

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto gainLinear = juce::Decibels::decibelsToGain (masterGainSmoothed.getNextValue());

        float monoSum = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);
            data[sample] *= gainLinear;
            monoSum += data[sample];
        }

        // 4) Alimentar el analizador de espectro con la suma mono
        //    post-procesamiento (lo que el usuario realmente escucha).
        pushNextSampleIntoFifo (numChannels > 0 ? monoSum / (float) numChannels : 0.0f);
    }
}

//==============================================================================
void HybridQAudioProcessor::pushNextSampleIntoFifo (float sample) noexcept
{
    // Patrón estándar de JUCE para alimentar un analizador de espectro sin
    // locks desde el audio thread (ver "Spectrum Analyzer Tutorial" de
    // JUCE): cuando el fifo se llena, se copia a fftData y se marca el
    // flag atómico para que la UI lo procese en su próximo timer tick.
    if (fifoIndex == fftSize)
    {
        if (! nextFFTBlockReady.load())
        {
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            std::copy (fifo.begin(), fifo.end(), fftData.begin());
            nextFFTBlockReady = true;
        }
        fifoIndex = 0;
    }

    fifo[(size_t) fifoIndex++] = sample;
}

//==============================================================================
float HybridQAudioProcessor::getFrequencyResponseDb (float frequencyHz) const
{
    // Lee directamente los valores atómicos de parámetro (std::atomic<float>,
    // seguros de leer desde cualquier hilo) en vez de tocar el estado
    // interno de cada BandProcessor -- ver el comentario extenso en
    // BandProcessor::computeMagnitudeForFrequencyDb sobre por qué la
    // versión anterior (leer BandProcessor::currentCoeffs desde la UI)
    // causaba un crash al arrastrar un handle.
    float totalDb = 0.0f;
    for (int i = 0; i < maxBands; ++i)
    {
        const auto& ptrs = bandParamPtrs[(size_t) i];
        if (ptrs.active->load() <= 0.5f)
            continue;

        const auto type = static_cast<FilterType> ((int) ptrs.type->load());
        totalDb += BandProcessor::computeMagnitudeForFrequencyDb (
            currentSampleRate, type, ptrs.freq->load(), ptrs.gain->load(), ptrs.q->load(),
            frequencyHz);
    }
    return totalDb;
}

bool HybridQAudioProcessor::isBandActive (int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= maxBands)
        return false;
    return bandParamPtrs[(size_t) bandIndex].active->load() > 0.5f;
}

//==============================================================================
juce::AudioProcessorEditor* HybridQAudioProcessor::createEditor()
{
    return new HybridQAudioProcessorEditor (*this);
}

//==============================================================================
void HybridQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void HybridQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HybridQAudioProcessor();
}

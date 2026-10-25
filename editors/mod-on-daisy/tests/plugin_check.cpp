// Loads the built "Mod on Daisy.vst3" like a host and checks what Studio One relies on:
//   ModOnDaisyPluginCheck <path to .vst3> <a .modpatch file>
// one effect with stereo in/out, audio passes bit for bit, the patch survives a state
// round trip into a second instance, the editor window opens and closes. Run with
// MOD_ON_DAISY_NO_AUTOCONNECT=1 so an attached kit is not touched.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

// JUCE's VST3 host wraps the plugin's own state (IComponent) in base64 inside an XML
// element; Studio One stores the IComponent bytes. These two helpers go to and from them.
void SetPluginState(juce::AudioPluginInstance& p, const juce::String& raw) {
    juce::XmlElement xml("VST3PluginState");
    const juce::MemoryBlock bytes(raw.toRawUTF8(), raw.getNumBytesAsUTF8());
    xml.createNewChildElement("IComponent")->addTextElement(bytes.toBase64Encoding());
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(xml, block);
    p.setStateInformation(block.getData(), int(block.getSize()));
}

// The patch text the plugin stored; JUCE's wrapper may append its own private data.
juce::String StateOf(juce::AudioPluginInstance& p) {
    juce::MemoryBlock block;
    p.getStateInformation(block);
    const std::unique_ptr<juce::XmlElement> xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), int(block.getSize()));
    juce::MemoryBlock raw;
    if(!xml || !xml->getChildByName("IComponent") || !raw.fromBase64Encoding(xml->getChildByName("IComponent")->getAllSubText()))
        return {};
    const juce::String text = juce::String::fromUTF8(static_cast<const char*>(raw.getData()), int(raw.getSize()));
    return text.upToLastOccurrenceOf("}", true, false);
}

void Pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if(argc < 3) {
        std::printf("usage: ModOnDaisyPluginCheck <Mod on Daisy.vst3> <patch.modpatch>\n");
        return 2;
    }
    const juce::String path = juce::String::fromUTF8(argv[1]);
    const juce::String patch = juce::File(juce::String::fromUTF8(argv[2])).loadFileAsString();
    Check(patch.contains("mod-on-daisy-patch"), "test patch readable");

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, path);
    Check(types.size() == 1, "one plugin in the bundle");
    if(types.isEmpty())
        return 1;
    const juce::PluginDescription& d = *types[0];
    Check(d.name == "Mod on Daisy" && !d.isInstrument, "name \"Mod on Daisy\", an effect");
    std::printf("     %s / %s / %s, %d in, %d out\n", d.manufacturerName.toRawUTF8(), d.category.toRawUTF8(),
                d.version.toRawUTF8(), d.numInputChannels, d.numOutputChannels);

    juce::String error;
    std::unique_ptr<juce::AudioPluginInstance> a = format.createInstanceFromDescription(d, 48000.0, 512, error);
    Check(a != nullptr, "instance created");
    if(!a) {
        std::printf("     %s\n", error.toRawUTF8());
        return 1;
    }
    Check(a->getTotalNumInputChannels() == 2 && a->getTotalNumOutputChannels() == 2, "stereo in, stereo out");
    a->setPlayConfigDetails(2, 2, 48000.0, 512);
    a->prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> buffer(2, 512), original(2, 512);
    for(int ch = 0; ch < 2; ++ch)
        for(int i = 0; i < 512; ++i)
            original.setSample(ch, i, float(std::sin(0.01 * i + ch)) * 0.5f);
    buffer.makeCopyOf(original);
    juce::MidiBuffer midi;
    a->processBlock(buffer, midi);
    bool same = true;
    for(int ch = 0; ch < 2; ++ch)
        for(int i = 0; i < 512; ++i)
            same = same && buffer.getSample(ch, i) == original.getSample(ch, i);
    Check(same, "audio passes through unchanged");

    // Project state: set a patch, let the plugin apply it (message thread), read it back.
    SetPluginState(*a, patch);
    Pump(500);
    const juce::String saved = StateOf(*a);
    Check(saved.contains("mod-on-daisy-patch") && saved.contains("\"depth\""), "state is a .modpatch text");
    std::unique_ptr<juce::AudioPluginInstance> b = format.createInstanceFromDescription(d, 48000.0, 512, error);
    SetPluginState(*b, saved);
    Pump(500);
    Check(b && !saved.isEmpty() && StateOf(*b) == saved, "state survives a round trip into a second instance");
    const auto depths = [](const juce::String& s) {
        return s.fromFirstOccurrenceOf("\"depth\"", false, false).upToFirstOccurrenceOf("\"lfo\"", false, false);
    };
    Check(depths(saved) == depths(StateOf(*b)) && depths(saved).contains("-100"), "matrix of the project patch kept");

    // Editor window: opens with the editor's size, closes, opens again (the content stays).
    for(int round = 0; round < 2; ++round) {
        std::unique_ptr<juce::AudioProcessorEditor> e(a->createEditorAndMakeActive());
        Check(e != nullptr && e->getWidth() >= 1100 && e->getHeight() >= 860, "editor window opens");
        Pump(200);
        e.reset();
        Pump(100);
    }
    a->releaseResources();
    b.reset();
    a.reset();
    Pump(200);
    std::printf(failures ? "FAIL: %d checks\n" : "PASS: VST3 plugin loads, passes audio, keeps its state\n", failures);
    return failures ? 1 : 0;
}

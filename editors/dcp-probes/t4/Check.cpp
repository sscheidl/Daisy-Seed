// Pre-check for the T4 probe in JUCE's own VST3 host (not Studio One): load the plugin,
// restore a state with another mapping, and check that the host is told about the new
// names (kParamTitlesChanged → audioProcessorChanged.parameterInfoChanged) and sees them.
//
//   DcpT4Check "<path>/DCP T4 Probe.vst3"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <iostream>

namespace {
struct Listener : juce::AudioProcessorListener {
    int info_changes = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& d) override {
        if(d.parameterInfoChanged)
            ++info_changes;
    }
};

juce::String Names(juce::AudioPluginInstance& p) {
    // Only the eight slots; a JUCE instrument with MIDI input adds Bypass and
    // 16 x 130 "MIDI CC" parameters (VST3 has no CC events, JUCE emulates them).
    juce::StringArray names;
    for(int i = 0; i < 8 && i < p.getParameters().size(); ++i)
        names.add(p.getParameters()[i]->getName(128));
    return names.joinIntoString(", ");
}

std::unique_ptr<juce::AudioPluginInstance> Load(juce::VST3PluginFormat& format, const juce::String& path) {
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, path);
    if(found.isEmpty())
        return nullptr;
    juce::String error;
    auto p = format.createInstanceFromDescription(*found[0], 48000.0, 256, error);
    if(!p)
        std::cout << "load error: " << error << "\n";
    return p;
}

int failures = 0;
void Expect(bool ok, const juce::String& what) {
    std::cout << (ok ? "PASS " : "FAIL ") << what << "\n";
    failures += ok ? 0 : 1;
}
}

int main(int argc, char** argv) {
    if(argc < 2) {
        std::cout << "usage: DcpT4Check <plugin.vst3>\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;
    juce::VST3PluginFormat format;
    const juce::String path = juce::String::fromUTF8(argv[1]);

    auto first = Load(format, path);
    if(!first)
        return 1;
    Listener listener;
    first->addListener(&listener);
    std::cout << "loaded: " << first->getName() << ", " << first->getParameters().size() << " parameters\n";
    std::cout << "names at load: " << Names(*first) << "\n";
    Expect(first->getParameters().size() >= 8, "eight automation slots first");
    Expect(Names(*first).startsWith("Slot 1"), "generic names at load");

    // A project state with mapping B, as the probe writes it.
    juce::XmlElement xml("DcpT4Probe");
    xml.setAttribute("mapping", 2);
    xml.setAttribute("defer", false);
    xml.setAttribute("slot01", 0.25);
    juce::MemoryBlock component;
    juce::AudioProcessor::copyXmlToBinary(xml, component);
    // JUCE's VST3 host wraps the component state in its own XML.
    juce::XmlElement wrapper("VST3PluginState");
    wrapper.createNewChildElement("IComponent")->addTextElement(component.toBase64Encoding());
    juce::MemoryBlock state;
    juce::AudioProcessor::copyXmlToBinary(wrapper, state);
    first->setStateInformation(state.getData(), int(state.getSize()));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);

    std::cout << "names after restore: " << Names(*first) << "\n";
    Expect(listener.info_changes > 0, "host told about changed parameter info");
    Expect(Names(*first).startsWith("B Crescendo"), "host sees the mapping B names");
    Expect(std::abs(first->getParameters()[0]->getValue() - 0.25f) < 1e-4f, "slot01 value restored");

    // Save and reopen in a fresh instance, as Studio One does with a project.
    juce::MemoryBlock saved;
    first->getStateInformation(saved);
    first->removeListener(&listener);
    first.reset();

    auto second = Load(format, path);
    if(!second)
        return 1;
    Listener listener2;
    second->addListener(&listener2);
    second->setStateInformation(saved.getData(), int(saved.getSize()));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
    std::cout << "names after reopen: " << Names(*second) << "\n";
    Expect(listener2.info_changes > 0, "reopen: host told again");
    Expect(Names(*second).startsWith("B Crescendo"), "reopen: mapping B names");
    second->removeListener(&listener2);
    second.reset();

    std::cout << (failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return failures == 0 ? 0 : 1;
}

#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

// DucklingDev: developer tool built from the same sources as the plugin.
//   DucklingDev selftest              -> processor-level integration tests, exit code 0 on success
//   DucklingDev snapshot <outDir>     -> renders the editor in several states to PNG files in outDir

int runSelfTest();                                // SelfTest.cpp
int runSnapshot (const juce::File& outputDir);    // Snapshot.cpp

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::String command = argc > 1 ? juce::String (argv[1]) : juce::String();
    const auto cwd = juce::File::getCurrentWorkingDirectory();

    if (command == "selftest")
        return runSelfTest();

    if (command == "snapshot")
        return runSnapshot (cwd.getChildFile (argc > 2 ? argv[2] : "snapshots"));

    std::cout << "usage: DucklingDev selftest | snapshot <outDir>\n";
    return 2;
}

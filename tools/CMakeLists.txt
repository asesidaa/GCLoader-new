add_subdirectory(ConfigGUI)
add_subdirectory(CardReaderTestClient)
add_subdirectory(AsioProbe)

add_dependencies(ConfigGUI AsioProbe)

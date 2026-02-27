#pragma once

#include "text_renderer.h"
#include <string>
#include <vector>
#include <functional>

class Console {
public:
    // registerCommand callback: takes args string, returns response string
    using CmdCallback = std::function<std::string(const std::string& args)>;

    Console(TextRenderer& renderer);

    bool isOpen() const { return open; }
    void toggle();

    // Feed keyboard input from GLFW
    void handleChar(unsigned int codepoint);
    void handleKey(int key, int action, int mods);

    // Register a named command with a callback
    void registerCommand(const std::string& name, const std::string& help, CmdCallback cb);

    // Print a line to the console log
    void print(const std::string& msg);

    // Draw the console overlay (call between textRenderer.begin/end)
    void draw(int screenWidth, int screenHeight);

    // Query game state set by console commands
    bool showFps() const { return fpsEnabled; }

private:
    TextRenderer& text;
    bool open = false;

    std::string inputBuffer;
    std::vector<std::string> log;
    int scrollOffset = 0;

    struct Command {
        std::string name;
        std::string help;
        CmdCallback callback;
    };
    std::vector<Command> commands;

    std::vector<std::string> suggestions;
    int selectedSuggestion = -1;

    bool fpsEnabled = false;

    void execute(const std::string& line);
    void updateSuggestions();
};

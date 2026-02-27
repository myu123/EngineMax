#include "console.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <sstream>

Console::Console(TextRenderer& renderer) : text(renderer) {
    // Built-in commands
    registerCommand("show_fps", "show_fps [0|1] - Toggle FPS counter",
        [this](const std::string& args) -> std::string {
            if (args == "1") {
                fpsEnabled = true;
                return "FPS counter enabled";
            } else if (args == "0") {
                fpsEnabled = false;
                return "FPS counter disabled";
            } else {
                return "Usage: show_fps [0|1]";
            }
        });

    registerCommand("help", "help - List all commands",
        [this](const std::string&) -> std::string {
            std::string result = "Available commands:";
            for (const auto& cmd : commands)
                result += "\n  " + cmd.help;
            return result;
        });

    registerCommand("clear", "clear - Clear console log",
        [this](const std::string&) -> std::string {
            log.clear();
            return "";
        });

    print("EngineMax Developer Console");
    print("Type 'help' for a list of commands.");
}

void Console::toggle() {
    open = !open;
    if (open) {
        inputBuffer.clear();
        suggestions.clear();
        selectedSuggestion = -1;
    }
}

void Console::registerCommand(const std::string& name, const std::string& help, CmdCallback cb) {
    commands.push_back({ name, help, cb });
}

void Console::print(const std::string& msg) {
    // Split multi-line messages
    std::istringstream stream(msg);
    std::string line;
    while (std::getline(stream, line))
        log.push_back(line);

    // Cap log size
    if (log.size() > 200)
        log.erase(log.begin(), log.begin() + (log.size() - 200));
}

void Console::handleChar(unsigned int codepoint) {
    if (!open) return;
    if (codepoint == '`' || codepoint == '~') return; // don't type the toggle key

    if (codepoint >= 32 && codepoint < 127) {
        inputBuffer += static_cast<char>(codepoint);
        updateSuggestions();
    }
}

void Console::handleKey(int key, int action, int /*mods*/) {
    if (!open) return;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (key == GLFW_KEY_BACKSPACE) {
        if (!inputBuffer.empty()) {
            inputBuffer.pop_back();
            updateSuggestions();
        }
    } else if (key == GLFW_KEY_ENTER) {
        if (!inputBuffer.empty()) {
            print("> " + inputBuffer);
            execute(inputBuffer);
            inputBuffer.clear();
            suggestions.clear();
            selectedSuggestion = -1;
        }
    } else if (key == GLFW_KEY_TAB) {
        // Autocomplete
        if (!suggestions.empty()) {
            int idx = (selectedSuggestion >= 0) ? selectedSuggestion : 0;
            inputBuffer = suggestions[idx] + " ";
            suggestions.clear();
            selectedSuggestion = -1;
        }
    } else if (key == GLFW_KEY_UP) {
        if (!suggestions.empty()) {
            selectedSuggestion = std::max(0, selectedSuggestion - 1);
        }
    } else if (key == GLFW_KEY_DOWN) {
        if (!suggestions.empty()) {
            selectedSuggestion = std::min((int)suggestions.size() - 1, selectedSuggestion + 1);
        }
    }
}

void Console::execute(const std::string& line) {
    // Split into command name and args
    std::string cmdName, args;
    auto spacePos = line.find(' ');
    if (spacePos != std::string::npos) {
        cmdName = line.substr(0, spacePos);
        args = line.substr(spacePos + 1);
        // Trim leading spaces from args
        auto start = args.find_first_not_of(' ');
        if (start != std::string::npos) args = args.substr(start);
        else args.clear();
    } else {
        cmdName = line;
    }

    // Find and run the command
    for (const auto& cmd : commands) {
        if (cmd.name == cmdName) {
            std::string result = cmd.callback(args);
            if (!result.empty()) print(result);
            return;
        }
    }

    print("Unknown command: '" + cmdName + "'. Type 'help' for a list of commands.");
}

void Console::updateSuggestions() {
    suggestions.clear();
    selectedSuggestion = -1;

    if (inputBuffer.empty()) return;

    // Extract the command portion (before first space)
    std::string prefix = inputBuffer;
    auto spacePos = prefix.find(' ');
    if (spacePos != std::string::npos) return; // already typing args, no suggestions

    for (const auto& cmd : commands) {
        if (cmd.name.rfind(prefix, 0) == 0) { // starts with prefix
            suggestions.push_back(cmd.name);
        }
    }

    if (suggestions.size() == 1 && suggestions[0] == prefix)
        suggestions.clear(); // exact match, no need to suggest
}

void Console::draw(int screenWidth, int screenHeight) {
    if (!open) return;

    float consoleHeight = screenHeight * 0.4f; // 40% of screen
    float lineH = text.charHeight();
    float pad = 8.0f;

    // Background
    text.drawRect(0, 0, (float)screenWidth, consoleHeight, glm::vec4(0.05f, 0.05f, 0.08f, 0.92f));

    // Bottom separator line
    text.drawRect(0, consoleHeight - lineH - pad * 2, (float)screenWidth, 1.0f, glm::vec4(0.4f, 0.4f, 0.5f, 1.0f));

    // Input line
    float inputY = consoleHeight - lineH - pad;
    std::string prompt = "> " + inputBuffer + "_";
    text.drawText(prompt, pad, inputY, glm::vec4(0.0f, 1.0f, 0.4f, 1.0f));

    // Log lines (above the input, scrolling up)
    float logBottom = consoleHeight - lineH - pad * 2 - 2.0f;
    int maxLines = (int)((logBottom - pad) / lineH);
    int startLine = std::max(0, (int)log.size() - maxLines);
    float y = logBottom - lineH;
    for (int i = (int)log.size() - 1; i >= startLine && y >= pad; --i) {
        text.drawText(log[i], pad, y, glm::vec4(0.85f, 0.85f, 0.85f, 1.0f));
        y -= lineH;
    }

    // Autocomplete suggestions dropdown (below input line)
    if (!suggestions.empty()) {
        float sugY = consoleHeight + 2.0f;
        float sugW = 0;
        for (const auto& s : suggestions)
            sugW = std::max(sugW, s.size() * text.charWidth() + pad * 2);

        text.drawRect(pad, sugY, sugW, (float)suggestions.size() * lineH + pad, glm::vec4(0.12f, 0.12f, 0.15f, 0.95f));

        for (int i = 0; i < (int)suggestions.size(); ++i) {
            glm::vec4 col = (i == selectedSuggestion)
                ? glm::vec4(0.0f, 1.0f, 0.4f, 1.0f)
                : glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);

            if (i == selectedSuggestion) {
                text.drawRect(pad, sugY + i * lineH, sugW, lineH, glm::vec4(0.2f, 0.2f, 0.25f, 1.0f));
            }
            text.drawText(suggestions[i], pad + 4.0f, sugY + i * lineH, col);
        }
    }
}

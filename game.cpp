#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

constexpr int kScreenWidth = 64;
constexpr int kScreenHeight = 18;
constexpr float kGravity = 32.0f;
constexpr float kJumpVelocity = -11.5f;
constexpr float kRunSpeed = 12.0f;          // world units per second
constexpr int kFrameTimeMs = 50;            // 20 FPS
constexpr char kPlayerSprite = '@';
constexpr char kCollectibleSprite = '*';

struct Level {
    std::string name;
    std::vector<std::string> rows; // top to bottom
};

#ifndef _WIN32
class TerminalRawMode {
public:
    TerminalRawMode() {
        tcgetattr(STDIN_FILENO, &oldState_);
        termios raw = oldState_;
        raw.c_lflag &= static_cast<unsigned int>(~(ICANON | ECHO));
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);

        oldFlags_ = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, oldFlags_ | O_NONBLOCK);
    }

    ~TerminalRawMode() {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldState_);
        fcntl(STDIN_FILENO, F_SETFL, oldFlags_);
    }

private:
    termios oldState_{};
    int oldFlags_{};
};
#endif

char pollKey() {
#ifdef _WIN32
    if (_kbhit()) {
        return static_cast<char>(_getch());
    }
    return '\0';
#else
    char ch = '\0';
    if (read(STDIN_FILENO, &ch, 1) == 1) {
        return ch;
    }
    return '\0';
#endif
}

char levelTile(const Level& level, int x, int y) {
    if (y < 0 || y >= static_cast<int>(level.rows.size())) {
        return ' ';
    }

    if (x < 0 || x >= static_cast<int>(level.rows[y].size())) {
        return ' ';
    }

    return level.rows[y][x];
}

bool isSolid(char tile) {
    return tile == '=' || tile == '#';
}

void clearScreen() {
    std::cout << "\x1b[2J\x1b[H";
}

void render(const Level& level, float playerX, float playerY, int collected, int totalCollectibles, int levelIndex, int levelCount) {
    int cameraX = static_cast<int>(playerX) - 10;
    if (cameraX < 0) {
        cameraX = 0;
    }

    std::vector<std::string> screen(kScreenHeight, std::string(kScreenWidth, ' '));

    for (int sy = 0; sy < kScreenHeight; ++sy) {
        int worldY = sy;
        for (int sx = 0; sx < kScreenWidth; ++sx) {
            int worldX = cameraX + sx;
            char t = levelTile(level, worldX, worldY);
            if (t == 'o') {
                t = kCollectibleSprite;
            }
            screen[sy][sx] = t;
        }
    }

    int playerScreenX = static_cast<int>(std::round(playerX)) - cameraX;
    int playerScreenY = static_cast<int>(std::round(playerY));
    if (playerScreenX >= 0 && playerScreenX < kScreenWidth && playerScreenY >= 0 && playerScreenY < kScreenHeight) {
        screen[playerScreenY][playerScreenX] = kPlayerSprite;
    }

    clearScreen();
    std::cout << "Nooby Dash  |  Level " << (levelIndex + 1) << '/' << levelCount
              << "  |  Icons " << collected << '/' << totalCollectibles << "\n";
    std::cout << "Controls: SPACE jump, Q quit\n\n";

    for (const auto& row : screen) {
        std::cout << row << '\n';
    }
}

int countCollectibles(const Level& level) {
    int count = 0;
    for (const auto& row : level.rows) {
        for (char c : row) {
            if (c == 'o') {
                ++count;
            }
        }
    }
    return count;
}

bool playLevel(Level& level, int levelIndex, int levelCount, int& totalCollected) {
    float playerX = 2.0f;
    float playerY = static_cast<float>(kScreenHeight - 3);
    float velocityY = 0.0f;
    int levelCollected = 0;
    const int totalCollectibles = countCollectibles(level);

    while (true) {
        char key = pollKey();
        if (key == 'q' || key == 'Q') {
            return false;
        }

        const float dt = static_cast<float>(kFrameTimeMs) / 1000.0f;

        bool onGround = isSolid(levelTile(level, static_cast<int>(std::round(playerX)), static_cast<int>(std::floor(playerY + 1.0f))));
        if (key == ' ' && onGround) {
            velocityY = kJumpVelocity;
        }

        velocityY += kGravity * dt;
        float nextY = playerY + velocityY * dt;

        if (velocityY > 0.0f) {
            int checkY = static_cast<int>(std::floor(nextY + 0.95f));
            if (isSolid(levelTile(level, static_cast<int>(std::round(playerX)), checkY))) {
                nextY = static_cast<float>(checkY - 1);
                velocityY = 0.0f;
            }
        } else if (velocityY < 0.0f) {
            int checkY = static_cast<int>(std::floor(nextY));
            if (isSolid(levelTile(level, static_cast<int>(std::round(playerX)), checkY))) {
                nextY = static_cast<float>(checkY + 1);
                velocityY = 0.0f;
            }
        }

        playerY = nextY;

        playerX += kRunSpeed * dt;

        if (playerY > kScreenHeight - 1) {
            playerY = static_cast<float>(kScreenHeight - 2);
            velocityY = 0.0f;
        }

        int tileX = static_cast<int>(std::round(playerX));
        int tileY = static_cast<int>(std::round(playerY));
        if (levelTile(level, tileX, tileY) == 'o') {
            level.rows[tileY][tileX] = ' ';
            ++levelCollected;
            ++totalCollected;
        }

        render(level, playerX, playerY, levelCollected, totalCollectibles, levelIndex, levelCount);

        if (playerX >= static_cast<float>(level.rows.front().size() - 2)) {
            std::cout << "\nLevel complete!\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kFrameTimeMs));
    }
}

std::vector<Level> buildLevels() {
    return {
        {"Sunny Start",
         {
             "                                                                ",
             "                                                                ",
             "                                                                ",
             "                                                                ",
             "                                                                ",
             "                 o                        o                      ",
             "            ###      o       ###    o          ###              ",
             "      o                                                         ",
             "                     o                               o          ",
             "                                                                ",
             "                                                                ",
             "                                                                ",
             "                                                                ",
             "                                                                ",
             "===============================================================#",
             "===============================================================#",
             "===============================================================#",
             "===============================================================#",
         }},
        {"Cloud Bridges",
         {
             "                                                                ",
             "                                                                ",
             "                                                                ",
             "         o              o                    o                  ",
             "        ###            ###                  ###                 ",
             "                                                                ",
             "                      o                       o                 ",
             "                  ###      o             ###                    ",
             "     o                                                          ",
             "                                                                ",
             "                                                                ",
             "                                                                ",
             "                                                                ",
             "                                                                ",
             "===============================================================#",
             "===============================================================#",
             "===============================================================#",
             "===============================================================#",
         }}}
    ;
}

} // namespace

int main() {
#ifndef _WIN32
    TerminalRawMode rawMode;
#endif

    std::cout << "Welcome to Nooby Dash!\n";
    std::cout << "You run automatically to the right. Press SPACE to jump and collect icons (*).\n";
    std::cout << "Press ENTER to begin...";
    std::cin.get();

    auto levels = buildLevels();
    int totalCollected = 0;

    for (int i = 0; i < static_cast<int>(levels.size()); ++i) {
        bool completed = playLevel(levels[i], i, static_cast<int>(levels.size()), totalCollected);
        if (!completed) {
            clearScreen();
            std::cout << "Thanks for playing Nooby Dash! Icons collected: " << totalCollected << "\n";
            return 0;
        }
    }

    clearScreen();
    std::cout << "You finished every level! Total icons collected: " << totalCollected << "\n";
    std::cout << "Nooby Dash complete!\n";
    return 0;
}

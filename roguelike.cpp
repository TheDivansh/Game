#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

using std::string;
using std::to_string;
using std::unique_ptr;
using std::vector;

const bool USE_EMOJI = true; 
const int MAP_W = 50;
const int MAP_H = 16;
const int MAX_FLOOR = 5;

int rnd(int lo, int hi) { return lo + std::rand() % (hi - lo + 1); }

namespace term {
bool ansi = false;

void init() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) ansi = SetConsoleMode(h, mode | 0x0004) != 0;
    SetConsoleOutputCP(65001);
#else
    ansi = true;
#endif
    if (ansi) std::cout << "\x1b[2J\x1b[H\x1b[?25l";
}

void shutdown() {
    if (ansi) std::cout << "\x1b[?25h";
}

void home() {
    if (ansi) std::cout << "\x1b[H";
    else std::system("cls");
}

int getKey() {
#ifdef _WIN32
    int c = _getch();
    if (c == 0 || c == 224) {  // arrow keys
        switch (_getch()) {
            case 72: return 'w';
            case 80: return 's';
            case 75: return 'a';
            case 77: return 'd';
        }
        return 0;
    }
    return c;
#else
    termios oldT, rawT;
    bool tty = tcgetattr(0, &oldT) == 0;
    if (tty) {
        rawT = oldT;
        rawT.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(0, TCSANOW, &rawT);
    }
    int c = getchar();
    if (tty) tcsetattr(0, TCSANOW, &oldT);
    return c == EOF ? -1 : c;
#endif
}

string paint(const string& text, int fg, int bg = -1) {
    if (!ansi) return text;
    string code;
    if (fg >= 0) code += "38;5;" + to_string(fg);
    if (bg >= 0) code += (code.empty() ? "" : ";") + string("48;5;") + to_string(bg);
    if (code.empty()) return text;
    return "\x1b[" + code + "m" + text + "\x1b[0m";
}
string paint(char ch, int fg, int bg = -1) { return paint(string(1, ch), fg, bg); }
} 

class Log {
    std::deque<string> lines;

public:
    void add(const string& s) {
        lines.push_back(s);
        if (lines.size() > 3) lines.pop_front();
    }
    const std::deque<string>& get() const { return lines; }
};

class Entity {
protected:
    string name;
    char glyph;
    int x = 0, y = 0;
    int hp, maxHp, atk, def;

public:
    Entity(string n, char g, int h, int a, int d)
        : name(std::move(n)), glyph(g), hp(h), maxHp(h), atk(a), def(d) {}
    virtual ~Entity() = default;

    int takeDamage(int dmg) {
        int actual = std::max(1, dmg - def);
        hp = std::max(0, hp - actual);
        return actual;
    }
    void heal(int amount) { hp = std::min(maxHp, hp + amount); }
    bool isAlive() const { return hp > 0; }

    void setPos(int nx, int ny) { x = nx; y = ny; }
    int getX() const { return x; }
    int getY() const { return y; }
    char getGlyph() const { return glyph; }
    const string& getName() const { return name; }
    int getHp() const { return hp; }
    int getMaxHp() const { return maxHp; }
    int getAtk() const { return atk; }
    int getDef() const { return def; }
};

class Player : public Entity {
    int level = 1, xp = 0, potions = 2;

public:
    Player() : Entity("You", '@', 40, 6, 1) {}

    int attack(bool& crit) const {
        crit = rnd(1, 100) <= 10;
        int dmg = atk + rnd(-1, 2);
        return std::max(1, crit ? dmg * 2 : dmg);
    }

    bool gainXp(int amount) {  // true on level up
        xp += amount;
        bool up = false;
        while (xp >= xpNeeded()) {
            xp -= xpNeeded();
            ++level;
            maxHp += 8;
            atk += 2;
            hp = maxHp;
            up = true;
        }
        return up;
    }

    bool drinkPotion() {
        if (potions <= 0 || hp == maxHp) return false;
        --potions;
        heal(30);
        return true;
    }

    void addPotion() { ++potions; }
    void addAtk(int n) { atk += n; }
    void addDef(int n) { def += n; }
    int getPotions() const { return potions; }
    int getLevel() const { return level; }
    int getXp() const { return xp; }
    int xpNeeded() const { return level * 25; }
};

class Item {
public:
    virtual ~Item() = default;
    virtual char glyph() const = 0;
    virtual string pickup(Player& p) = 0;
};

class HealthPotion : public Item {
public:
    char glyph() const override { return '!'; }
    string pickup(Player& p) override {
        p.addPotion();
        return "You pick up a health potion. (press H to drink)";
    }
};

class SwordUpgrade : public Item {
public:
    char glyph() const override { return '/'; }
    string pickup(Player& p) override {
        p.addAtk(2);
        return "You find a sharper blade! Attack +2.";
    }
};

class ShieldUpgrade : public Item {
public:
    char glyph() const override { return '['; }
    string pickup(Player& p) override {
        p.addDef(1);
        return "You find sturdy armor! Defense +1.";
    }
};

struct GroundItem {
    int x, y;
    unique_ptr<Item> item;
};

class Level;

class Enemy : public Entity {
protected:
    int xpReward;

    virtual bool canMoveThisTurn() { return true; }
    virtual string verb() const { return "hits"; }

    void attackPlayer(Player& p, Log& log) {
        int dealt = p.takeDamage(std::max(1, atk + rnd(-1, 1)));
        log.add("The " + name + " " + verb() + " you for " + to_string(dealt) + ".");
    }

public:
    Enemy(string n, char g, int h, int a, int d, int xp)
        : Entity(std::move(n), g, h, a, d), xpReward(xp) {}
    int getXpReward() const { return xpReward; }

    virtual void act(Level& lv, Player& p, Log& log);
};

class Goblin : public Enemy {
public:
    explicit Goblin(int f) : Enemy("Goblin", 'g', 14 + 4 * (f - 1), 4 + (f - 1), 0, 10) {}
};

class Skeleton : public Enemy {
protected:
    string verb() const override { return "slashes"; }

public:
    explicit Skeleton(int f) : Enemy("Skeleton", 's', 20 + 5 * (f - 1), 6 + (f - 1), 1, 18) {}
};

class Orc : public Enemy {
    bool moveNow = false;

protected:
    bool canMoveThisTurn() override {  // slow: moves every other turn
        moveNow = !moveNow;
        return moveNow;
    }
    string verb() const override { return "smashes"; }

public:
    explicit Orc(int f) : Enemy("Orc", 'O', 34 + 6 * (f - 1), 8 + (f - 1), 2, 30) {}
};

class Dragon : public Enemy {
    int turn = 0;

protected:
    string verb() const override { return "claws"; }

public:
    Dragon() : Enemy("Dragon", 'D', 90, 12, 4, 0) {}
    void act(Level& lv, Player& p, Log& log) override;
};

unique_ptr<Enemy> spawnEnemy(int floorNum) {
    int roll = rnd(1, 100);
    if (floorNum == 1 || roll <= 35) return std::make_unique<Goblin>(floorNum);
    if (floorNum == 2 || roll <= 70) return std::make_unique<Skeleton>(floorNum);
    return std::make_unique<Orc>(floorNum);
}

struct Rect {
    int x, y, w, h;
    std::pair<int, int> center() const { return {x + w / 2, y + h / 2}; }
};

class Level {
public:
    vector<string> grid;
    vector<Rect> rooms;
    vector<unique_ptr<Enemy>> enemies;
    vector<GroundItem> items;
    int startX = 0, startY = 0;

    explicit Level(int floorNum) : grid(MAP_H, string(MAP_W, '#')) {
        generateRooms();
        startX = rooms.front().center().first;
        startY = rooms.front().center().second;

        auto last = rooms.back().center();
        if (floorNum < MAX_FLOOR) {
            grid[last.second][last.first] = '>';
        } else {
            auto boss = std::make_unique<Dragon>();
            boss->setPos(last.first, last.second);
            enemies.push_back(std::move(boss));
        }

        for (size_t r = 1; r < rooms.size(); ++r) {
            int count = rnd(1, 2) + (floorNum >= 3 ? 1 : 0);
            for (int i = 0; i < count; ++i) {
                int ex, ey;
                randomCell(rooms[r], ex, ey);
                if (grid[ey][ex] == '>' || enemyAt(ex, ey)) continue;
                auto e = spawnEnemy(floorNum);
                e->setPos(ex, ey);
                enemies.push_back(std::move(e));
            }
            if (rnd(1, 100) <= 55) {
                GroundItem gi;
                randomCell(rooms[r], gi.x, gi.y);
                if (grid[gi.y][gi.x] == '>') continue;
                int k = rnd(1, 100);
                if (k <= 50) gi.item = std::make_unique<HealthPotion>();
                else if (k <= 75) gi.item = std::make_unique<SwordUpgrade>();
                else gi.item = std::make_unique<ShieldUpgrade>();
                items.push_back(std::move(gi));
            }
        }
    }

    bool isFloor(int x, int y) const {
        return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && grid[y][x] != '#';
    }

    Enemy* enemyAt(int x, int y) {
        for (auto& e : enemies)
            if (e->getX() == x && e->getY() == y) return e.get();
        return nullptr;
    }

    bool canEnter(int x, int y, const Player& p) {
        return isFloor(x, y) && !(p.getX() == x && p.getY() == y) && !enemyAt(x, y);
    }

    void removeDead() {
        enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
                                     [](const unique_ptr<Enemy>& e) { return !e->isAlive(); }),
                      enemies.end());
    }

private:
    void generateRooms() {
        while (rooms.size() < 5) {
            rooms.clear();
            grid.assign(MAP_H, string(MAP_W, '#'));
            for (int attempt = 0; attempt < 60; ++attempt) {
                int w = rnd(5, 11), h = rnd(3, 6);
                Rect r{rnd(1, MAP_W - w - 2), rnd(1, MAP_H - h - 2), w, h};
                bool overlaps = false;
                for (auto& o : rooms)
                    if (r.x <= o.x + o.w + 1 && r.x + r.w + 1 >= o.x &&
                        r.y <= o.y + o.h + 1 && r.y + r.h + 1 >= o.y)
                        overlaps = true;
                if (overlaps) continue;

                for (int yy = r.y; yy < r.y + r.h; ++yy)
                    for (int xx = r.x; xx < r.x + r.w; ++xx) grid[yy][xx] = '.';
                if (!rooms.empty()) tunnel(rooms.back().center(), r.center());
                rooms.push_back(r);
            }
        }
    }

    void tunnel(std::pair<int, int> a, std::pair<int, int> b) {
        for (int xx = std::min(a.first, b.first); xx <= std::max(a.first, b.first); ++xx)
            grid[a.second][xx] = '.';
        for (int yy = std::min(a.second, b.second); yy <= std::max(a.second, b.second); ++yy)
            grid[yy][b.first] = '.';
    }

    void randomCell(const Rect& r, int& cx, int& cy) {
        cx = rnd(r.x, r.x + r.w - 1);
        cy = rnd(r.y, r.y + r.h - 1);
    }
};

void Enemy::act(Level& lv, Player& p, Log& log) {
    int dx = p.getX() - x, dy = p.getY() - y;
    int dist = std::abs(dx) + std::abs(dy);

    if (dist == 1) {
        attackPlayer(p, log);
        return;
    }
    if (!canMoveThisTurn()) return;

    int mx = 0, my = 0;
    if (dist <= 8) {
        int sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
        bool horizontalFirst = std::abs(dx) >= std::abs(dy);
        int firstX = horizontalFirst ? sx : 0, firstY = horizontalFirst ? 0 : sy;
        int secondX = horizontalFirst ? 0 : sx, secondY = horizontalFirst ? sy : 0;
        if ((firstX || firstY) && lv.canEnter(x + firstX, y + firstY, p)) { mx = firstX; my = firstY; }
        else if ((secondX || secondY) && lv.canEnter(x + secondX, y + secondY, p)) { mx = secondX; my = secondY; }
    } else if (rnd(1, 100) <= 30) {
        static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
        int d = rnd(0, 3);
        if (lv.canEnter(x + DX[d], y + DY[d], p)) { mx = DX[d]; my = DY[d]; }
    }
    x += mx;
    y += my;
}

void Dragon::act(Level& lv, Player& p, Log& log) {
    ++turn;
    int dist = std::abs(p.getX() - x) + std::abs(p.getY() - y);
    if (dist > 1 && dist <= 5 && turn % 3 == 0) {  // ranged fire breath
        int dealt = p.takeDamage(atk * 2);
        log.add("The Dragon BREATHES FIRE at you for " + to_string(dealt) + "!");
        return;
    }
    Enemy::act(lv, p, log);
}

class Game {
    Player player;
    unique_ptr<Level> level;
    Log log;
    int floorNum = 1;
    bool running = true;
    bool won = false;

    bool nearFloor(int x, int y) const {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (level->isFloor(x + dx, y + dy)) return true;
        return false;
    }

    void newFloor() {
        level = std::make_unique<Level>(floorNum);
        player.setPos(level->startX, level->startY);
    }

    static string sprite(char c) {
        if (USE_EMOJI && term::ansi) {
            switch (c) {
                case '@': return "\xF0\x9F\xA7\x99";  
                case 'g': return "\xF0\x9F\x91\xBA";  
                case 's': return "\xF0\x9F\x92\x80";  
                case 'O': return "\xF0\x9F\x91\xB9";  
                case 'D': return "\xF0\x9F\x90\x89"; 
            }
        }
        return string(1, c) + ' ';
    }

    static int fgFor(char c) {
        switch (c) {
            case '@': return 46;   
            case 'g': return 203;  
            case 's': return 255;  
            case 'O': return 208;  
            case 'D': return 201;  
            case '!': return 213;  
            case '/': case '[': return 220;  
            case '>': return 51;   
            default: return -1;
        }
    }

    void render() {
        const int WALL = 245, FLOOR = 236;
        term::home();
        string out;
        const string eol = term::ansi ? "\x1b[K\n" : "\n"; 
        for (int yy = 0; yy < MAP_H; ++yy) {
            for (int xx = 0; xx < MAP_W; ++xx) {
                char tile = level->grid[yy][xx];

                if (tile == '#') {  
                    if (!nearFloor(xx, yy)) out += "  ";
                    else if (term::ansi) out += term::paint("  ", -1, WALL);
                    else out += "##";
                    continue;
                }

                char c = tile == '>' ? '>' : (term::ansi ? ' ' : '.');
                for (auto& gi : level->items)
                    if (gi.x == xx && gi.y == yy) c = gi.item->glyph();
                if (Enemy* e = level->enemyAt(xx, yy)) c = e->getGlyph();
                if (player.getX() == xx && player.getY() == yy) c = '@';
                out += term::paint(sprite(c), fgFor(c), FLOOR);
            }
            out += eol;
        }

        int hp = player.getHp(), maxHp = player.getMaxHp();
        int filled = (hp * 10 + maxHp - 1) / maxHp;
        int barColor = hp * 2 > maxHp ? 46 : (hp * 4 > maxHp ? 220 : 196);
        string bar;
        for (int i = 0; i < 10; ++i)
            bar += i < filled ? term::paint('=', barColor) : term::paint('-', 240);

        out += string(MAP_W * 2, '-') + eol;
        out += "Floor " + to_string(floorNum) + "/" + to_string(MAX_FLOOR) +
               "   HP [" + bar + "] " + to_string(hp) + "/" + to_string(maxHp) +
               "   Lv " + to_string(player.getLevel()) +
               " (XP " + to_string(player.getXp()) + "/" + to_string(player.xpNeeded()) + ")" + eol +
               "ATK " + to_string(player.getAtk()) + "   DEF " + to_string(player.getDef()) +
               "   Potions " + to_string(player.getPotions()) + eol +
               string(MAP_W * 2, '-') + eol;
        for (int i = 0; i < 3; ++i)  
            out += (i < (int)log.get().size() ? log.get()[i] : string()) + eol;
        out += "[WASD] move/attack  [H] potion  [.] wait  [Q] quit" + eol;
        if (term::ansi) out += "\x1b[J"; 
        std::cout << out << std::flush;
    }

    bool tryMove(int dx, int dy) { 
        int nx = player.getX() + dx, ny = player.getY() + dy;

        if (Enemy* e = level->enemyAt(nx, ny)) {
            bool crit;
            int dealt = e->takeDamage(player.attack(crit));
            log.add(string(crit ? "CRITICAL! " : "") + "You hit the " + e->getName() +
                    " for " + to_string(dealt) + ".");
            if (!e->isAlive()) {
                log.add("The " + e->getName() + " dies. (+" + to_string(e->getXpReward()) + " XP)");
                if (e->getGlyph() == 'D') {
                    won = true;
                    running = false;
                }
                if (player.gainXp(e->getXpReward()))
                    log.add("*** LEVEL UP! You are now level " + to_string(player.getLevel()) + " ***");
                level->removeDead();
            }
            return true;
        }

        if (!level->isFloor(nx, ny)) return false;

        player.setPos(nx, ny);

        for (size_t i = 0; i < level->items.size(); ++i) {
            if (level->items[i].x == nx && level->items[i].y == ny) {
                log.add(level->items[i].item->pickup(player));
                level->items.erase(level->items.begin() + i);
                break;
            }
        }

        if (level->grid[ny][nx] == '>') {
            ++floorNum;
            player.heal(10);
            newFloor();
            log.add("You descend to floor " + to_string(floorNum) + ". (+10 HP)");
        }
        return true;
    }

    void enemiesTurn() {
        for (auto& e : level->enemies) {
            e->act(*level, player, log);
            if (!player.isAlive()) {
                running = false;
                return;
            }
        }
    }

    void finish(const string& msg) {
        render();
        std::cout << "\n==== " << msg << " ====\n";
    }

public:
    Game() { newFloor(); }

    void run() {
        log.add("You enter the dungeon. Find the stairs '>' on each floor.");
        while (running) {
            render();
            int key = term::getKey();
            if (key == -1) return;

            bool turnUsed = false;
            switch (std::tolower(key)) {
                case 'w': turnUsed = tryMove(0, -1); break;
                case 's': turnUsed = tryMove(0, 1); break;
                case 'a': turnUsed = tryMove(-1, 0); break;
                case 'd': turnUsed = tryMove(1, 0); break;
                case '.': turnUsed = true; break;
                case 'h':
                    if (player.drinkPotion()) {
                        log.add("You drink a potion. (+30 HP)");
                        turnUsed = true;
                    } else {
                        log.add(player.getPotions() == 0 ? "You have no potions."
                                                         : "You are already at full health.");
                    }
                    break;
                case 'q':
                    finish("You abandon the quest.");
                    return;
            }
            if (turnUsed && running) enemiesTurn();
        }

        if (won) finish("The Dragon falls! YOU WIN!");
        else finish("You died on floor " + to_string(floorNum) + ". GAME OVER.");
    }
};

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    term::init();
    Game game;
    game.run();
    term::shutdown();
    return 0;
}

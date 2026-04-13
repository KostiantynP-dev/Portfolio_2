#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <stdexcept>
#include <map>

// ---------------------------------------------------------------------------
// Tile symbols (display)
// ---------------------------------------------------------------------------
namespace Tiles {
    const char EMPTY = ' ';
    const char WALL = '#';
    const char PLAYER = '@';
    const char GOAL = 'X';
    const char KEY = 'K';
    const char DOOR = 'L';
    const char ENEMY = 'E';
    const char HEALTH_POTION = 'H';
    const char STRENGTH_POTION = 'S';
    const char DEFENSE_POTION = 'G';
    const char HAZARD = '~';  // damaging floor (walkable; type from metadata)
}

// ---------------------------------------------------------------------------
// Cell model (kind + per-instance values)
// ---------------------------------------------------------------------------
enum class TileKind {
    Empty,
    Wall,
    PlayerSpawn,
    Goal,
    Key,
    Door,
    Enemy,
    HealthPotion,
    StrengthPotion,
    DefensePotion,
    DamagingFloor
};

/** Global hazard type -> damage (all tiles of that type use the same damage). */
inline int hazardDamageForType(const std::string& type) {
    std::string t;
    for (char ch : type) {
        t += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (t == "poison") return 2;
    if (t == "lava") return 5;
    if (t == "spikes") return 3;
    return 3;  // sensible default for unknown editor types
}

inline const std::vector<std::string>& hazardTypeList() {
    static const std::vector<std::string> k = { "poison", "lava", "spikes" };
    return k;
}

struct CellInstance {
    TileKind kind = TileKind::Empty;
    /** For Key/Door: matching strings open the door (e.g. "blue"). */
    std::string lockType;
    int enemyHp = 20;
    int enemyStr = 8;
    int enemyDef = 2;
    /** Potion stat bonus (HP / Str / Def depending on kind). */
    int potionBonus = 10;
    /** Damaging floor category; damage from hazardDamageForType. */
    std::string hazardType = "spikes";

    char displayChar() const {
        switch (kind) {
        case TileKind::Empty: return Tiles::EMPTY;
        case TileKind::Wall: return Tiles::WALL;
        case TileKind::PlayerSpawn: return Tiles::PLAYER;
        case TileKind::Goal: return Tiles::GOAL;
        case TileKind::Key: return Tiles::KEY;
        case TileKind::Door: return Tiles::DOOR;
        case TileKind::Enemy: return Tiles::ENEMY;
        case TileKind::HealthPotion: return Tiles::HEALTH_POTION;
        case TileKind::StrengthPotion: return Tiles::STRENGTH_POTION;
        case TileKind::DefensePotion: return Tiles::DEFENSE_POTION;
        case TileKind::DamagingFloor: return Tiles::HAZARD;
        }
        return Tiles::EMPTY;
    }

    static CellInstance fromLegacyChar(char ch) {
        CellInstance c;
        switch (ch) {
        case Tiles::EMPTY:
        case Tiles::PLAYER:
            c.kind = (ch == Tiles::PLAYER) ? TileKind::PlayerSpawn : TileKind::Empty;
            break;
        case Tiles::WALL: c.kind = TileKind::Wall; break;
        case Tiles::GOAL: c.kind = TileKind::Goal; break;
        case Tiles::KEY: c.kind = TileKind::Key; c.lockType = "default"; break;
        case Tiles::DOOR: c.kind = TileKind::Door; c.lockType = "default"; break;
        case Tiles::ENEMY: c.kind = TileKind::Enemy; break;
        case Tiles::HEALTH_POTION: c.kind = TileKind::HealthPotion; c.potionBonus = 10; break;
        case Tiles::STRENGTH_POTION: c.kind = TileKind::StrengthPotion; c.potionBonus = 5; break;
        case Tiles::DEFENSE_POTION: c.kind = TileKind::DefensePotion; c.potionBonus = 5; break;
        case Tiles::HAZARD:
            c.kind = TileKind::DamagingFloor;
            c.hazardType = "spikes";
            break;
        default:
            c.kind = TileKind::Empty;
            break;
        }
        return c;
    }
};

// ---------------------------------------------------------------------------
// Player
// ---------------------------------------------------------------------------
class Player {
public:
    Player() : health(100), strength(10), defense(5) {}

    int getHealth() const { return health; }
    int getStrength() const { return strength; }
    int getDefense() const { return defense; }

    void setHealth(int value) { health = value; }
    void addHealth(int amount) { health += amount; }
    void addStrength(int amount) { strength += amount; }
    void addDefense(int amount) { defense += amount; }

    void addKey(const std::string& type) {
        std::string t = normalizeKeyType(type);
        keyRing[t]++;
    }

    bool useKey(const std::string& doorType) {
        std::string t = normalizeKeyType(doorType);
        auto it = keyRing.find(t);
        if (it == keyRing.end() || it->second <= 0) return false;
        it->second--;
        if (it->second <= 0) keyRing.erase(it);
        return true;
    }

    int countKey(const std::string& type) const {
        std::string t = normalizeKeyType(type);
        auto it = keyRing.find(t);
        return it == keyRing.end() ? 0 : it->second;
    }

    const std::map<std::string, int>& getKeyRing() const { return keyRing; }

    std::string keysDescription() const {
        if (keyRing.empty()) return "none";
        std::string s;
        for (const auto& kv : keyRing) {
            if (!s.empty()) s += ", ";
            s += kv.first + " x" + std::to_string(kv.second);
        }
        return s;
    }

    bool isAlive() const { return health > 0; }

private:
    static std::string normalizeKeyType(std::string t) {
        std::string out;
        for (char ch : t) {
            if (!std::isspace(static_cast<unsigned char>(ch)))
                out += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return out.empty() ? std::string("default") : out;
    }

    int health;
    int strength;
    int defense;
    std::map<std::string, int> keyRing;
};

// ---------------------------------------------------------------------------
// Dungeon
// ---------------------------------------------------------------------------
class Dungeon {
public:
    Dungeon(int rows, int cols) : playerRow(0), playerCol(0) {
        if (rows <= 0 || cols <= 0)
            throw std::invalid_argument("Rows and columns must be positive.");
        cells.assign(rows, std::vector<CellInstance>(cols));
    }

    explicit Dungeon(const std::vector<std::string>& gridIn) : playerRow(0), playerCol(0) {
        if (gridIn.empty())
            throw std::invalid_argument("Grid cannot be empty.");
        const size_t numCols = gridIn[0].size();
        for (const auto& row : gridIn) {
            if (row.size() != numCols)
                throw std::invalid_argument("All rows must have the same length.");
        }
        cells.resize(gridIn.size());
        for (size_t r = 0; r < gridIn.size(); r++) {
            cells[r].resize(numCols);
            for (size_t c = 0; c < numCols; c++) {
                char ch = gridIn[r][c];
                cells[r][c] = CellInstance::fromLegacyChar(ch);
                if (ch == Tiles::PLAYER) {
                    playerRow = static_cast<int>(r);
                    playerCol = static_cast<int>(c);
                    cells[r][c].kind = TileKind::Empty;
                }
            }
        }
    }

    int getRows() const { return static_cast<int>(cells.size()); }
    int getCols() const { return cells.empty() ? 0 : static_cast<int>(cells[0].size()); }

    char getCell(int row, int col) const {
        if (!isInBounds(row, col)) return Tiles::WALL;
        return cells[row][col].displayChar();
    }

    const CellInstance& getCellData(int row, int col) const {
        static const CellInstance kWall = [] {
            CellInstance w;
            w.kind = TileKind::Wall;
            return w;
            }();
        if (!isInBounds(row, col)) return kWall;
        return cells[row][col];
    }

    CellInstance& getCellDataMut(int row, int col) {
        if (!isInBounds(row, col))
            throw std::out_of_range("getCellDataMut");
        return cells[row][col];
    }

    void setCellData(int row, int col, CellInstance ci) {
        if (isInBounds(row, col)) cells[row][col] = std::move(ci);
    }

    void setCell(int row, int col, char tile) {
        if (!isInBounds(row, col)) return;
        cells[row][col] = CellInstance::fromLegacyChar(tile);
    }

    int getPlayerRow() const { return playerRow; }
    int getPlayerCol() const { return playerCol; }

    void setPlayerPosition(int row, int col) {
        if (isInBounds(row, col)) {
            playerRow = row;
            playerCol = col;
        }
    }

    bool isInBounds(int row, int col) const {
        return row >= 0 && row < getRows() && col >= 0 && col < getCols();
    }

    /** Deep copy for play; preserves player position without relying on '@' in grid. */
    Dungeon cloneForPlay() const {
        Dungeon d(getRows(), getCols());
        d.cells = cells;
        d.playerRow = playerRow;
        d.playerCol = playerCol;
        return d;
    }

private:
    std::vector<std::vector<CellInstance>> cells;
    int playerRow;
    int playerCol;
};

// ---------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------
enum class GameResult { InProgress, Victory, GameOver, Quit };

class Game {
public:
    explicit Game(Dungeon dungeon) : dungeon_(std::move(dungeon)), result_(GameResult::InProgress) {}

    void display() const {
        const int rows = dungeon_.getRows();
        const int cols = dungeon_.getCols();
        const int playerRow = dungeon_.getPlayerRow();
        const int playerCol = dungeon_.getPlayerCol();

        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                if (r == playerRow && c == playerCol)
                    std::cout << Tiles::PLAYER;
                else
                    std::cout << dungeon_.getCell(r, c);
            }
            std::cout << '\n';
        }
        std::cout << "Hero - HP: " << player_.getHealth()
            << " | Str: " << player_.getStrength()
            << " | Def: " << player_.getDefense()
            << "\nKeys held: " << player_.keysDescription() << '\n';
    }

    void processMove(const std::string& input) {
        std::string direction = normalizeDirection(input);
        if (direction.empty()) return;

        int rowDelta = 0;
        int colDelta = 0;
        if (direction == "left") colDelta = -1;
        else if (direction == "right") colDelta = 1;
        else if (direction == "up") rowDelta = -1;
        else if (direction == "down") rowDelta = 1;

        tryMove(rowDelta, colDelta);
    }

    /** Inspect a square by 1-based coordinates; prints detailed cell info. */
    void inspectSquare(int row1, int col1) const {
        const int r = row1 - 1;
        const int c = col1 - 1;
        if (!dungeon_.isInBounds(r, c)) {
            std::cout << "That square is outside the dungeon.\n";
            return;
        }
        std::cout << "Square (" << row1 << ", " << col1 << "):\n";
        describeCell(dungeon_.getCellData(r, c));
    }

    GameResult getResult() const { return result_; }
    void quit() { result_ = GameResult::Quit; }

    static std::string normalizeDirection(const std::string& input) {
        std::string trimmed;
        for (char ch : input) {
            if (!std::isspace(static_cast<unsigned char>(ch)))
                trimmed += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (trimmed == "left" || trimmed == "l" || trimmed == "a") return "left";
        if (trimmed == "right" || trimmed == "r" || trimmed == "d") return "right";
        if (trimmed == "up" || trimmed == "u" || trimmed == "w") return "up";
        if (trimmed == "down" || trimmed == "s") return "down";
        return "";
    }

    static bool isValidDirection(const std::string& input) {
        return !normalizeDirection(input).empty();
    }

    static void describeCell(const CellInstance& cell) {
        switch (cell.kind) {
        case TileKind::Empty:
            std::cout << "  Empty floor (walkable).\n";
            break;
        case TileKind::Wall:
            std::cout << "  Wall (blocked).\n";
            break;
        case TileKind::PlayerSpawn:
            std::cout << "  Player start marker.\n";
            break;
        case TileKind::Goal:
            std::cout << "  Goal — reach here to win.\n";
            break;
        case TileKind::Key:
            std::cout << "  Key (type: " << (cell.lockType.empty() ? "default" : cell.lockType) << ").\n";
            break;
        case TileKind::Door:
            std::cout << "  Locked door (needs key type: "
                << (cell.lockType.empty() ? "default" : cell.lockType) << ").\n";
            break;
        case TileKind::Enemy:
            std::cout << "  Enemy — HP: " << cell.enemyHp << ", Str: " << cell.enemyStr
                << ", Def: " << cell.enemyDef << ".\n";
            break;
        case TileKind::HealthPotion:
            std::cout << "  Health potion (+" << cell.potionBonus << " HP).\n";
            break;
        case TileKind::StrengthPotion:
            std::cout << "  Strength potion (+" << cell.potionBonus << " Str).\n";
            break;
        case TileKind::DefensePotion:
            std::cout << "  Defense potion (+" << cell.potionBonus << " Def).\n";
            break;
        case TileKind::DamagingFloor:
            std::cout << "  Damaging floor type: " << cell.hazardType
                << ", damage per step: " << hazardDamageForType(cell.hazardType) << ".\n";
            break;
        }
    }

private:
    static int damageDealt(int attackerStrength, int defenderDefense) {
        int damage = attackerStrength - defenderDefense;
        return damage > 0 ? damage : 0;
    }
    /** Editor stores start as PlayerSpawn, which draws as '@'; clear it when the hero walks away. */
    void clearPlayerSpawnAt(int row, int col) {
        CellInstance& here = dungeon_.getCellDataMut(row, col);
        if (here.kind == TileKind::PlayerSpawn) {
            here.kind = TileKind::Empty;
        }
    }
    bool runBattle(int enemyHp, int enemyStr, int enemyDef) {
        int eh = enemyHp;
        int playerHealth = player_.getHealth();
        const int playerStr = player_.getStrength();
        const int playerDef = player_.getDefense();

        int round = 1;
        while (true) {
            std::cout << "--- Round " << round << " ---\n";

            int dmg = damageDealt(playerStr, enemyDef);
            eh -= dmg;
            std::cout << "Your attack deals " << dmg << " damage. Enemy HP: "
                << (eh > 0 ? eh : 0) << ".\n";
            if (eh <= 0) {
                player_.setHealth(playerHealth);
                return true;
            }

            dmg = damageDealt(enemyStr, playerDef);
            playerHealth -= dmg;
            std::cout << "Enemy attack deals " << dmg << " damage. Your HP: "
                << (playerHealth > 0 ? playerHealth : 0) << ".\n";
            if (playerHealth <= 0)
                return false;

            round++;
        }
    }

    bool tryMove(int rowDelta, int colDelta) {
        const int oldRow = dungeon_.getPlayerRow();
        const int oldCol = dungeon_.getPlayerCol();
        const int newRow = oldRow + rowDelta;
        const int newCol = oldCol + colDelta;

        if (!dungeon_.isInBounds(newRow, newCol))
            return false;

        const CellInstance& cell = dungeon_.getCellData(newRow, newCol);

        if (cell.kind == TileKind::Wall) {
            std::cout << "You cannot walk through walls.\n";
            return false;
        }

        if (cell.kind == TileKind::Door) {
            std::string need = cell.lockType.empty() ? std::string("default") : cell.lockType;
            if (!player_.useKey(need)) {
                std::cout << "The door is locked. You need a " << need << " key.\n";
                return false;
            }
            std::cout << "You used a " << need << " key and opened the door.\n";
            CellInstance empty;
            empty.kind = TileKind::Empty;
            dungeon_.setCellData(newRow, newCol, empty);
            clearPlayerSpawnAt(oldRow, oldCol);
            dungeon_.setPlayerPosition(newRow, newCol);
            return true;
        }

        if (cell.kind == TileKind::Empty) {
            std::cout << "You move forward.\n";
            clearPlayerSpawnAt(oldRow, oldCol);
            dungeon_.setPlayerPosition(newRow, newCol);
            return true;
        }

        if (cell.kind == TileKind::DamagingFloor) {
            int dmg = hazardDamageForType(cell.hazardType);
            std::cout << "You step on " << cell.hazardType << " for " << dmg << " damage!\n";
            player_.addHealth(-dmg);
            if (!player_.isAlive()) {
                result_ = GameResult::GameOver;
                std::cout << "You have been defeated. Game Over.\n";
                return true;
            }
            clearPlayerSpawnAt(oldRow, oldCol);
            dungeon_.setPlayerPosition(newRow, newCol);
            return true;
        }

        if (cell.kind == TileKind::Key) {
            std::string kt = cell.lockType.empty() ? std::string("default") : cell.lockType;
            player_.addKey(kt);
            std::cout << "You picked up a " << kt << " key!\n";
            CellInstance empty;
            empty.kind = TileKind::Empty;
            dungeon_.setCellData(newRow, newCol, empty);
            clearPlayerSpawnAt(oldRow, oldCol);
            dungeon_.setPlayerPosition(newRow, newCol);
            return true;
        }

        if (cell.kind == TileKind::HealthPotion) {
            player_.addHealth(cell.potionBonus);
            std::cout << "You drank a health potion! Health +" << cell.potionBonus << ".\n";
            CellInstance empty;
            empty.kind = TileKind::Empty;
            dungeon_.setCellData(newRow, newCol, empty);
            clearPlayerSpawnAt(oldRow, oldCol);
            dungeon_.setPlayerPosition(newRow, newCol);
            return true;
        }

        if (cell.kind == TileKind::StrengthPotion) {
            player_.addStrength(cell.potionBonus);
            std::cout << "You drank a strength potion! Strength +" << cell.potionBonus << ".\n";
            CellInstance empty;
            empty.kind = TileKind::Empty;
            dungeon_.setCellData(newRow, newCol, empty);
            clearPlayerSpawnAt(oldRow, oldCol);
            dungeon_.setPlayerPosition(newRow, newCol);
            return true;
        }

        if (cell.kind == TileKind::DefensePotion) {
            player_.addDefense(cell.potionBonus);
            std::cout << "You drank a defense potion! Defense +" << cell.potionBonus << ".\n";
            CellInstance empty;
            empty.kind = TileKind::Empty;
            dungeon_.setCellData(newRow, newCol, empty);
            clearPlayerSpawnAt(oldRow, oldCol);
            dungeon_.setPlayerPosition(newRow, newCol);
            return true;
        }

        if (cell.kind == TileKind::Enemy) {
            std::cout << "An enemy blocks your path! Battle!\n";
            if (!runBattle(cell.enemyHp, cell.enemyStr, cell.enemyDef)) {
                result_ = GameResult::GameOver;
                std::cout << "You have been defeated. Game Over.\n";
                return true;
            }
            std::cout << "You won the battle!\n";
            CellInstance empty;
            empty.kind = TileKind::Empty;
            dungeon_.setCellData(newRow, newCol, empty);
            clearPlayerSpawnAt(oldRow, oldCol);
            dungeon_.setPlayerPosition(newRow, newCol);
            return true;
        }

        if (cell.kind == TileKind::Goal) {
            result_ = GameResult::Victory;
            std::cout << "You reached the goal! Victory!\n";
			clearPlayerSpawnAt(oldRow, oldCol);
            dungeon_.setPlayerPosition(newRow, newCol);
            return true;
        }

        if (cell.kind == TileKind::PlayerSpawn) {
            std::cout << "You move forward.\n";
			clearPlayerSpawnAt(oldRow, oldCol);
            dungeon_.setPlayerPosition(newRow, newCol);
            return true;
        }

        return false;
    }

    Dungeon dungeon_;
    Player player_;
    GameResult result_;
};

// ---------------------------------------------------------------------------
// Input helpers
// ---------------------------------------------------------------------------
static std::string collapseSpaces(const std::string& input) {
    std::string result;
    for (char ch : input) {
        if (!std::isspace(static_cast<unsigned char>(ch)))
            result += ch;
    }
    return result;
}

static std::string toLowerToken(std::string s) {
    std::string out;
    for (char ch : s) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

static std::string readLine() {
    std::string line;
    std::getline(std::cin, line);
    return line;
}

// ---------------------------------------------------------------------------
// Level Editor
// ---------------------------------------------------------------------------
class LevelEditor {
public:
    void run() {
        std::cout << "\nLevel editor: start with a new dungeon or load one?\n"
            << "  (type \"new\" or \"load\"): ";
        std::string start = readLine();
        std::string st = toLowerToken(collapseSpaces(start));

        Dungeon dungeon(5, 5);
        if (st == "load") {
            if (!loadDungeonInto(dungeon)) {
                std::cout << "Could not load; creating 5x5 empty dungeon instead.\n";
                dungeon = Dungeon(5, 5);
            }
        }
        else {
            int rows = 0, cols = 0;
            promptGridSize(rows, cols);
            dungeon = Dungeon(rows, cols);
        }

        while (true) {
            std::cout << "\n1) Add an object   2) Inspect square   3) Save dungeon\n"
                << "4) Return to main menu (discard unsaved changes is OK)\n"
                << "Choice (number or keyword): ";
            std::string line = readLine();
            std::string choice = toLowerToken(collapseSpaces(line));

            if (choice == "1" || choice == "add" || choice == "object") {
                addObject(dungeon);
            }
            else if (choice == "2" || choice == "inspect" || choice == "look") {
                inspectEditorSquare(dungeon);
            }
            else if (choice == "3" || choice == "save") {
                saveDungeon(dungeon);
            }
            else if (choice == "4" || choice == "exit" || choice == "back" || choice == "menu") {
                std::cout << "Leaving level editor.\n";
                return;
            }
            else {
                std::cout << "Invalid choice. Use from 1 to 4 or a keyword (add, inspect, save, exit).\n";
            }
        }
    }

    const std::vector<std::pair<std::string, Dungeon>>& getSavedDungeons() const { return savedDungeons; }

private:
    bool loadDungeonInto(Dungeon& out) {
        if (savedDungeons.empty()) {
            std::cout << "No saved dungeons to load.\n";
            return false;
        }
        std::cout << "Saved dungeons:\n";
        for (size_t i = 0; i < savedDungeons.size(); i++)
            std::cout << "  " << (i + 1) << ") " << savedDungeons[i].first << '\n';
        std::cout << "Enter number or exact name: ";
        std::string line = readLine();
        std::istringstream iss(line);
        int n = 0;
        if (iss >> n && n >= 1 && n <= static_cast<int>(savedDungeons.size())) {
            out = savedDungeons[static_cast<size_t>(n - 1)].second.cloneForPlay();
            std::cout << "Loaded \"" << savedDungeons[static_cast<size_t>(n - 1)].first << "\".\n";
            return true;
        }
        std::string target = toLowerToken(line);
        for (const auto& p : savedDungeons) {
            if (toLowerToken(p.first) == target) {
                out = p.second.cloneForPlay();
                std::cout << "Loaded \"" << p.first << "\".\n";
                return true;
            }
        }
        std::cout << "No dungeon matched.\n";
        return false;
    }

    void promptGridSize(int& rows, int& cols) {
        const int maxSize = 100;
        while (true) {
            std::cout << "Enter number of rows: ";
            std::string line = readLine();
            std::istringstream iss(line);
            if (!(iss >> rows) || rows <= 0 || rows > maxSize) {
                std::cout << "Invalid input. Enter a positive number (e.g. 5).\n";
                continue;
            }
            break;
        }
        while (true) {
            std::cout << "Enter number of columns: ";
            std::string line = readLine();
            std::istringstream iss(line);
            if (!(iss >> cols) || cols <= 0 || cols > maxSize) {
                std::cout << "Invalid input. Enter a positive number (e.g. 6).\n";
                continue;
            }
            break;
        }
    }

    void displayEditorGrid(const Dungeon& dungeon) const {
        const int numRows = dungeon.getRows();
        const int numCols = dungeon.getCols();
        const int rowLabelWidth = (numRows >= 10) ? 2 : 1;

        std::cout << std::string(rowLabelWidth, ' ');
        for (int c = 0; c < numCols; c++)
            std::cout << ' ' << (c + 1);
        std::cout << '\n';

        for (int r = 0; r < numRows; r++) {
            std::cout << std::setw(rowLabelWidth) << (r + 1);
            for (int c = 0; c < numCols; c++) {
                int pr = dungeon.getPlayerRow();
                int pc = dungeon.getPlayerCol();
                char ch = (r == pr && c == pc) ? Tiles::PLAYER : dungeon.getCell(r, c);
                std::cout << ' ' << ch;
            }
            std::cout << '\n';
        }
    }

    static int objectMenuChoice() {
        std::cout << "Select object (number or name):\n"
            << " 1) Empty   2) Wall   3) Player start   4) Goal   5) Key\n"
            << " 6) Door    7) Enemy  8) Health potion  9) Strength potion\n"
            << " 10) Defense potion   11) Damaging floor\n"
            << "Choice: ";
        std::string line = readLine();
        std::string s = toLowerToken(collapseSpaces(line));
        if (s.empty()) return -1;

        if (s == "1" || s == "empty" || s == "space") return 1;
        if (s == "2" || s == "wall") return 2;
        if (s == "3" || s == "player" || s == "start") return 3;
        if (s == "4" || s == "goal") return 4;
        if (s == "5" || s == "key") return 5;
        if (s == "6" || s == "door" || s == "locked") return 6;
        if (s == "7" || s == "enemy") return 7;
        if (s == "8" || s == "health" || s == "hp" || s == "healthpotion") return 8;
        if (s == "9" || s == "strength" || s == "str" || s == "strengthpotion") return 9;
        if (s == "10" || s == "defense" || s == "def" || s == "defensepotion") return 10;
        if (s == "11" || s == "hazard" || s == "damaging" || s == "damagingfloor" || s == "floor") return 11;
        return -1;
    }

    static bool parseCoordinates(const std::string& input, int numRows, int numCols, int& r, int& c) {
        std::istringstream iss(input);
        int rowNum = 0, colNum = 0;
        if (!(iss >> rowNum >> colNum)) return false;
        if (rowNum < 1 || rowNum > numRows || colNum < 1 || colNum > numCols) return false;
        r = rowNum - 1;
        c = colNum - 1;
        return true;
    }

    static bool isStopCommand(const std::string& line) {
        return toLowerToken(collapseSpaces(line)) == "stop";
    }

    static int readIntLine(const std::string& prompt, int minV, int maxV) {
        while (true) {
            std::cout << prompt;
            std::string line = readLine();
            std::istringstream iss(line);
            int v = 0;
            if (iss >> v && v >= minV && v <= maxV) return v;
            std::cout << "Invalid. Enter a number between " << minV << " and " << maxV << ".\n";
        }
    }

    static std::string promptKeyType() {
        std::cout << "Key/door type (e.g. blue, red, default): ";
        std::string t = readLine();
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(0, 1);
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
        if (t.empty()) return "default";
        return toLowerToken(t);
    }

    static std::string promptHazardTypeMenu() {
        const auto& types = hazardTypeList();
        std::cout << "Damaging floor types:\n";
        for (size_t i = 0; i < types.size(); i++)
            std::cout << "  " << (i + 1) << ") " << types[i]
            << " (" << hazardDamageForType(types[i]) << " damage per step)\n";
        std::cout << "Enter number or type name: ";
        std::string line = readLine();
        std::istringstream iss(line);
        int n = 0;
        if (iss >> n && n >= 1 && n <= static_cast<int>(types.size()))
            return types[static_cast<size_t>(n - 1)];
        std::string tok = toLowerToken(collapseSpaces(line));
        for (const auto& t : types) {
            if (toLowerToken(t) == tok) return t;
        }
        if (!tok.empty()) return tok;
        return "spikes";
    }

    CellInstance buildCellForChoice(int choice) {
        CellInstance ci;
        switch (choice) {
        case 1: ci.kind = TileKind::Empty; break;
        case 2: ci.kind = TileKind::Wall; break;
        case 3: ci.kind = TileKind::PlayerSpawn; break;
        case 4: ci.kind = TileKind::Goal; break;
        case 5:
            ci.kind = TileKind::Key;
            ci.lockType = promptKeyType();
            break;
        case 6:
            ci.kind = TileKind::Door;
            ci.lockType = promptKeyType();
            break;
        case 7:
            ci.kind = TileKind::Enemy;
            std::cout << "Enemy HP Str Def (three integers, e.g. 20 8 2): ";
            {
                std::string line = readLine();
                std::istringstream iss(line);
                if (!(iss >> ci.enemyHp >> ci.enemyStr >> ci.enemyDef)) {
                    ci.enemyHp = 20;
                    ci.enemyStr = 8;
                    ci.enemyDef = 2;
                    std::cout << "Using defaults 20 8 2.\n";
                }
            }
            break;
        case 8:
            ci.kind = TileKind::HealthPotion;
            ci.potionBonus = readIntLine("Health bonus amount (e.g. 10): ", 1, 999);
            break;
        case 9:
            ci.kind = TileKind::StrengthPotion;
            ci.potionBonus = readIntLine("Strength bonus amount (e.g. 5): ", 1, 999);
            break;
        case 10:
            ci.kind = TileKind::DefensePotion;
            ci.potionBonus = readIntLine("Defense bonus amount (e.g. 5): ", 1, 999);
            break;
        case 11:
            ci.kind = TileKind::DamagingFloor;
            ci.hazardType = promptHazardTypeMenu();
            break;
        default:
            ci.kind = TileKind::Empty;
            break;
        }
        return ci;
    }

    void inspectEditorSquare(Dungeon& dungeon) {
        displayEditorGrid(dungeon);
        std::cout << "Inspect: enter row and column (1-based, e.g. 2 3): ";
        std::string line = readLine();
        int r = 0, c = 0;
        if (!parseCoordinates(line, dungeon.getRows(), dungeon.getCols(), r, c)) {
            std::cout << "Invalid coordinates.\n";
            return;
        }
        std::cout << "Contents of (" << (r + 1) << ", " << (c + 1) << "):\n";
        std::cout << "  Symbol: '" << dungeon.getCell(r, c) << "'\n";
        Game::describeCell(dungeon.getCellData(r, c));
    }

    void addObject(Dungeon& dungeon) {
        int choice = objectMenuChoice();
        if (choice < 1 || choice > 11) {
            std::cout << "Invalid choice. Try again.\n";
            return;
        }

        CellInstance cell = buildCellForChoice(choice);

        if (choice == 2) {
            while (true) {
                displayEditorGrid(dungeon);
                std::cout << "Wall: row column (1-based), or 'stop' when done: ";
                std::string line = readLine();
                if (isStopCommand(line)) break;

                int row = 0, col = 0;
                if (!parseCoordinates(line, dungeon.getRows(), dungeon.getCols(), row, col)) {
                    std::cout << "Invalid coordinates.\n";
                    continue;
                }
                CellInstance w;
                w.kind = TileKind::Wall;
                dungeon.setCellData(row, col, w);
                std::cout << "Wall placed.\n";
            }
            std::cout << "Finished placing walls.\n";
            return;
        }

        displayEditorGrid(dungeon);
        std::cout << "Enter row and column (e.g. 2 3): ";
        std::string line = readLine();
        int row = 0, col = 0;
        if (!parseCoordinates(line, dungeon.getRows(), dungeon.getCols(), row, col)) {
            std::cout << "Invalid coordinates.\n";
            return;
        }

        dungeon.setCellData(row, col, cell);
        if (cell.kind == TileKind::PlayerSpawn)
            dungeon.setPlayerPosition(row, col);

        std::cout << "Object placed.\n";
        displayEditorGrid(dungeon);
    }

    void saveDungeon(const Dungeon& dungeon) {
        std::cout << "Enter a name for this dungeon: ";
        std::string name = readLine();
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(0, 1);
        if (name.empty()) {
            std::cout << "Name cannot be empty. Using 'Unnamed Dungeon'.\n";
            name = "Unnamed Dungeon";
        }

        // Keep full cell metadata (stats, key types, hazards); do not round-trip through ASCII.
        Dungeon stored = dungeon.cloneForPlay();

        for (auto& p : savedDungeons) {
            if (toLowerToken(p.first) == toLowerToken(name)) {
                p.first = name;
                p.second = std::move(stored);
                std::cout << "Dungeon \"" << name << "\" updated.\n";
                return;
            }
        }
        savedDungeons.push_back({ std::move(name), std::move(stored) });
        std::cout << "Dungeon saved.\n";
    }

    std::vector<std::pair<std::string, Dungeon>> savedDungeons;
};

// ---------------------------------------------------------------------------
// Legend & sample levels
// ---------------------------------------------------------------------------
static void printSymbolLegend() {
    std::cout
        << "\n--- Map legend ---\n"
        << "  " << Tiles::PLAYER << "  You\n"
        << "  " << Tiles::WALL << "  Wall (blocked)\n"
        << "  " << Tiles::EMPTY << "  Empty floor\n"
        << "  " << Tiles::GOAL << "  Goal\n"
        << "  " << Tiles::KEY << "  Key (type shown when you inspect or pick up)\n"
        << "  " << Tiles::DOOR << "  Locked door (needs matching key type)\n"
        << "  " << Tiles::ENEMY << "  Enemy (stats vary — inspect to see)\n"
        << "  " << Tiles::HEALTH_POTION << "  Health potion\n"
        << "  " << Tiles::STRENGTH_POTION << "  Strength potion\n"
        << "  " << Tiles::DEFENSE_POTION << "  Defense potion\n"
        << "  " << Tiles::HAZARD << "  Damaging floor (type/damage vary — inspect)\n"
        << "------------------\n\n";
}

static std::vector<std::string> tutorialGrid() {
    return {
        "#######",
        "#  K  #",
        "# @ #L#",
        "#  #X #",
        "#######"
    };
}

static std::vector<std::string> level1Grid() {
    return {
        "   #####  ",
        " ###   ###",
        " #  E#  H#",
        " # ~ ##L##",
        " #K  @#X##",
        " #########"
    };
}

// ---------------------------------------------------------------------------
// Main menu & game loop
// ---------------------------------------------------------------------------
static void runGameLoop(Game& game) {
    printSymbolLegend();
    bool first = true;
    while (game.getResult() == GameResult::InProgress) {
        if (!first)
            std::cout << '\n';
        first = false;
        game.display();
        std::cout << "Move: W/A/S/D or arrows (U/D/L/R). Inspect: \"inspect #row #col\" (1-based). Quit dungeon: quit\n"
            << "> ";

        std::string input = readLine();
        if (input.empty()) continue;

        std::string low = toLowerToken(input);
        if (low == "quit" || low == "q" || low == "exit") {
            game.quit();
            std::cout << "You leave the dungeon.\n";
            break;
        }

        std::istringstream insp(input);
        std::string cmd;
        insp >> cmd;
        std::string cmdLow = toLowerToken(cmd);
        if (cmdLow == "inspect" || cmdLow == "i") {
            int r = 0, c = 0;
            if (!(insp >> r >> c))
                std::cout << "Usage: inspect <row> <col> (numbers are 1-based).\n";
            else
                game.inspectSquare(r, c);
            continue;
        }

        if (!Game::isValidDirection(input)) {
            std::cout << "Unknown command. Use WASD or U/D/L/R, inspect row col, or quit.\n";
            continue;
        }
        game.processMove(input);
    }
}

static int getMainMenuChoice() {
    while (true) {
        std::cout << "\nMain menu (number or keyword):\n"
            << " 1) Enter a dungeon  (play / enter / dungeon)\n"
            << " 2) Design a dungeon  (design / editor)\n"
            << " 3) Exit\n"
            << "Choice: ";
        std::string line = readLine();
        std::string t = toLowerToken(collapseSpaces(line));

        if (t == "1" || t == "play" || t == "enter" || t == "dungeon" || t == "start")
            return 1;
        if (t == "2" || t == "design" || t == "editor" || t == "build")
            return 2;
        if (t == "3" || t == "exit" || t == "quit" || t == "q")
            return 3;
        std::cout << "Invalid input. Try from 1 to 3 or a keyword like play, editor, exit.\n";
    }
}

static int getDungeonChoice(const std::vector<std::pair<std::string, Dungeon>>& allDungeons) {
    const int count = static_cast<int>(allDungeons.size());
    while (true) {
        std::cout << "Pick a dungeon by number or name: ";
        std::string line = readLine();
        std::istringstream iss(line);
        int number = 0;
        if (iss >> number && number >= 1 && number <= count)
            return number - 1;

        std::string target = toLowerToken(line);
        for (int i = 0; i < count; i++) {
            if (toLowerToken(allDungeons[static_cast<size_t>(i)].first) == target)
                return i;
        }
        for (int i = 0; i < count; i++) {
            const std::string& n = allDungeons[static_cast<size_t>(i)].first;
            if (toLowerToken(n).find(target) != std::string::npos)
                return i;
        }
        std::cout << "No match. Enter from 1 to " << count << " or part of the dungeon name.\n";
    }
}

int main() {
    std::cout << "Welcome to Magic Tower: Revenge of the Warlock Part VII\n\n";

    LevelEditor editor;
    std::vector<std::pair<std::string, Dungeon>> preloadedDungeons = {
        { "Tutorial", Dungeon(tutorialGrid()) },
        { "The First Floor", Dungeon(level1Grid()) }
    };

    while (true) {
        const int menuChoice = getMainMenuChoice();

        if (menuChoice == 3) {
            std::cout << "Thanks for playing!\n";
            break;
        }

        if (menuChoice == 2) {
            editor.run();
            continue;
        }

        std::vector<std::pair<std::string, Dungeon>> allDungeons = preloadedDungeons;
        for (const auto& nameAndDungeon : editor.getSavedDungeons())
            allDungeons.push_back(nameAndDungeon);

        if (allDungeons.empty()) {
            std::cout << "No dungeons available. Design one first!\n";
            continue;
        }

        std::cout << "\nDungeons:\n";
        for (size_t i = 0; i < allDungeons.size(); i++)
            std::cout << " " << (i + 1) << ") " << allDungeons[i].first << '\n';

        const int selectedIndex = getDungeonChoice(allDungeons);
        Game game(allDungeons[static_cast<size_t>(selectedIndex)].second.cloneForPlay());
        runGameLoop(game);

        if (game.getResult() == GameResult::GameOver || game.getResult() == GameResult::Victory
            || game.getResult() == GameResult::Quit)
            std::cout << "Returning to main menu.\n";
    }

    return 0;
}
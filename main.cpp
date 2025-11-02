
#include <bits/stdc++.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdlib> // Para system("clear")
using namespace std;

struct Pos { int x=0,y=0; };
static inline int manhattan(const Pos &a, const Pos &b){ return abs(a.x-b.x)+abs(a.y-b.y); }

// ---------- Monster ----------
struct Monster {
    int id = 0;
    int hp = 0;
    int attack_damage = 0;
    int vision_range = 0;
    int attack_range = 0;
    Pos pos{};
    atomic<bool> active{false};
    atomic<bool> alive{true};

    Monster() = default;
    Monster(const Monster&) = delete;
    Monster& operator=(const Monster&) = delete;
    Monster(Monster&& other) noexcept {
        id = other.id; hp = other.hp; attack_damage = other.attack_damage;
        vision_range = other.vision_range; attack_range = other.attack_range; pos = other.pos;
        active.store(other.active.load()); alive.store(other.alive.load());
    }
    Monster& operator=(Monster&& other) noexcept {
        if(this!=&other){
            id = other.id; hp = other.hp; attack_damage = other.attack_damage;
            vision_range = other.vision_range; attack_range = other.attack_range; pos = other.pos;
            active.store(other.active.load()); alive.store(other.alive.load());
        }
        return *this;
    }
};

// ---------- Hero ----------
struct Hero {
    int id = 0;
    int hp = 0;
    int attack_damage = 0;
    int attack_range = 0;
    Pos pos{};
    vector<Pos> path;
    atomic<bool> alive{true};
    atomic<bool> reached{false};

    Hero() = default;
    Hero(const Hero&) = delete;
    Hero& operator=(const Hero&) = delete;
    Hero(Hero&& other) noexcept {
        id = other.id; hp = other.hp; attack_damage = other.attack_damage;
        attack_range = other.attack_range; pos = other.pos; path = std::move(other.path);
        alive.store(other.alive.load()); reached.store(other.reached.load());
    }
    Hero& operator=(Hero&& other) noexcept {
        if(this!=&other){
            id = other.id; hp = other.hp; attack_damage = other.attack_damage;
            attack_range = other.attack_range; pos = other.pos; path = std::move(other.path);
            alive.store(other.alive.load()); reached.store(other.reached.load());
        }
        return *this;
    }
};

// ---------- Config/global ----------
struct Config {
    int grid_w = 30;
    int grid_h = 20;
    int hero_count = 0;
    vector<Hero> heroes;
    int monster_count = 0;
    vector<Monster> monsters;
} cfg;

// synchronization
mutex world_mtx;            // protege posiciones y HP
mutex cout_mtx;             // logs
condition_variable cv_end;
atomic<bool> sim_end{false};

// renderer params
int RENDER_MS = 120; // ms per frame

// logging to stderr (no interfiere con grid in stdout)
void logf(const string &s){
    lock_guard<mutex> lk(cout_mtx);
    cerr << s << '\n';
}

// Helper: map hero id to single-char for grid display (1..9 -> '1'..'9', 10->'A', etc.)
static inline char hero_char_for(int id){
    if(id>=1 && id<=9) return char('0' + id);
    int idx = id - 10;
    if(idx >= 0 && idx < 26) return char('A' + idx);
    return 'H';
}

// --------- Parsing config (supports HERO_COUNT and HERO_i_... keys) ----------
bool parse_path_pairs(const string &s, vector<Pos> &out){
    out.clear();
    size_t p = s.find('(');
    while(p!=string::npos){
        size_t q = s.find(')', p);
        if(q==string::npos) break;
        string in = s.substr(p+1, q-p-1);
        int a=0,b=0; char comma;
        istringstream iss(in);
        if(!(iss>>a>>comma>>b)) return false;
        out.push_back({a,b});
        p = s.find('(', q);
    }
    return true;
}


// ========================================================================
// === INICIO DE LA FUNCIÓN parse_config CORREGIDA E INTELIGENTE ===
// ========================================================================
bool parse_config(const string &fname){
    ifstream ifs(fname);
    if(!ifs.is_open()) return false;
    string line;
    vector<string> lines;

    // --- PASO 1: "Pegar" líneas rotas ---
    string current_line = "";
    while (getline(ifs, line)) {
        auto lpos = line.find_first_not_of(" \t\r\n");
        if (lpos == string::npos) continue; 
        auto rpos = line.find_last_not_of(" \t\r\n");
        string t = line.substr(lpos, rpos - lpos + 1);
        if (t.empty() || t[0] == '#') continue; 

        bool is_new_key = (t.rfind("GRID_SIZE", 0) == 0 ||
                           t.rfind("HERO_COUNT", 0) == 0 ||
                           t.rfind("HERO_HP", 0) == 0 ||
                           t.rfind("HERO_ATTACK_DAMAGE", 0) == 0 ||
                           t.rfind("HERO_ATTACK_RANGE", 0) == 0 ||
                           t.rfind("HERO_START", 0) == 0 ||
                           t.rfind("HERO_PATH", 0) == 0 ||
                           t.rfind("HERO_", 0) == 0 || 
                           t.rfind("MONSTER_COUNT", 0) == 0 ||
                           t.rfind("MONSTER_", 0) == 0); 

        if (is_new_key) {
            if (!current_line.empty()) lines.push_back(current_line);
            current_line = t;
        } else {
            if (!current_line.empty()) current_line += " " + t;
        }
    }
    if (!current_line.empty()) lines.push_back(current_line);
    // --- FIN del "pegado" de líneas ---


    // --- PASO 2: Primera pasada: buscar cuentas, formato, y max ID ---
    bool single_hero_format = false; // Flag para formato HERO_HP
    bool hero_count_explicit = false; // Flag para HERO_COUNT
    int max_hero_id_found = 0; // Para inferir HERO_COUNT
    regex hero_id_regex("^HERO_(\\d+)_HP\\s+.*$"); // Regex para buscar IDs

    for(auto &ln : lines){
        if(ln.rfind("GRID_SIZE",0)==0){
            istringstream iss(ln); string k; iss>>k>>cfg.grid_w>>cfg.grid_h;
        } else if(ln.rfind("HERO_COUNT",0)==0){
            istringstream iss(ln); string k; iss>>k>>cfg.hero_count;
            hero_count_explicit = true; // El count es explícito
        } else if(ln.rfind("MONSTER_COUNT",0)==0){
            istringstream iss(ln); string k; iss>>k>>cfg.monster_count;
        } else if (ln.rfind("HERO_HP", 0) == 0) {
            single_hero_format = true;
        } else {
            smatch m;
            if(regex_search(ln, m, hero_id_regex)){
                int id = stoi(m[1]);
                if(id > max_hero_id_found) max_hero_id_found = id;
            }
        }
    }

    // --- NUEVO: Lógica de decisión para HERO_COUNT ---
    // Esta es la lógica que tú pediste
    if (hero_count_explicit) {
        // Se encontró HERO_COUNT N, se usa ese valor.
        // (cfg.hero_count ya está seteado)
    } else if (max_hero_id_found > 0) {
        // No se encontró HERO_COUNT, pero sí HERO_X_HP.
        // Se infiere el count del ID más alto.
        cfg.hero_count = max_hero_id_found;
    } else if (single_hero_format) {
        // No se encontró N-Hero, pero sí HERO_HP.
        // Se asume 1 héroe.
        cfg.hero_count = 1;
    }
    // Si no, cfg.hero_count se queda en 0 (no hay héroes).
    
    // --- PASO 3: Inicializar vectores (ahora con el count correcto) ---
    if(cfg.hero_count < 0) cfg.hero_count = 0;
    cfg.heroes.clear(); cfg.heroes.reserve(cfg.hero_count);
    for(int i=0;i<cfg.hero_count;i++){
        Hero h; h.id = i+1;
        cfg.heroes.push_back(std::move(h));
    }
    if(cfg.monster_count < 0) cfg.monster_count = 0;
    cfg.monsters.clear(); cfg.monsters.reserve(cfg.monster_count);
    for(int i=0;i<cfg.monster_count;i++){
        Monster m; m.id = i+1;
        cfg.monsters.push_back(std::move(m));
    }

    // --- PASO 4: Segunda pasada: parsear héroes y monstruos ---
    regex hero_regex("^HERO_(\\d+)_(\\w+)\\s+(.*)$");
    regex monster_regex("^MONSTER_(\\d+)_(\\w+)\\s+(.*)$");

    for(auto &ln : lines){
        smatch m;
        if(regex_search(ln, m, hero_regex)){
            // Formato N-Héroes (HERO_1_HP...)
            int idx = stoi(m[1]);
            string prop = m[2];
            string rest = m[3];
            if(idx >= 1 && idx <= cfg.hero_count){
                Hero &h = cfg.heroes[idx-1];
                if(prop=="HP") h.hp = stoi(rest);
                else if(prop=="ATTACK_DAMAGE") h.attack_damage = stoi(rest);
                else if(prop=="ATTACK_RANGE") h.attack_range = stoi(rest);
                else if(prop=="START"){
                    istringstream iss(rest); iss>>h.pos.x>>h.pos.y;
                } else if(prop=="PATH"){
                    vector<Pos> pth;
                    if(parse_path_pairs(rest, pth)) h.path = std::move(pth);
                }
            }
        } else if(regex_search(ln, m, monster_regex)){
            // Formato Monstruos (MONSTER_1_HP...)
            int idx = stoi(m[1]);
            string prop = m[2];
            string rest = m[3];
            if(idx>=1 && idx<=cfg.monster_count){
                Monster &mo = cfg.monsters[idx-1];
                if(prop=="HP") mo.hp = stoi(rest);
                else if(prop=="ATTACK_DAMAGE") mo.attack_damage = stoi(rest);
                else if(prop=="VISION_RANGE") mo.vision_range = stoi(rest);
                else if(prop=="ATTACK_RANGE") mo.attack_range = stoi(rest);
                else if(prop=="COORDS"){ istringstream iss(rest); iss>>mo.pos.x>>mo.pos.y; }
            }
        } else if (cfg.hero_count == 1 && single_hero_format) {
            // Lógica para formato 1-Héroe (HERO_HP...)
            Hero &h = cfg.heroes[0];
            string k, rest;
            istringstream iss(ln);
            iss >> k;
            getline(iss, rest);
            auto lpos = rest.find_first_not_of(" \t");
            if(lpos != string::npos) rest = rest.substr(lpos);
            else rest = ""; 

            if (k == "HERO_HP" && !rest.empty()) h.hp = stoi(rest);
            else if (k == "HERO_ATTACK_DAMAGE" && !rest.empty()) h.attack_damage = stoi(rest);
            else if (k == "HERO_ATTACK_RANGE" && !rest.empty()) h.attack_range = stoi(rest);
            else if (k == "HERO_START" && !rest.empty()) {
                istringstream iss_start(rest); iss_start >> h.pos.x >> h.pos.y;
            } else if (k == "HERO_PATH" && !rest.empty()) {
                vector<Pos> pth;
                if(parse_path_pairs(rest, pth)) h.path = std::move(pth);
            }
        }
    }

    // Sanity defaults
    for(auto &h : cfg.heroes){
        if(h.hp==0) h.hp = 100;
    }
    for(auto &mo : cfg.monsters){
        if(mo.hp==0) mo.hp = 50;
    }

    return true;
}
// ========================================================================
// === FIN DE LA FUNCIÓN parse_config CORREGIDA ===
// ========================================================================


// --------- Alert propagation (BFS): when a monster sees a hero, alert monsters in its vision radius ----------
void propagate_alert(int from_idx){
    queue<int> q; q.push(from_idx);
    vector<bool> seen(cfg.monster_count, false);
    seen[from_idx] = true;
    while(!q.empty()){
        int cur = q.front(); q.pop();
        Monster &mcur = cfg.monsters[cur];
        for(int j=0;j<cfg.monster_count;j++){
            if(j==cur) continue;
            Monster &m2 = cfg.monsters[j];
            if(!m2.alive.load()) continue;
            int d;
            { lock_guard<mutex> lk(world_mtx); d = manhattan(mcur.pos, m2.pos); }
            if(d <= mcur.vision_range && !seen[j]){
                bool prev = m2.active.exchange(true);
                if(!prev) logf("MONSTER "+to_string(m2.id)+" alertado por MONSTER "+to_string(mcur.id));
                seen[j]=true;
                q.push(j);
            }
        }
    }
}

// --------- HERO thread function (one per hero) ----------
void hero_thread_func(int idx){
    // idx is 0-based index into cfg.heroes
    Hero &h = cfg.heroes[idx];
    logf("[HERO " + to_string(h.id) + "] Start at (" + to_string(h.pos.x) + "," + to_string(h.pos.y) + ") HP=" + to_string(h.hp));
    size_t step = 0;
    while(!sim_end.load()){
        if(h.hp <= 0){
            h.alive = false;
            logf("[HERO " + to_string(h.id) + "] Muere.");
            break;
        }
        if(h.reached.load()){
            this_thread::sleep_for(chrono::milliseconds(200));
            continue;
        }

        // check if any monster in hero's attack range
        bool any_in_range = false;
        {
            lock_guard<mutex> lk(world_mtx);
            for(auto &m : cfg.monsters){
                if(!m.alive.load()) continue;
                if(manhattan(h.pos, m.pos) <= h.attack_range){ any_in_range = true; break; }
            }
        }
        if(any_in_range){
            logf("[HERO " + to_string(h.id) + "] Entra en combate en (" + to_string(h.pos.x) + "," + to_string(h.pos.y) + ")");
            // attack loop until no monsters in range (or hero dies)
            while(!sim_end.load()){
                bool still = false;
                for(auto &m : cfg.monsters){
                    if(!m.alive.load()) continue;
                    int d;
                    { lock_guard<mutex> lk(world_mtx); d = manhattan(h.pos, m.pos); }
                    if(d <= h.attack_range){
                        still = true;
                        // attack this monster (protect monster.hp by world_mtx)
                        {
                            lock_guard<mutex> lk(world_mtx);
                            if(!m.alive.load()) continue;
                            m.hp -= h.attack_damage;
                            logf("[HERO " + to_string(h.id) + "] Ataca M" + to_string(m.id) + " -> HP=" + to_string(max(0,m.hp)));
                            if(m.hp <= 0){
                                m.alive = false;
                                logf("[MONSTER " + to_string(m.id) + "] Muerto por HERO " + to_string(h.id));
                            }
                        }
                        this_thread::sleep_for(chrono::milliseconds(350));
                        if(h.hp <= 0){
                            h.alive = false;
                            logf("[HERO " + to_string(h.id) + "] Murió durante combate.");
                            break;
                        }
                    }
                }
                if(!still) break;
            }
            logf("[HERO " + to_string(h.id) + "] Sale de combate.");
        } else {
            // move one step towards next waypoint
            if(step < h.path.size()){
                Pos target = h.path[step];
                if(h.pos.x == target.x && h.pos.y == target.y){
                    step++;
                    continue;
                }
                Pos newpos = h.pos;
                if(target.x > h.pos.x) newpos.x++;
                else if(target.x < h.pos.x) newpos.x--;
                else if(target.y > h.pos.y) newpos.y++;
                else if(target.y < h.pos.y) newpos.y--;
                {
                    lock_guard<mutex> lk(world_mtx);
                    newpos.x = max(0, min(cfg.grid_w-1, newpos.x));
                    newpos.y = max(0, min(cfg.grid_h-1, newpos.y));
                    h.pos = newpos;
                }
                this_thread::sleep_for(chrono::milliseconds(200));
            } else {
                // reached final waypoint
                h.reached = true;
                logf("[HERO " + to_string(h.id) + "] Llega a su meta.");
                break;
            }
        }
    }
    // thread ends
}

// --------- MONSTER thread function ----------
void monster_thread_func(int idx){
    Monster &m = cfg.monsters[idx];
    logf("[MONSTER " + to_string(m.id) + "] Start at (" + to_string(m.pos.x) + "," + to_string(m.pos.y) + ") HP=" + to_string(m.hp));
    while(!sim_end.load() && m.alive.load()){
        // check vision: if sees any alive hero -> active and propagate
        bool sees = false;
        Pos nearestHeroPos{};
        int nearestHeroId = -1;
        int minDist = INT_MAX;
        {
            lock_guard<mutex> lk(world_mtx);
            for(auto &h : cfg.heroes){
                if(!h.alive.load() || h.reached.load()) continue;
                int d = manhattan(m.pos, h.pos);
                if(d <= m.vision_range) sees = true;
                if(d < minDist){ minDist = d; nearestHeroPos = h.pos; nearestHeroId = h.id; }
            }
        }
        if(sees && !m.active.load()){
            m.active = true;
            logf("[MONSTER " + to_string(m.id) + "] Vio a un HERO y se activa!");
            propagate_alert(idx);
        }

        if(m.active.load()){
            // find nearest alive hero to chase/attack
            int targetIndex = -1;
            int bestDist = INT_MAX;
            {
                lock_guard<mutex> lk(world_mtx);
                for(size_t i=0;i<cfg.heroes.size();++i){
                    auto &h = cfg.heroes[i];
                    if(!h.alive.load() || h.reached.load()) continue;
                    int d = manhattan(m.pos, h.pos);
                    if(d < bestDist){ bestDist = d; targetIndex = (int)i; }
                }
            }
            if(targetIndex == -1){
                this_thread::sleep_for(chrono::milliseconds(250));
                continue;
            }

            // if hero within attack range -> attack that hero
            bool hero_in_attack = false;
            {
                lock_guard<mutex> lk(world_mtx);
                if(manhattan(m.pos, cfg.heroes[targetIndex].pos) <= m.attack_range) hero_in_attack = true;
            }
            if(hero_in_attack){
                // attack protected by world_mtx on hero HP
                {
                    lock_guard<mutex> lk(world_mtx);
                    Hero &target = cfg.heroes[targetIndex];
                    if(target.hp > 0){
                        target.hp -= m.attack_damage;
                        logf("[MONSTER " + to_string(m.id) + "] Ataca HERO " + to_string(target.id) + " -> HP=" + to_string(max(0,target.hp)));
                        if(target.hp <= 0){
                            target.alive = false;
                            logf("[MONSTER " + to_string(m.id) + "] Mató a HERO " + to_string(target.id));
                        }
                    }
                }
                this_thread::sleep_for(chrono::milliseconds(450));
            } else {
                // move one step towards the target hero (greedy Manhattan)
                Pos targetPos;
                {
                    lock_guard<mutex> lk(world_mtx);
                    targetPos = cfg.heroes[targetIndex].pos;
                }
                Pos newpos = m.pos;
                if(targetPos.x > m.pos.x) newpos.x++;
                else if(targetPos.x < m.pos.x) newpos.x--;
                else if(targetPos.y > m.pos.y) newpos.y++;
                else if(targetPos.y < m.pos.y) newpos.y--;
                {
                    lock_guard<mutex> lk(world_mtx);
                    newpos.x = max(0, min(cfg.grid_w-1, newpos.x));
                    newpos.y = max(0, min(cfg.grid_h-1, newpos.y));
                    m.pos = newpos;
                }
                this_thread::sleep_for(chrono::milliseconds(300));
            }
        } else {
            // passive sleep
            this_thread::sleep_for(chrono::milliseconds(300));
        }
    }
    // thread ends
}

// --------- Renderer thread (draw grid) ----------
void renderer_thread_func(){
    while(!sim_end.load()){
        // Build grid
        vector<string> grid(cfg.grid_h, string(cfg.grid_w, '.'));
        vector<int> hero_count_in_cell(cfg.grid_w * cfg.grid_h, 0);
        {
            lock_guard<mutex> lk(world_mtx);
            // monsters first
            for(auto &m : cfg.monsters){
                if(m.alive.load()){
                    if(m.pos.y>=0 && m.pos.y<cfg.grid_h && m.pos.x>=0 && m.pos.x<cfg.grid_w)
                        grid[m.pos.y][m.pos.x] = 'M';
                }
            }
            // heroes - may overwrite M for visibility priority of heroes
            for(auto &h : cfg.heroes){
                if(h.alive.load()){
                    if(h.pos.y>=0 && h.pos.y<cfg.grid_h && h.pos.x>=0 && h.pos.x<cfg.grid_w){
                        int idx = h.pos.y*cfg.grid_w + h.pos.x;
                        hero_count_in_cell[idx]++;
                        // show a digit/letter for hero id if single hero, or '@' if multiple
                        if(hero_count_in_cell[idx]==1){
                            grid[h.pos.y][h.pos.x] = hero_char_for(h.id);
                        } else {
                            grid[h.pos.y][h.pos.x] = '@';
                        }
                    }
                }
            }
        }

        // --- CORRECCIÓN DE ANIMACIÓN ---
        // Clear screen + home
        system("clear"); // Usa el comando 'clear' del sistema
        
        cout << "=== DOOM-SIM (ASCII) N-HEROES ===   Grid: " << cfg.grid_w << "x" << cfg.grid_h << "   Frame(ms): " << RENDER_MS << "\n";
        for(int y=0;y<cfg.grid_h;y++){
            for(int x=0;x<cfg.grid_w;x++){
                char c = grid[y][x];
                if(c=='M') cout << "\033[1;33m" << c << "\033[0m";
                else if(c=='@') cout << "\033[1;35m" << c << "\033[0m";
                else if(c>='0' && c<='9') cout << "\033[1;31m" << c << "\033[0m";
                else if(c>='A' && c<='Z') cout << "\033[1;31m" << c << "\033[0m";
                else cout << c;
            }
            cout << '\n';
        }

        // Info panel
        {
            lock_guard<mutex> lk(world_mtx);
            cout << "\nHeroes:\n";
            for(auto &h : cfg.heroes){
                cout << " H" << h.id << " [" << (h.alive.load() ? "VIVO" : "MUERTO") << (h.reached.load() ? "/META":"") << "] HP=" << h.hp
                     << " Pos=(" << h.pos.x << "," << h.pos.y << ")";
                cout << " PathLen=" << h.path.size() << "\n";
            }
            cout << "Monsters:\n";
            for(auto &m : cfg.monsters){
                cout << " M" << m.id << " [" << (m.alive.load() ? "VIVO" : "MUERTO") << "] HP=" << m.hp
                     << " Pos=(" << m.pos.x << "," << m.pos.y << ") V="<<m.vision_range<<" R="<<m.attack_range<<"\n";
            }
            cout << "\n(Logs aparecen en stderr)\n";
        }

        cout.flush();
        this_thread::sleep_for(chrono::milliseconds(RENDER_MS));

        // quick termination check
        bool all_done = true;
        {
            lock_guard<mutex> lk(world_mtx);
            if(cfg.heroes.empty()){
                all_done = true;
            } else {
                bool any_alive_unreached = false;
                for(auto &h : cfg.heroes){
                    if(h.alive.load() && !h.reached.load()){ any_alive_unreached = true; break; }
                }
                if(any_alive_unreached) all_done = false;
            }
        }
        if(all_done){
            sim_end = true;
            cv_end.notify_all();
            break;
        }
    }

    // final draw summary
    cout << "\033[2J\033[H"; // Limpia una última vez
    cout << "=== DOOM-SIM (FIN) ===\n";
    {
        lock_guard<mutex> lk(world_mtx);
        cout << "Resumen final:\n";
        for(auto &h : cfg.heroes){
            cout << " H" << h.id << " -> " << (h.alive.load() ? "VIVO" : "MUERTO") << (h.reached.load() ? "/META":"") << " HP="<<h.hp<<" Pos=("<<h.pos.x<<","<<h.pos.y<<")\n";
        }
        for(auto &m : cfg.monsters){
            cout << " M" << m.id << " -> " << (m.alive.load() ? "VIVO" : "MUERTO") << " HP="<<m.hp<<" Pos=("<<m.pos.x<<","<<m.pos.y<<")\n";
        }
    }
    cout.flush();
}

// --------- main ----------
int main(int argc, char** argv){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    if(argc < 2){
        cerr << "Uso: " << argv[0] << " config.txt\n";
        return 1;
    }
    string cfgfile = argv[1];
    if(!parse_config(cfgfile)){
        cerr << "Error al parsear config: " << cfgfile << "\n";
        return 1;
    }

    // create threads
    vector<thread> hero_threads;
    for(int i=0;i<cfg.hero_count;i++){
        hero_threads.emplace_back(hero_thread_func, i);
    }
    vector<thread> monster_threads;
    for(int i=0;i<cfg.monster_count;i++){
        monster_threads.emplace_back(monster_thread_func, i);
    }
    thread renderer(renderer_thread_func);

    // Wait for sim_end
    {
        unique_lock<mutex> lk(world_mtx);
        // Si no hay héroes, termina de inmediato
        if(cfg.hero_count > 0) {
            cv_end.wait(lk, [](){ return sim_end.load(); });
        } else {
            sim_end = true; // Asegúrate de que sim_end esté en true
        }
    }

    // join heroes
    for(auto &t : hero_threads) if(t.joinable()) t.join();
    for(auto &t : monster_threads) if(t.joinable()) t.join();
    if(renderer.joinable()) renderer.join();

    cerr << "Simulación finalizada.\n";
    return 0;
}
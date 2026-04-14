#include <algorithm>
#include <cmath>
#include <iostream>
#include <initializer_list>
#include <tower_defense.h>
#include <vector>
#include <string>
#include <sstream>

using namespace std;
using namespace flecs::components;

// Shortcuts to imported components
using Position = transform::Position3;
using Rotation = transform::Rotation3;
using Velocity = physics::Velocity3;
using Input = input::Input;
using SpatialQuery = flecs::systems::physics::SpatialQuery;
using SpatialQueryResult = flecs::systems::physics::SpatialQueryResult;
using Color = graphics::Color;
using Specular = graphics::Specular;
using Emissive = graphics::Emissive;
using Box = geometry::Box;
using PointLight = graphics::PointLight;

#define ECS_PI_2 ((float)(GLM_PI * 2))

// Game constants
static const int LevelScale = 1;
static const float EnemySpeed = 5.0;
static const float EnemySpawnInterval = 0.15;

static const float RecoilAmount = 0.3;
static const float DecreaseRecoilRate = 1.5;
static const float HitCooldownRate = 1.0;
static const float HitCooldownInitialValue = 0.25;

static const float TurretRotateSpeed = 4.0;
static const float TurretRange = 5.0;
static const float TurretCannonOffset = 0.2;
static const float TurretCannonLength = 0.6;

static const float BulletSpeed = 26.0;
static const float BulletDamage = 0.015;

static const float BeamDamage = 0.25;
static const float BeamSize = 0.06;

static const float SmokeSize = 1.5;
static const float ExplodeRadius = 1.0;
static const int SmokeParticleCount = 50;

static const float SparkInitialVelocity = 11.0;
static const int SparkParticleCount = 30;
static const float SparkSize = 0.15;

static const float TileSize = 3.0;
static const float TileHeight = 0.6;
static const float TileSpacing = 0.00;
static const int TileCountX = 20;
static const int TileCountZ = 20;

// Direction vector. During pathfinding enemies will cycle through this vector
// to find the next direction to turn to.
static const transform::Position2 dir[] = {
    {-1, 0},
    {0, -1},
    {1, 0},
    {0, 1},
};

// Level builder utilities
template <typename T>
class grid {
public:
    grid(int width, int height)
        : m_width(width)
        , m_height(height) 
    { 
        for (int x = 0; x < width; x ++) {
            for (int y = 0; y < height; y ++) {
                m_values.push_back(T());
            }
        }
    }

    void set(int32_t x, int32_t y, T value) {
        m_values[y * m_width + x] = value;
    }

    const T operator()(int32_t x, int32_t y) {
        return m_values[y * m_width + x];
    }

private:
    int m_width;
    int m_height;
    std::vector<T> m_values;
};

struct Waypoint {
    float x, y;
};

enum class TileKind {
    Turret = 0, // Default
    Path,
    Other
};

struct Waypoints {
    Waypoints(grid<TileKind> *g, initializer_list<Waypoint> pts) : tiles(g) {
        for (const auto& p : pts)
            add(p, TileKind::Path);
    }

    void add(Waypoint next, TileKind kind) {
        next.x *= LevelScale; next.y *= LevelScale;
        if (next.x == last.x) {
            do {
                last.y += (last.y < next.y) - (last.y > next.y);
                tiles->set(last.x, last.y, kind);
            } while (next.y != last.y);
        } else if (next.y == last.y) {
            do {
                last.x += (last.x < next.x) - (last.x > next.x);
                tiles->set(last.x, last.y, kind);
            } while (next.x != last.x);
        }

        last.x = next.x;
        last.y = next.y;
    }

    void fromTo(Waypoint first, Waypoint second, TileKind kind) {
        last = first;
        add(second, kind);
    }

    grid<TileKind> *tiles = nullptr;
    Waypoint last = {0, 0};
};

// Components

namespace tower_defense {

// Forward declaration
struct ScoreDigit;
struct Billboard;

struct Game {
    Game() {
        level = flecs::entity::null();
        center = {0,0,0};
        size = 0;
        current_level = 1;
        max_level = 5;
        enemies_to_spawn = 0;
        enemies_spawned = 0;
        enemies_alive = 0;
        score = 0;
        failed = false;
        won = false;
        fireworks_timer = 0;
        billboard_created = false;
        displayed_score = -1;
    }

    flecs::entity level;
    
    Position center;
    float size;

    int current_level;
    int max_level;
    int enemies_to_spawn;
    int enemies_spawned;
    int enemies_alive;
    int score;
    bool failed;
    bool won;
    float fireworks_timer;
    bool billboard_created;
    int displayed_score;  // Track what score is currently displayed
};

static int enemy_count_for_level(int level) {
    return 5 + (level - 1) * 2;
}

static void check_level_complete(flecs::world& ecs, Game& g) {
    if (g.failed || g.won) {
        return;
    }

    if (g.enemies_spawned >= g.enemies_to_spawn && g.enemies_alive == 0) {
        if (g.current_level >= g.max_level) {
            g.won = true;
            std::cout << "=== VICTORY! ===" << std::endl;
            std::cout << "All " << g.max_level << " levels completed!" << std::endl;
            std::cout << "Final Score: " << g.score << std::endl;
        } else {
            g.current_level += 1;
            g.enemies_spawned = 0;
            g.enemies_alive = 0;
            g.enemies_to_spawn = enemy_count_for_level(g.current_level);
            std::cout << "Starting level " << g.current_level << " (" << g.enemies_to_spawn << " enemies)" << std::endl;
        }
    }
}

struct Level {
    Level() {
        map = nullptr;
    }

    Level(grid<TileKind> *arg_map, transform::Position2 arg_spawn) {
        map = arg_map;
        spawn_point = arg_spawn;
    }

    grid<TileKind> *map;
    transform::Position2 spawn_point;  
};

struct Particle {
    float size_decay;
    float color_decay;
    float velocity_decay;
    float lifespan;
};

struct ParticleLifespan {
    ParticleLifespan() {
        t = 0;
    }
    float t;
};

struct ExplosionLight {
    float intensity;
    float decay;
};

struct Enemy { };

struct Direction {
    int value;
};

struct Health {
    Health() {
        value = 1.0;
    }
    float value;
};

struct Bullet { };

struct Turret { 
    Turret(float fire_interval_arg = 1.0) {
        lr = 1;
        t_since_fire = 0;
        fire_interval = fire_interval_arg;
    }

    float fire_interval;
    float t_since_fire;
    int lr;
};

struct Recoil {
    float value;
};

struct HitCooldown {
    float value;
};

struct Laser { };

struct Target {
    Target() {
        prev_position[0] = 0;
        prev_position[1] = 0;
        prev_position[2] = 0;
        lock = false;
    }

    flecs::entity target;
    vec3 prev_position;
    vec3 aim_position;
    float angle;
    float distance;
    bool lock;
};

// Prefab types
namespace prefabs {
    struct Tree {
        float height;
        float variation;
    };

    struct Tile { };
    struct Path { };
    struct Enemy { };

    struct materials {
        struct Beam { };
        struct Metal { };
        struct CannonHead { };
        struct LaserLight { };
    };

    struct Particle { };
    struct Bullet { };
    struct NozzleFlash { };
    struct Smoke { };
    struct Spark { };
    struct Ion { };
    struct Bolt { };
    struct Firework { };

    struct Turret {
        struct Base { };
        struct Head { };
    };

    struct Cannon {
        struct Head {
            struct BarrelLeft { };
            struct BarrelRight { };
        };
    };

    struct Laser {
        struct Head {
            struct Beam { };
        };
    };
}

// Tag component for score digit entities (used for cleanup)
struct ScoreDigit { };

// Tag component for billboard text entities
struct Billboard { };

}

using namespace tower_defense;

// Scope for level entities (tile, path)
struct level { };

// Scope for turrets
struct turrets { };

// Scope for enemies
struct enemies { };

// Scope for particles
struct particles { };

// Scope for HUD elements (score display)
struct hud { };

// Utility functions
float randf(float scale) {
    return ((float)rand() / (float)RAND_MAX) * scale;
}

// ============================================================
// Pixel font data (5 rows x 3 cols) for digits and letters
// Each row is 3 bits, top-to-bottom
// ============================================================
struct PixelGlyph {
    int rows[5]; // each row: 3 bits (bit2=left, bit1=mid, bit0=right)
};

static const PixelGlyph digit_glyphs[10] = {
    {7,5,5,5,7}, // 0
    {2,6,2,2,7}, // 1
    {7,1,7,4,7}, // 2
    {7,1,7,1,7}, // 3
    {5,5,7,1,1}, // 4
    {7,4,7,1,7}, // 5
    {7,4,7,5,7}, // 6
    {7,1,1,1,1}, // 7
    {7,5,7,5,7}, // 8
    {7,5,7,1,7}, // 9
};

static PixelGlyph char_to_glyph(char c) {
    switch(c) {
        case 'A': return {7,5,7,5,5};
        case 'C': return {7,4,4,4,7};
        case 'D': return {6,5,5,5,6};
        case 'E': return {7,4,7,4,7};
        case 'F': return {7,4,7,4,4};
        case 'G': return {7,4,5,5,7};
        case 'I': return {7,2,2,2,7};
        case 'L': return {4,4,4,4,7};
        case 'M': return {5,7,5,5,5};
        case 'N': return {5,7,7,5,5};
        case 'O': return {7,5,5,5,7};
        case 'P': return {7,5,7,4,4};
        case 'R': return {7,5,7,5,5};
        case 'S': return {7,4,7,1,7};
        case 'T': return {7,2,2,2,2};
        case 'V': return {5,5,5,5,7};
        case 'Y': return {5,5,7,2,2};
        case '!': return {2,2,2,0,2};
        case ' ': return {0,0,0,0,0};
        case '0': return digit_glyphs[0];
        case '1': return digit_glyphs[1];
        case '2': return digit_glyphs[2];
        case '3': return digit_glyphs[3];
        case '4': return digit_glyphs[4];
        case '5': return digit_glyphs[5];
        case '6': return digit_glyphs[6];
        case '7': return digit_glyphs[7];
        case '8': return digit_glyphs[8];
        case '9': return digit_glyphs[9];
        default:  return {0,0,0,0,0};
    }
}

// Build a string of glyphs using 3D boxes. Returns the created entities.
// base_pos: world position of the top-left pixel of the first glyph
// pixel_size: size of each "pixel" block
// spacing: horizontal gap between glyphs (in pixel_size units)
// color: RGB color of the blocks
// emissive_val: emissive intensity for glow
// add_billboard: if true, add Billboard tag to entities
static std::vector<flecs::entity> build_text_3d(
    flecs::world& ecs,
    const Position& base_pos,
    float pixel_size,
    float spacing,
    const std::string& text,
    const Color& color,
    float emissive_val = 1.0f,
    bool add_billboard = false)
{
    std::vector<flecs::entity> entities;
    float advance = 3.0f * pixel_size + spacing * pixel_size;
    float cursor_x = base_pos.x;
    
    for (size_t ci = 0; ci < text.size(); ci++) {
        PixelGlyph g = char_to_glyph(text[ci]);
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                if (g.rows[row] & (1 << (2 - col))) {
                    float px = cursor_x + col * pixel_size;
                    float py = base_pos.y - row * pixel_size;
                    float pz = base_pos.z;
                    
                    auto e = ecs.entity().child_of<hud>();
                    e.set<Position>({px, py, pz})
                     .set<Box>({pixel_size * 0.9f, pixel_size * 0.9f, pixel_size * 0.9f})
                     .set<Color>(color)
                     .set<Emissive>({emissive_val});
                    
                    if (add_billboard) {
                        e.add<Billboard>();
                    }
                    
                    entities.push_back(e);
                }
            }
        }
        cursor_x += advance;
    }
    
    return entities;
}

float to_coord(float x) {
    return x * (TileSpacing + TileSize) - (TileSize / 2.0);
}

float from_coord(float x) {
    return (x + (TileSize / 2.0)) / (TileSpacing + TileSize);
}

float toX(float x) {
    return to_coord(x + 0.5) - to_coord((TileCountX / 2.0));
}

float toZ(float z) {
    return to_coord(z);
}

float from_x(float x) {
    return from_coord(x + to_coord((TileCountX / 2.0))) - 0.5;
}

float from_z(float z) {
    return from_coord(z);
}

float angle_normalize(float angle) {
    return angle - floor(angle / ECS_PI_2) * ECS_PI_2;
}

float look_at(vec3 eye, vec3 dest) {
    vec3 diff;
    
    glm_vec3_sub(dest, eye, diff);
    float x = fabs(diff[0]), z = fabs(diff[2]);
    bool x_sign = diff[0] < 0, z_sign = diff[2] < 0;
    float r = atan(z / x);

    if (z_sign) {
        r += GLM_PI;
    }

    if (z_sign == x_sign) {
        r = -r + GLM_PI;
    }

    return angle_normalize(r + GLM_PI);
}

float rotate_to(float cur, float target, float increment) {
    cur = angle_normalize(cur);
    target = angle_normalize(target);

    if (cur - target > GLM_PI) {
        cur -= ECS_PI_2;
    } else if (cur - target < -GLM_PI) {
        cur += ECS_PI_2;
    }
    
    if (cur > target) {
        cur -= increment;
        if (cur < target) {
            cur = target;
        }
    } else {
        cur += increment;
        if (cur > target) {
            cur = target;
        }
    }

    return cur;
}

// Check if enemy needs to change direction
bool find_path(Position& p, Direction& d, const Level& lvl) {
    float t_x = from_x(p.x);
    float t_y = from_z(p.z);
    int ti_x = (int)t_x;
    int ti_y = (int)t_y;
    float td_x = t_x - ti_x;
    float td_y = t_y - ti_y;

    if (td_x < 0.1 && td_y < 0.1) {
        grid<TileKind> *tiles = lvl.map;
        int backwards = (d.value + 2) % 4;

        for (int i = 0; i < 3; i ++) {
            int n_x = ti_x + dir[d.value].x;
            int n_y = ti_y + dir[d.value].y;

            if (n_x >= 0 && n_x <= TileCountX) {
                if (n_y >= 0 && n_y <= TileCountZ) {
                    if (tiles[0](n_x, n_y) == TileKind::Path) {
                        return false;
                    }
                }
            }

            do {
                d.value = (d.value + 1) % 4;
            } while (d.value == backwards);
        }

        return true;        
    }

    return false;
}

static bool turret_at_tile(flecs::world& ecs, float x, float z) {
    bool found = false;
    ecs.each([&](flecs::entity e, const Position& p, const Turret&) {
        if (fabs(p.x - x) < 0.1f && fabs(p.z - z) < 0.1f) {
            found = true;
        }
    });
    return found;
}

static void remove_tree_at(flecs::world& ecs, float x, float z) {
    ecs.each([&](flecs::entity e, const Position& p) {
        if (fabs(p.x - x) < 0.1f && fabs(p.z - z) < 0.1f) {
            if (e.has<prefabs::Tree>()) {
                e.destruct();
            }
        }
    });
}

static bool try_build_turret(flecs::world& ecs, const Level& lvl, bool laser) {
    std::vector<transform::Position2> candidates;

    for (int x = 0; x < TileCountX * LevelScale; x ++) {
        for (int z = 0; z < TileCountZ * LevelScale; z ++) {
            if (lvl.map[0](x, z) != TileKind::Turret) {
                continue;
            }

            float xc = toX(x);
            float zc = toZ(z);
            if (!turret_at_tile(ecs, xc, zc)) {
                candidates.push_back({(float)x, (float)z});
            }
        }
    }

    if (candidates.empty()) {
        return false;
    }

    auto tile = candidates[rand() % candidates.size()];
    float xc = toX(tile.x);
    float zc = toZ(tile.y);

    remove_tree_at(ecs, xc, zc);

    auto entity = ecs.entity().child_of<turrets>();
    if (laser) {
        entity.is_a<prefabs::Laser>();
    } else {
        entity.is_a<prefabs::Cannon>();
    }
    entity.set<Position>({xc, TileHeight / 2, zc});
    return true;
}

static bool try_delete_turret(flecs::world& ecs) {
    bool deleted = false;
    ecs.each([&](flecs::entity e, const Position& p, const Turret&) {
        if (!deleted) {
            e.destruct();
            deleted = true;
        }
    });
    return deleted;
}

void TurretControl(flecs::iter& it, size_t, const Input& input, const Game& g) {
    if (g.failed || g.won) {
        return;
    }

    if (input.keys[ECS_KEY_B].down) {
        if (try_build_turret(it.world(), g.level.get<Level>(), false)) {
            std::cout << "Built a cannon turret." << std::endl;
        } else {
            std::cout << "No valid turret location available." << std::endl;
        }
    }

    if (input.keys[ECS_KEY_L].down) {
        if (try_build_turret(it.world(), g.level.get<Level>(), true)) {
            std::cout << "Built a laser turret." << std::endl;
        } else {
            std::cout << "No valid laser turret location available." << std::endl;
        }
    }

    if (input.keys[ECS_KEY_X].down || input.keys[ECS_KEY_DELETE].down) {
        if (try_delete_turret(it.world())) {
            std::cout << "Deleted a turret." << std::endl;
        } else {
            std::cout << "No turret to delete." << std::endl;
        }
    }
}

void SpawnEnemy(flecs::iter& it, size_t, Game& g) {
    if (g.failed || g.won) {
        return;
    }

    if (g.enemies_spawned >= g.enemies_to_spawn) {
        return;
    }

    const Level& lvl = g.level.get<Level>();
    g.enemies_spawned += 1;
    g.enemies_alive += 1;

    it.world().entity().child_of<enemies>().is_a<prefabs::Enemy>()
        .set<Direction>({0})
        .set<Position>({
            lvl.spawn_point.x, 1.2, lvl.spawn_point.y
        });
}

void MoveEnemy(flecs::iter& it, size_t i,
    Position& p, Direction& d, Game& g)
{
    const Level& lvl = g.level.get<Level>();

    if (find_path(p, d, lvl)) {
        g.enemies_alive = std::max(0, g.enemies_alive - 1);
        g.failed = true;
        std::cout << "A monster escaped! Game over." << std::endl;
        it.entity(i).destruct();
    } else {
        p.x += dir[d.value].x * EnemySpeed * it.delta_time();
        p.z += dir[d.value].y * EnemySpeed * it.delta_time();
    }
}

void ClearTarget(Target& target, Position& p) {
    flecs::entity t = target.target;
    if (t) {
        if (!t.is_alive()) {
            target.target = flecs::entity::null();
            target.lock = false;
        } else {
            Position target_pos = t.get<Position>();
            float distance = glm_vec3_distance(p, target_pos);
            if (distance > TurretRange) {
                target.target = flecs::entity::null();
                target.lock = false;
            }
        }
    }
}

void FindTarget(flecs::iter& it, size_t i, Turret& turret, Target& target, 
    Position& p, const SpatialQuery& q, SpatialQueryResult& qr) 
{
    if (target.target) {
        return;
    }

    flecs::entity enemy;
    float distance = 0, min_distance = 0;

    q.findn(p, TurretRange, qr);
    for (auto e : qr) {
        distance = glm_vec3_distance(p, e.pos);
        if (distance > TurretRange) {
            continue;
        }

        if (!min_distance || distance < min_distance) {
            min_distance = distance;
            enemy = it.world().entity(e.id);
        }
    }

    if (min_distance) {
        target.target = enemy;
        target.distance = min_distance;
    }
}

void AimTarget(flecs::iter& it, size_t i,
    Turret& turret, Target& target, Position& p) 
{
    flecs::entity enemy = target.target;
    if (enemy && enemy.is_alive()) {
        flecs::entity e = it.entity(i);

        Position target_p = enemy.get<Position>();
        vec3 diff;
        glm_vec3_sub(target_p, target.prev_position, diff);

        target.prev_position[0] = target_p.x;
        target.prev_position[1] = target_p.y;
        target.prev_position[2] = target_p.z;
        float distance = glm_vec3_distance(p, target_p);

        flecs::entity beam = e.target<prefabs::Laser::Head::Beam>();
        if (!beam) {
            glm_vec3_scale(diff, distance * 1, diff);
            glm_vec3_add(target_p, diff, target_p);
        }

        target.aim_position[0] = target_p.x;
        target.aim_position[1] = target_p.y;
        target.aim_position[2] = target_p.z;            

        float angle = look_at(p, target_p);

        flecs::entity head = e.target<prefabs::Turret::Head>();
        Rotation r = head.get<Rotation>();
        r.y = rotate_to(r.y, angle, TurretRotateSpeed * it.delta_time());
        head.set<Rotation>(r);
        target.angle = angle;
        target.lock = (r.y == angle) * (distance < TurretRange);
    }
}

void FireCountdown(flecs::iter& it, size_t i,
    Turret& turret, Target& target) 
{
    turret.t_since_fire += it.delta_time();
}

void FireAtTarget(flecs::iter& it, size_t i,
    Turret& turret, Target& target, Position& p)
{
    auto ecs = it.world();
    bool is_laser = it.is_set(3);
    flecs::entity e = it.entity(i);

    if (turret.t_since_fire < turret.fire_interval) {
        return;
    }

    if (target.target && target.lock) {
        Position pos = p;
        float angle = target.angle;
        vec3 v, target_p;
        target_p[0] = target.aim_position[0];
        target_p[1] = target.aim_position[1];
        target_p[2] = target.aim_position[2];
        glm_vec3_sub(p, target_p, v);
        glm_vec3_normalize(v);

        if (!is_laser) {
            pos.x += 1.7 * TurretCannonLength * -v[0];
            pos.z += 1.7 * TurretCannonLength * -v[2];
            glm_vec3_scale(v, BulletSpeed, v);
            pos.x += sin(angle) * 0.8 * TurretCannonOffset * turret.lr;
            pos.y = 1.1;
            pos.z += cos(angle) * 0.8 * TurretCannonOffset * turret.lr;

            flecs::entity barrel;
            if (turret.lr == -1) {
                barrel = e.target<prefabs::Cannon::Head::BarrelLeft>();
            } else {
                barrel = e.target<prefabs::Cannon::Head::BarrelRight>();
            }
            turret.lr = -turret.lr;

            barrel.set<Recoil>({ RecoilAmount });

            ecs.entity().is_a<prefabs::Bullet>()
                .child_of<particles>()
                .set<Position>(pos)
                .set<Velocity>({-v[0], 0, -v[2]});
            ecs.entity().is_a<prefabs::NozzleFlash>()
                .child_of<particles>()
                .set<Position>(pos)
                .set<Rotation>({0, angle, 0});

            ecs.entity()
                .child_of<particles>()
                .set<Position>({pos.x, pos.y, pos.z})
                .set<PointLight>({{0.5, 0.4, 0.2}, 0.4})
                .set<ExplosionLight>({1.0, 7.0}); 
        } else {
            e.target<prefabs::Laser::Head::Beam>().enable();
            pos.x += 1.4 * -v[0];
            pos.y = 1.1;
            pos.z += 1.4 * -v[2];
            ecs.scope<particles>().entity().is_a<prefabs::Bolt>()
                .set<Position>(pos)
                .set<Rotation>({0, angle, 0}); 
        }

        turret.t_since_fire = 0;
    }
}

void BeamControl(flecs::iter& it, size_t i,
    Position& p, Turret& turret, Target& target) 
{
    flecs::entity beam = it.entity(i).target<prefabs::Laser::Head::Beam>();
    if (beam && (!target.target || !target.lock)) {
        if (beam.enabled()) {
            beam.disable();
            beam.set<Box>({0.0, 0.0, 0});
        }
        return;
    }

    if (target.lock && beam && beam.enabled()) {
        flecs::entity enemy = target.target;
        if (!enemy.is_alive()) {
            return;
        }

        Position target_pos = enemy.get<Position>();
        float distance = glm_vec3_distance(p, target_pos);
        beam.set<Position>({ (distance / 2), 0.1, 0.0 });
        beam.set<Box>({BeamSize, BeamSize, distance});

        enemy.get([&](Health& h, HitCooldown& hc) {
            h.value -= BeamDamage * it.delta_time();
            hc.value = HitCooldownInitialValue;
        });

        {     
            float x_r = randf(ECS_PI_2);
            float y_r = randf(ECS_PI_2);
            float z_r = randf(ECS_PI_2);
            float speed = randf(5) + 2.0;
            float size = randf(0.15);

            it.world().scope<particles>().entity().is_a<prefabs::Ion>()
                .child_of<particles>()
                .set<Position>({target_pos.x, target_pos.y, target_pos.z}) 
                .set<Box>({size, size, size})
                .set<Velocity>({
                    cos(x_r) * speed, fabs(cos(y_r) * speed), cos(z_r) * speed});
        }
    }
}

void ApplyRecoil(Position& p, const Recoil& r) {
   p.x = TurretCannonLength - r.value; 
}

void DecreaseRecoil(flecs::iter& it, size_t, Recoil& r) {
   r.value -= it.delta_time() * DecreaseRecoilRate;
   if (r.value < 0) {
    r.value = 0;
   }
}

void DecreaseHitCoolDown(flecs::iter& it, size_t, HitCooldown& hc) {
   hc.value -= it.delta_time() * HitCooldownRate;
   if (hc.value < 0) {
    hc.value = 0;
   }
}

void ProgressParticle(flecs::iter& it, size_t i,
    ParticleLifespan& pl, const Particle& p, Box *box, Color *color, Velocity *vel)
{
    if (box) {
        box->width *= pow(p.size_decay, it.delta_time());
        box->height *= pow(p.size_decay, it.delta_time());
        box->depth *= pow(p.size_decay, it.delta_time());
    }
    if (color) {
        color->r *= pow(p.color_decay, it.delta_time());
        color->g *= pow(p.color_decay, it.delta_time());
        color->b *= pow(p.color_decay, it.delta_time()); 
    }
    if (vel) {
        vel->x *= pow(p.velocity_decay, it.delta_time());
        vel->y *= pow(p.velocity_decay, it.delta_time());
        vel->z *= pow(p.velocity_decay, it.delta_time());
    }

    pl.t += it.delta_time();
    if (pl.t > p.lifespan || ((box->width + box->height + box->depth) < 0.1)) {
        it.entity(i).destruct();
    }
}

void explode(flecs::world& ecs, Position& p, float pC, float rC, Color rgbRnd, Color rgbC) {
    for (int s = 0; s < SmokeParticleCount * pC; s ++) {
        float red = randf(rgbRnd.r) + rgbC.r;
        float green = randf(rgbRnd.g) + rgbC.g;
        float blue = randf(rgbRnd.b) + rgbC.b;
        float size = SmokeSize * randf(1.0) * rC;

        Position pp;
        pp.x = p.x + randf(ExplodeRadius) - ExplodeRadius / 2; 
        pp.y = p.y + randf(ExplodeRadius) - ExplodeRadius / 2;
        pp.z = p.z + randf(ExplodeRadius) - ExplodeRadius / 2;

        ecs.scope<particles>().entity().is_a<prefabs::Smoke>()
            .set<Position>(pp)
            .set<Box>({size, size, size})
            .set<Color>({red, green, blue});
    }

    for (int s = 0; s < SparkParticleCount * pC; s ++) {
        float x_r = randf(ECS_PI_2);
        float y_r = randf(ECS_PI_2);
        float z_r = randf(ECS_PI_2);
        float speed = randf(SparkInitialVelocity) * rC + 2.0;
        float size = SparkSize + randf(0.2);

        ecs.scope<particles>().entity().is_a<prefabs::Spark>()
            .set<Position>({p.x, p.y, p.z}) 
            .set<Box>({size, size, size})
            .set<Velocity>({
                cos(x_r) * speed, fabs(cos(y_r) * speed), cos(z_r) * speed});
    }

    ecs.entity()
        .child_of<particles>()
        .set<Position>({p.x, p.y, p.z})
        .set<PointLight>({{rgbC.r, rgbC.g, rgbC.b}, 1.25f + pC / 2.0f})
        .set<ExplosionLight>({0.75f * (0.5f + pC / 2.0f), 1.5f});
}

// Firework colors for celebration
static const Color firework_colors[] = {
    {1.0, 0.2, 0.2},  // Red
    {0.2, 1.0, 0.2},  // Green
    {0.3, 0.3, 1.0},  // Blue
    {1.0, 1.0, 0.2},  // Yellow
    {1.0, 0.2, 1.0},  // Magenta
    {0.2, 1.0, 1.0},  // Cyan
    {1.0, 0.6, 0.1},  // Orange
    {0.8, 0.4, 1.0},  // Purple
};

static const int firework_color_count = sizeof(firework_colors) / sizeof(firework_colors[0]);

static void spawn_firework(flecs::world& ecs, const Position& center) {
    float ox = randf(16.0) - 8.0;
    float oy = randf(8.0) + 5.0;
    float oz = randf(16.0) - 8.0;
    
    Position burst_pos = {center.x + ox, center.y + oy, center.z + oz};
    
    Color base_color = firework_colors[rand() % firework_color_count];
    
    int burst_count = 40 + rand() % 20;
    for (int i = 0; i < burst_count; i++) {
        float theta = randf(ECS_PI_2);
        float phi = randf(GLM_PI);
        float speed = randf(4.0) + 3.0;
        
        float vx = sin(phi) * cos(theta) * speed;
        float vy = sin(phi) * sin(theta) * speed;
        float vz = cos(phi) * speed;
        
        float size = 0.2 + randf(0.25);
        
        float r_var = base_color.r + randf(0.2) - 0.1;
        float g_var = base_color.g + randf(0.2) - 0.1;
        float b_var = base_color.b + randf(0.2) - 0.1;
        
        ecs.scope<particles>().entity().is_a<prefabs::Firework>()
            .set<Position>({burst_pos.x, burst_pos.y, burst_pos.z})
            .set<Box>({size, size, size})
            .set<Color>({r_var, g_var, b_var})
            .set<Velocity>({vx, vy, vz});
    }
    
    ecs.entity()
        .child_of<particles>()
        .set<Position>({burst_pos.x, burst_pos.y, burst_pos.z})
        .set<PointLight>({{base_color.r, base_color.g, base_color.b}, 2.0f})
        .set<ExplosionLight>({1.5f, 0.8f});
}

// Victory celebration: spawn fireworks periodically
void VictoryCelebration(flecs::iter& it, size_t, Game& g) {
    if (!g.won) {
        return;
    }
    
    g.fireworks_timer += it.delta_time();
    
    if (g.fireworks_timer >= 0.3f) {
        g.fireworks_timer = 0;
        spawn_firework(it.world(), g.center);
    }
}

// ============================================================
// ScoreDisplay: Update HUD score in 3D space (above map)
// Rebuilds score text only when score changes
// ============================================================
void ScoreDisplay(flecs::iter& it, size_t, Game& g) {
    // Only rebuild when score changes
    if (g.displayed_score == g.score) {
        return;
    }
    
    flecs::world ecs = it.world();
    
    // Destroy old score digits
    ecs.each([&](flecs::entity e, const ScoreDigit&) {
        e.destruct();
    });
    
    // Score display position: above the map, offset to the left
    float score_y = 14.0f;
    float score_x = g.center.x - 10.0f;
    float score_z = g.center.z;
    
    // Format score text
    std::ostringstream oss;
    oss << "SCORE " << g.score;
    std::string score_text = oss.str();
    
    // Build score text with small bright pixels
    float pixel_size = 0.35f;
    float spacing = 1.0f;
    auto entities = build_text_3d(ecs,
        {score_x, score_y, score_z},
        pixel_size, spacing, score_text,
        {1.0f, 0.9f, 0.3f}, 3.0f, false);  // Gold color, high emissive
    
    for (auto& e : entities) {
        e.add<ScoreDigit>();
    }
    
    // Add a point light behind the score for extra visibility
    float text_width = score_text.size() * (3.0f * pixel_size + spacing * pixel_size);
    ecs.entity().child_of<hud>()
        .set<Position>({score_x + text_width / 2, score_y - 1.0f, score_z})
        .set<PointLight>({{1.0f, 0.9f, 0.3f}, 3.0f})
        .add<ScoreDigit>();
    
    g.displayed_score = g.score;
}

// ============================================================
// VictoryBillboard: Create a large 3D billboard when game is won
// Only creates once when won=true
// ============================================================
void VictoryBillboard(flecs::iter& it, size_t, Game& g) {
    if (!g.won || g.billboard_created) {
        return;
    }
    
    flecs::world ecs = it.world();
    g.billboard_created = true;
    
    // Billboard position: high above center of map
    float by = 12.0f;
    float bx = g.center.x;
    float bz = g.center.z;
    
    float large_pixel = 0.8f;  // Large pixel size for billboard
    float large_spacing = 1.2f;
    
    // "VICTORY!" - large, bright gold
    std::string victory_text = "VICTORY!";
    float victory_width = victory_text.size() * (3.0f * large_pixel + large_spacing * large_pixel);
    float vx = bx - victory_width / 2;
    
    build_text_3d(ecs,
        {vx, by, bz},
        large_pixel, large_spacing, victory_text,
        {1.0f, 0.85f, 0.1f}, 3.0f, true);  // Bright gold, very high emissive
    
    // "SCORE N" - even larger digits
    std::ostringstream oss;
    oss << "SCORE " << g.score;
    std::string score_text = oss.str();
    float score_pixel = 1.2f;  // Extra large for score
    float score_spacing = 1.5f;
    float score_width = score_text.size() * (3.0f * score_pixel + score_spacing * score_pixel);
    float sx = bx - score_width / 2;
    
    build_text_3d(ecs,
        {sx, by - 7.0f, bz},
        score_pixel, score_spacing, score_text,
        {1.0f, 0.3f, 0.1f}, 3.0f, true);  // Bright red-orange, super high emissive
    
    // Add dramatic point lights behind the billboard
    ecs.entity().child_of<hud>()
        .set<Position>({bx, by - 2.0f, bz})
        .set<PointLight>({{1.0f, 0.85f, 0.1f}, 8.0f});
    
    ecs.entity().child_of<hud>()
        .set<Position>({bx, by - 6.0f, bz})
        .set<PointLight>({{1.0f, 0.3f, 0.1f}, 10.0f});
    
    // Add a bright spotlight behind for dramatic effect
    ecs.entity().child_of<hud>()
        .set<Position>({bx, by + 3.0f, bz - 2.0f})
        .set<PointLight>({{1.0f, 1.0f, 0.8f}, 5.0f});
}

// GameOver display
void GameOverBillboard(flecs::iter& it, size_t, Game& g) {
    if (!g.failed || g.billboard_created) {
        return;
    }
    
    flecs::world ecs = it.world();
    g.billboard_created = true;
    
    float by = 12.0f;
    float bx = g.center.x;
    float bz = g.center.z;
    
    float large_pixel = 0.8f;
    float large_spacing = 1.2f;
    
    // "GAME OVER" - large, red
    std::string go_text = "GAME OVER";
    float go_width = go_text.size() * (3.0f * large_pixel + large_spacing * large_pixel);
    float gx = bx - go_width / 2;
    
    build_text_3d(ecs,
        {gx, by, bz},
        large_pixel, large_spacing, go_text,
        {1.0f, 0.1f, 0.1f}, 3.0f, true);  // Red, high emissive
    
    // "SCORE N"
    std::ostringstream oss;
    oss << "SCORE " << g.score;
    std::string score_text = oss.str();
    float score_pixel = 1.0f;
    float score_spacing = 1.5f;
    float score_width = score_text.size() * (3.0f * score_pixel + score_spacing * score_pixel);
    float sx = bx - score_width / 2;
    
    build_text_3d(ecs,
        {sx, by - 6.0f, bz},
        score_pixel, score_spacing, score_text,
        {0.8f, 0.8f, 0.8f}, 3.0f, true);  // White-ish
    
    // Red point light
    ecs.entity().child_of<hud>()
        .set<Position>({bx, by - 2.0f, bz})
        .set<PointLight>({{1.0f, 0.1f, 0.1f}, 6.0f});
}

void HitTarget(flecs::iter& it, size_t i, Position& p, Health& h, Box& b, 
    HitCooldown& hit_cooldown, const SpatialQuery& q, SpatialQueryResult& qr)
{    
    flecs::world ecs = it.world();
    flecs::entity enemy = it.entity(i);
    float range = b.width / 2;

    q.findn(p, range, qr);
    for (auto e : qr) {
        it.world().entity(e.id).destruct();
        auto prevHealth = h.value;
        h.value -= BulletDamage;
        if (prevHealth > 0.9 && h.value < 0.9) {
            explode(ecs, p, 0.2, 0.3, {0.01, 0.3, 0.3}, {0.05, 0.7, 0.2});
            enemy.set<Color>({0.05, 0.2, 0.6});
        } else if (prevHealth > 0.7 && h.value < 0.7) {
            explode(ecs, p, 0.4, 0.5, {0.01, 0.3, 0.3}, {0.01, 0.2, 0.8});
            enemy.set<Color>({0.2, 0.05, 0.4});
        } else if (prevHealth > 0.5 && h.value < 0.5) {
            explode(ecs, p, 0.5, 0.5, {0.3, 0.01, 0.3}, {0.01, 0.01, 0.7});
            enemy.set<Color>({0.2, 0.05, 0.2});
        } else if (prevHealth > 0.3 && h.value < 0.3) {
            explode(ecs, p, 0.6, 0.7, {0.5, 0.2, 0.5}, {0.8, 0.01, 0.8});
            enemy.set<Color>({0.1, 0.03, 0.0});
        }
        hit_cooldown.value = HitCooldownInitialValue;
    }
}

void DestroyEnemy(flecs::entity e, Health& h, Position& p, Game& g) {
    flecs::world ecs = e.world();
    if (h.value <= 0) {
        g.enemies_alive = std::max(0, g.enemies_alive - 1);
        g.score += 1;
        std::cout << "Enemy destroyed! Score: " << g.score << std::endl;
        e.destruct();
        explode(ecs, p, 1.1, 1.0, {0.5, 0.2, 0.1}, {0.7, 0.1, 0.05});
        check_level_complete(ecs, g);
    }
}

void init_components(flecs::world& ecs) {
    ecs.component<Game>()
        .member("level", &Game::level)
        .member("center", &Game::center)
        .member("size", &Game::size)
        .member("current_level", &Game::current_level)
        .member("max_level", &Game::max_level)
        .member("enemies_to_spawn", &Game::enemies_to_spawn)
        .member("enemies_spawned", &Game::enemies_spawned)
        .member("enemies_alive", &Game::enemies_alive)
        .member("score", &Game::score)
        .member("failed", &Game::failed)
        .member("won", &Game::won)
        .member("fireworks_timer", &Game::fireworks_timer);

    ecs.component<Enemy>();
    ecs.component<Laser>();
    ecs.component<Bullet>();

    ecs.component<Particle>()
        .member("size_decay", &Particle::size_decay)
        .member("color_decay", &Particle::color_decay)
        .member("velocity_decay", &Particle::velocity_decay)
        .member("lifespan", &Particle::lifespan)
        .add(flecs::OnInstantiate, flecs::Inherit);

    ecs.component<ParticleLifespan>();

    ecs.component<Health>()
        .member("value", &Health::value);
    
    ecs.component<HitCooldown>()
        .member("value", &HitCooldown::value);

    ecs.component<Turret>()
        .member("fire_interval", &Turret::fire_interval);

    ecs.component<Target>()
        .member("target", &Target::target)
        .member("prev_position", &Target::prev_position)
        .member("aim_position", &Target::aim_position)
        .member("angle", &Target::angle)
        .member("distance", &Target::distance)
        .member("lock", &Target::lock);

    ecs.component<ScoreDigit>();
    ecs.component<Billboard>();
}

void init_game(flecs::world& ecs) {
    // Singleton with global game data
    ecs.component<Game>().add(flecs::Singleton);

    Game& g = ecs.ensure<Game>();
    g.current_level = 1;
    g.max_level = 5;
    g.enemies_to_spawn = enemy_count_for_level(g.current_level);
    g.enemies_spawned = 0;
    g.enemies_alive = 0;
    g.score = 0;
    g.failed = false;
    g.won = false;
    g.fireworks_timer = 0;
    g.billboard_created = false;
    g.displayed_score = -1;
    g.center = { toX(TileCountX / 2), 0, toZ(TileCountZ / 2) };
    g.size = TileCountX * (TileSize + TileSpacing) + 2;
    std::cout << "Starting level " << g.current_level << " (" << g.enemies_to_spawn << " enemies)" << std::endl;

    // Camera, lighting & canvas configuration
    ecs.script().filename("etc/assets/app.flecs").run();

    // Prefab assets
    ecs.script().filename("etc/assets/materials.flecs").run();
    ecs.script().filename("etc/assets/tree.flecs").run();
    ecs.script().filename("etc/assets/tile.flecs").run();
    ecs.script().filename("etc/assets/particle.flecs").run();
    ecs.script().filename("etc/assets/bullet.flecs").run();
    ecs.script().filename("etc/assets/nozzle_flash.flecs").run();
    ecs.script().filename("etc/assets/smoke.flecs").run();
    ecs.script().filename("etc/assets/spark.flecs").run();
    ecs.script().filename("etc/assets/ion.flecs").run();
    ecs.script().filename("etc/assets/bolt.flecs").run();
    ecs.script().filename("etc/assets/firework.flecs").run();
    ecs.script().filename("etc/assets/enemy.flecs").run();
    ecs.script().filename("etc/assets/turret.flecs").run();
    ecs.script().filename("etc/assets/cannon.flecs").run();
    ecs.script().filename("etc/assets/laser.flecs").run();
}

// Build level
void init_level(flecs::world& ecs) {
    Game& g = ecs.ensure<Game>();

    grid<TileKind> *path = new grid<TileKind>(
        TileCountX * LevelScale, TileCountZ * LevelScale);

    Waypoints waypoints(path, {
        {0, 1}, {8, 1}, {8, 3}, {1, 3}, {1, 8}, {4, 8}, {4, 5}, {8, 5}, {8, 7},
        {6, 7}, {6, 9}, {11, 9}, {11, 1}, {18, 1}, {18, 3}, {16, 3}, {16, 5},
        {18, 5}, {18, 7}, {16, 7}, {16, 9}, {18, 9}, {18, 12}, {1, 12}, {1, 18},
        {3, 18}, {3, 15}, {5, 15}, {5, 18}, {7, 18}, {7, 15}, {9, 15}, {9, 18},
        {12, 18}, {12, 14}, {18, 14}, {18, 16}, {14, 16}, {14, 19}, {19, 19}
    });

    transform::Position2 spawn_point = {
        toX(LevelScale * TileCountX - 1), 
        toZ(LevelScale * TileCountZ - 1)
    };

    g.level = ecs.entity()
        .child_of<Level>()
        .set<Level>({path, spawn_point});

    ecs.entity("GroundPlane")
        .child_of<level>()
        .set<Position>({0, -2.7, toZ(TileCountZ / 2 - 0.5)})
        .set<Box>({toX(TileCountX + 0.5) * 20, 5, toZ(TileCountZ + 2) * 10})
        .set<Color>({0.11, 0.15, 0.1});

    for (int x = 0; x < TileCountX * LevelScale; x ++) {
        for (int z = 0; z < TileCountZ * LevelScale; z++) {
            float xc = toX(x);
            float zc = toZ(z);

            auto t = ecs.scope<level>().entity().set<Position>({xc, 0, zc});
            if (path[0](x, z) == TileKind::Path) {
                t.is_a<prefabs::Path>();
            } else if (path[0](x, z) == TileKind::Turret) {
                t.is_a<prefabs::Tile>();

                bool canTurret = false;
                if (x < (TileCountX * LevelScale - 1) && (z < (TileCountZ * LevelScale - 1))) {
                    canTurret |= (path[0](x + 1, z) == TileKind::Path);
                    canTurret |= (path[0](x, z + 1) == TileKind::Path);
                }
                if (x && z) {
                    canTurret |= (path[0](x - 1, z) == TileKind::Path);
                    canTurret |= (path[0](x, z - 1) == TileKind::Path);
                }

                auto e = ecs.entity().set<Position>({xc, TileHeight / 2, zc});
                if (!canTurret || (randf(1) > 0.3)) {
                    if (randf(1) > 0.05) {
                        e.child_of<level>();
                        e.set<prefabs::Tree>({
                            1.5f + randf(2.5),
                            randf(0.1)
                        });
                        e.set<Rotation>({0, randf(2.0 * M_PI)});
                    } else {
                        e.destruct();
                    }
                } else {
                    e.child_of<turrets>();
                    if (randf(1) > 0.3) {
                        e.is_a<prefabs::Cannon>();
                    } else {
                        e.is_a<prefabs::Laser>();
                        e.target<prefabs::Laser::Head::Beam>().disable();
                    }
                }
            } else if (path[0](x, z) == TileKind::Other) {
                t.is_a<prefabs::Tile>();
            }
        }
    }
}

void init_systems(flecs::world& ecs) {
    ecs.scope(ecs.entity("tower_defense"), [&](){ // Keep root scope clean

    // Spawn enemies periodically
    ecs.system<Game>("SpawnEnemy")
        .interval(EnemySpawnInterval)
        .each(SpawnEnemy);

    // Turret build/delete controls (B: cannon, L: laser, X/Delete: remove)
    ecs.system<const Input, const Game>("TurretControl")
        .each(TurretControl);

    // Move enemies
    ecs.system<Position, Direction, Game>("MoveEnemy")
        .with<Enemy>()
        .each(MoveEnemy);

    // Clear invalid target for turrets
    ecs.system<Target, Position>("ClearTarget")
        .each(ClearTarget);

    // Find target for turrets
    ecs.system<Turret, Target, Position, const SpatialQuery, SpatialQueryResult>
            ("FindTarget")
        .term_at(3).up(flecs::IsA).second<Enemy>()
        .term_at(4).second<Enemy>()
        .each(FindTarget);

    // Aim turret at enemies
    ecs.system<Turret, Target, Position>("AimTarget")
        .each(AimTarget);

    // Countdown until next fire
    ecs.system<Turret, Target>("FireCountdown")
        .each(FireCountdown);

    // Aim beam at target
    ecs.system<Position, Turret, Target>("BeamControl")
        .each(BeamControl);

    // Fire bullets at enemies
    ecs.system<Turret, Target, Position>("FireAtTarget")
        .with<Laser>().optional()
        .each(FireAtTarget);

    // Apply recoil to barrels
    ecs.system<Position, const Recoil>("ApplyRecoil")
        .each(ApplyRecoil);

    // Decrease recoil amount over time
    ecs.system<Recoil>("DecreaseRecoil")
        .each(DecreaseRecoil);

    // Decrease recoil amount over time
    ecs.system<HitCooldown>("DecreaseHitCooldown")
        .each(DecreaseHitCoolDown);

    // Simple particle system
    ecs.system<ParticleLifespan, const Particle, Box*, Color*, Velocity*>
            ("ProgressParticle")
        .term_at(1).up(flecs::IsA) // shared particle properties
        .each(ProgressParticle);

    // Test for collisions with enemies
    ecs.system<Position, Health, Box, HitCooldown, const SpatialQuery, SpatialQueryResult>
            ("HitTarget")
        .term_at(4).up(flecs::IsA).second<Bullet>()
        .term_at(5).second<Bullet>()
        .each(HitTarget);

    // Destroy enemy when health goes to 0
    ecs.system<Health, Position, Game>("DestroyEnemy")
        .with<Enemy>()
        .each(DestroyEnemy);

    // Decrease intensity of explosion light over time
    ecs.system<ExplosionLight, PointLight>("UpdateExplosionLight")
        .each([](flecs::iter& it, size_t i, ExplosionLight& l, PointLight& p) {
            flecs::entity e = it.entity(i);
            l.intensity -= l.decay * it.delta_time();
            if (l.intensity <= 0) {
                e.destruct();
            } else {
                p.intensity = l.intensity;
            }
        });

    // Victory celebration: spawn fireworks when player wins
    ecs.system<Game>("VictoryCelebration")
        .each(VictoryCelebration);

    // Score display: update HUD score when it changes
    ecs.system<Game>("ScoreDisplay")
        .each(ScoreDisplay);

    // Victory billboard: create large 3D text billboard on win
    ecs.system<Game>("VictoryBillboard")
        .each(VictoryBillboard);

    // Game Over billboard: create large 3D text billboard on loss
    ecs.system<Game>("GameOverBillboard")
        .each(GameOverBillboard);
    });
}

int main(int argc, char *argv[]) {
    flecs::world ecs(argc, argv);

    ecs.import<flecs::components::transform>();
    ecs.import<flecs::components::graphics>();
    ecs.import<flecs::components::geometry>();
    ecs.import<flecs::components::gui>();
    ecs.import<flecs::components::physics>();
    ecs.import<flecs::components::input>();
    ecs.import<flecs::systems::transform>();
    ecs.import<flecs::systems::physics>();
    ecs.import<flecs::game>();
    ecs.import<flecs::systems::sokol>();

    init_components(ecs);
    init_game(ecs);
    init_level(ecs);
    init_systems(ecs);

    ecs.app()
        .enable_rest()
        .enable_stats()
        .target_fps(60)
        .run();
}

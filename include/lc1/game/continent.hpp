#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lc1 {

enum class RegionType : std::uint8_t {
    Plain,     
    Forest,    
    Mountain,  
    Wasteland, 
    Sea,       
};

// Stable identities, independent of vector order. Zero is reserved for an invalid ID.
enum class RegionId : std::uint32_t {};
enum class LocationId : std::uint32_t {};
enum class RoadId : std::uint32_t {};

struct Region {
    RegionId id;
    std::string name;
    RegionType type = RegionType::Plain;
    bool walkable() const { return type != RegionType::Sea && type != RegionType::Mountain; }
};

struct TileData {
    RegionId region{1};
};

enum class ContinentSurface { Grass, Road, Stone, Plaster, Roof, Wood, Window, Foliage, Canvas };

// XZ coordinates, in metres. Geometry, movement and city placement share these bounds.
struct GroundBounds {
    glm::vec2 min;
    glm::vec2 max;
};

struct GroundPatch {
    GroundBounds bounds;
    ContinentSurface surface = ContinentSurface::Grass;
};

struct ContinentBlock {
    glm::vec3 min;
    glm::vec3 max;
    ContinentSurface surface = ContinentSurface::Stone;
    bool solid = true;
};

enum class LocationType : std::uint8_t { City, Village, Fortress, Port, Ruins, Wilderness };

// Spatial layout only. A location may span tiles/regions; region identifies its entrance.
// Ownership, resources and garrison belong to persistent game state, keyed by id.
struct Location {
    LocationId id;
    std::string name;
    LocationType type;
    RegionId region;
    GroundBounds bounds;
    glm::vec2 entrance;
};

// Undirected connection between location entrances. Points are an XZ centreline in metres.
// Roads paint the existing ground; they do not override terrain or building collision.
struct Road {
    RoadId id;
    std::string name;
    LocationId from;
    LocationId to;
    float width;
    std::vector<glm::vec2> points;

    float length() const;
    bool contains(glm::vec2 position, float clearance = 0.0F) const;
};

struct ContinentData {
    glm::vec2 origin;
    float tile_size;
    std::vector<std::vector<TileData>> tiles; // tiles[z][x]
    glm::vec3 spawn;
    std::vector<GroundPatch> patches;
    std::vector<ContinentBlock> blocks;
    std::vector<Region> regions = {{RegionId{1}, "Plain", RegionType::Plain}};
    std::vector<Location> locations;
    std::vector<Road> roads;
};

class Continent {
  public:
    explicit Continent(ContinentData data);
    // Optional application scenery, relative to spawn. Blocks participate in
    // both the continent's visible geometry and its static collision.
    static Continent make_prototype(std::span<ContinentBlock const> spawn_blocks = {});
    // The playable continent follows continent-present.svg, the source of the PNG.
    static Continent make_present(std::span<ContinentBlock const> spawn_blocks = {});

    GroundBounds bounds() const;
    float tile_size() const { return data_.tile_size; }
    std::size_t columns() const { return data_.tiles.front().size(); }
    std::size_t rows() const { return data_.tiles.size(); }
    std::optional<TileData> tile_at(glm::vec2 position) const;
    glm::vec3 spawn() const { return data_.spawn; }
    std::span<GroundPatch const> patches() const { return data_.patches; }
    std::span<ContinentBlock const> blocks() const { return data_.blocks; }
    std::span<Region const> regions() const { return data_.regions; }
    std::span<Location const> locations() const { return data_.locations; }
    std::span<Road const> roads() const { return data_.roads; }

    // Nullable, non-owning queries. Results borrow this immutable continent's storage.
    Region const *region(RegionId id) const;
    Location const *location(LocationId id) const;
    Road const *road(RoadId id) const;
    Region const *region_at(glm::vec2 position) const;
    Location const *location_at(glm::vec2 position) const;
    Road const *road_at(glm::vec2 position) const;
    std::vector<RoadId> roads_from(LocationId id) const;

    // The first terrain is flat. Absence means outside the continent or impassable terrain.
    std::optional<float> ground_height(glm::vec2 position) const;
    bool can_stand(glm::vec3 feet, float half_width, float height) const;

    // Sweeps an upright box, then slides along contact planes. Feet must start in a
    // valid location; the body cannot step, jump or climb in this first ground controller.
    glm::vec3 move(glm::vec3 feet, glm::vec2 displacement, float half_width, float height) const;

  private:
    ContinentData data_;
    // Cached passability in row-major order. Collision visits only the swept tile range.
    std::vector<std::uint8_t> blocked_tiles_;
};

} // namespace lc1

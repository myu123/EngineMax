#pragma once

#include <glm/glm.hpp>

struct AABB {
    glm::vec3 min;
    glm::vec3 max;
};

// Returns true if a and b overlap on all three axes
bool testOverlap(const AABB& a, const AABB& b);

// Returns the shortest push vector to move 'a' out of 'b'.
// Zero vector if no overlap.
glm::vec3 resolveCollision(const AABB& a, const AABB& b);

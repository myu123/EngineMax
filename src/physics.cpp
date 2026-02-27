#include "physics.h"

#include <cmath>
#include <limits>

bool testOverlap(const AABB& a, const AABB& b) {
    return (a.min.x < b.max.x && a.max.x > b.min.x) &&
           (a.min.y < b.max.y && a.max.y > b.min.y) &&
           (a.min.z < b.max.z && a.max.z > b.min.z);
}

glm::vec3 resolveCollision(const AABB& a, const AABB& b) {
    if (!testOverlap(a, b))
        return glm::vec3(0.0f);

    // For each axis, compute overlap in both directions
    // and pick the smallest overall to get the minimum push
    struct Candidate {
        float depth;
        glm::vec3 push;
    };

    Candidate candidates[6] = {
        { b.max.x - a.min.x, glm::vec3( (b.max.x - a.min.x), 0.0f, 0.0f) },  // push +x
        { a.max.x - b.min.x, glm::vec3(-(a.max.x - b.min.x), 0.0f, 0.0f) },  // push -x
        { b.max.y - a.min.y, glm::vec3(0.0f,  (b.max.y - a.min.y), 0.0f) },   // push +y
        { a.max.y - b.min.y, glm::vec3(0.0f, -(a.max.y - b.min.y), 0.0f) },   // push -y
        { b.max.z - a.min.z, glm::vec3(0.0f, 0.0f,  (b.max.z - a.min.z)) },   // push +z
        { a.max.z - b.min.z, glm::vec3(0.0f, 0.0f, -(a.max.z - b.min.z)) },   // push -z
    };

    float minDepth = std::numeric_limits<float>::max();
    glm::vec3 bestPush(0.0f);

    for (const auto& c : candidates) {
        if (c.depth < minDepth) {
            minDepth = c.depth;
            bestPush = c.push;
        }
    }

    return bestPush;
}

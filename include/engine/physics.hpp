#pragma once

namespace engine {
class Scene;

/** Advances physics-driven entities in a Scene.
 *
 * Gravity is measured in pixels per second squared. Positive gravity
 * accelerates entities toward positive screen y (downward).
 */
class PhysicsSystem {
public:
    explicit PhysicsSystem(float gravity);

    void setGravity(float gravity) noexcept;

    float gravity() const noexcept;

    void step(Scene& scene, float deltaSeconds) const;

    bool isCollision(Scene& scene, int entityID1, int entityID2) const;

private:
    float gravity_;
};
} // namespace engine

#include <array>
#include <cstddef>
#include <engine/engine.hpp>
#include <string>

int main() {
    constexpr int windowWidth = 1920;
    constexpr int windowHeight = 1080;

    constexpr float orbSize = 128.f;
    constexpr float laneWidth = static_cast<float>(windowWidth) / 5.f;

    constexpr float topStart = -orbSize;
    constexpr float bottomStart = static_cast<float>(windowHeight);

    engine::log::init();
    engine::log::info("starting gravity_demo");

    engine::Window window("gravity_demo", windowWidth, windowHeight);
    engine::Renderer renderer(window);

    const std::string assetDir = GRAVITY_DEMO_ASSET_DIR;
    const engine::TextureId orbTexture = renderer.loadTexture(assetDir + "orb.png");
    const engine::SpriteSheetId orbSheet =
        renderer.createSpriteSheet(orbTexture, engine::SpriteSheetLayout::grid({128.f, 128.f}, 4));

    engine::Scene scene;
    /*
     *  Creates an animated orb with its own velocity and gravity scale.
     *  The lambda returns the new entity ID.
     */
    const auto createOrb = [&](float x, float y, float initialVelocityY, float gravityScale) {
        const engine::EntityId id = scene.createEntity();
        scene.transform(id).position = {x, y};
        scene.addShape(id, {.size = {orbSize, orbSize}});
        scene.addSpriteAnimation(id,
                                 engine::SpriteAnimation::uniform(orbSheet, {0, 1, 2, 3}, 0.1f));
        scene.addRigidBody(id, {.velocity = {0.f, initialVelocityY}, .gravityScale = gravityScale});
        return id;
    };

    /*
     *  Places an orb in the horizontal center of a lane.
     */
    const auto laneX = [](std::size_t lane) {
        return static_cast<float>(lane) * laneWidth + (laneWidth - orbSize) * 0.5f;
    };

    /*
     *  Lane 0:
     *  No gravity, but constant downward velocity.
     */
    const engine::EntityId constantVelocityOrb = createOrb(laneX(0), topStart, 200.f, 0.f);

    /*
     *  Lane 1:
     *  Half-strength downward gravity.
     */
    const engine::EntityId halfGravityOrb = createOrb(laneX(1), topStart, 0.f, 0.5f);

    /*
     *  Lane 3:
     *  Double-strength downward gravity.
     */
    const engine::EntityId normalGravityOrb = createOrb(laneX(2), topStart, 0.f, 1.f);

    /*
     *  Lane 2:
     *  Normal downward gravity.
     */
    const engine::EntityId doubleGravityOrb = createOrb(laneX(3), topStart, 0.f, 2.f);

    /*
     * Lane 4:
     * Negative gravity scale, causing upward acceleration.
     */
    const engine::EntityId reverseGravityOrb = createOrb(laneX(4), bottomStart, 0.f, -1.f);

    /*
     *  Stores the information required to reset each orb.
     */
    struct OrbReset {
        engine::EntityId id;
        engine::Transform initialTransform;
        glm::vec2 initialVelocity;
        bool resetsAboveScreen;
    };

    const std::array<OrbReset, 5> resetRules{
        {{constantVelocityOrb, {.position = {laneX(0), topStart}}, {0.f, 200.f}, false},
         {halfGravityOrb, {.position = {laneX(1), topStart}}, {0.f, 0.f}, false},
         {normalGravityOrb, {.position = {laneX(2), topStart}}, {0.f, 0.f}, false},
         {doubleGravityOrb, {.position = {laneX(3), topStart}}, {0.f, 0.f}, false},
         {reverseGravityOrb, {.position = {laneX(4), bottomStart}}, {0.f, 0.f}, true}}};

    /*
     * Different lane colors make the five demonstrations
     * visually distinct without requiring text rendering.
     */
    const std::array<engine::Color, 5> laneColors{{
        {40, 120, 255, 255}, // bright blue
        {40, 200, 120, 255}, // bright green
        {30, 200, 220, 255}, // bright cyan
        {255, 150, 40, 255}, // bright orange
        {210, 70, 220, 255}  // bright purple
    }};

    engine::PhysicsSystem physics(980.f);
    engine::Clock clock;

    while (!window.shouldClose()) {
        window.pollEvents();
        clock.tick();

        const float deltaSeconds = clock.deltaSeconds();

        engine::advanceAnimations(scene, deltaSeconds);

        physics.step(scene, deltaSeconds);

        /*
         * Reset each orb independently when it leaves
         * the appropriate side of the screen.
         */
        for (const OrbReset& rule : resetRules) {
            engine::Transform& transform = scene.transform(rule.id);

            const bool leftThroughTop = transform.position.y + orbSize < 0.f;

            const bool leftThroughBottom = transform.position.y > static_cast<float>(windowHeight);

            const bool shouldReset = rule.resetsAboveScreen ? leftThroughTop : leftThroughBottom;

            if (shouldReset) {
                transform = rule.initialTransform;

                engine::RigidBody* rigidBody = scene.getRigidBody(rule.id);

                rigidBody->velocity = rule.initialVelocity;
            }
        }

        renderer.clear({0, 0, 0, 255});

        /*
         * Draw lane backgrounds before drawing entities.
         */
        for (std::size_t lane = 0; lane < laneColors.size(); ++lane) {
            renderer.fillRect({static_cast<float>(lane) * laneWidth, 0.f},
                              {laneWidth, static_cast<float>(windowHeight)}, laneColors[lane]);
        }

        renderer.drawEntities(scene);
        renderer.present();
    }

    engine::log::info("gravity_demo exiting cleanly");

    return 0;
}

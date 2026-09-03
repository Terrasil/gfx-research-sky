#include <gfx/research/application.hpp>
#include <gfx/research/csv_writer.hpp>
#include <gfx/research/framebuffer.hpp>
#include <gfx/research/fullscreen_triangle.hpp>
#include <gfx/research/gpu_timer.hpp>
#include <gfx/research/model.hpp>
#include <gfx/research/orbit_camera.hpp>
#include <gfx/research/primitives.hpp>
#include <gfx/research/screenshot.hpp>
#include <gfx/research/shader.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <utility>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace {
    constexpr std::array<const char*, 3> SKY_MODE_LABELS = {
        "Day clouds",
        "Night clouds",
        "Aurora"
    };

    constexpr std::array<const char*, 3> SKY_MODE_IDS = {
        "day-clouds",
        "night-clouds",
        "aurora"
    };

    struct LaunchOptions {
        bool publicationSuite = false;
        bool publicationQuick = false;
        std::filesystem::path publicationOutput = "results/publication";
    };

    struct PublicationCase {
        std::string pairId;
        std::string sweep;
        int step = 0;
        int skyMode = 0;
        bool proposed = false;
        bool nullTest = false;
        int targetWidth = 1920;
        int targetHeight = 1080;
        float skyRadius = 300.0f;
        float cameraOffsetX = 0.0f;
        float sphereX = 0.0f;
        int warmupFrames = 120;
        int sampleFrames = 600;
        bool capture = false;
    };

    struct ValidationResult {
        double backgroundHitError = std::numeric_limits<double>::quiet_NaN();
        double reflectionHitError = std::numeric_limits<double>::quiet_NaN();
    };

    struct TimingStats {
        double mean = 0.0;
        double median = 0.0;
        double stddev = 0.0;
        double p95 = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
    };

    glm::mat4 fittedModelTransform(
        const gfx::research::Model& model,
        const glm::vec3& groundPosition,
        const float targetSize,
        const float yawRadians
    ) {
        const glm::vec3 minimum = model.bounds_min();
        const glm::vec3 maximum = model.bounds_max();
        const glm::vec3 extent = maximum - minimum;
        const float largestExtent = std::max({extent.x, extent.y, extent.z, 1e-5f});
        const float scale = targetSize / largestExtent;
        const glm::vec3 anchor(
            (minimum.x + maximum.x) * 0.5f,
            minimum.y,
            (minimum.z + maximum.z) * 0.5f
        );

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, groundPosition);
        transform = glm::rotate(transform, yawRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::scale(transform, glm::vec3(scale));
        transform = glm::translate(transform, -anchor);
        return transform;
    }

    std::string scalarToken(const float value) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << value;
        std::string result = stream.str();
        std::replace(result.begin(), result.end(), '-', 'm');
        std::replace(result.begin(), result.end(), '.', 'p');
        return result;
    }

    TimingStats calculateStats(const std::vector<double>& values) {
        TimingStats result;
        if (values.empty()) return result;

        std::vector<double> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        result.minimum = sorted.front();
        result.maximum = sorted.back();
        result.mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());

        const std::size_t middle = sorted.size() / 2;
        result.median = sorted.size() % 2 == 0
            ? (sorted[middle - 1] + sorted[middle]) * 0.5
            : sorted[middle];

        double variance = 0.0;
        for (const double value : sorted) {
            const double difference = value - result.mean;
            variance += difference * difference;
        }
        variance /= static_cast<double>(sorted.size());
        result.stddev = std::sqrt(variance);

        const std::size_t p95Index = std::min(
            sorted.size() - 1,
            static_cast<std::size_t>(std::ceil(static_cast<double>(sorted.size()) * 0.95)) - 1
        );
        result.p95 = sorted[p95Index];
        return result;
    }

    bool intersectSphereReference(
        const glm::dvec3& origin,
        const glm::dvec3& directionInput,
        const glm::dvec3& center,
        const double radius,
        glm::dvec3& hit
    ) {
        const glm::dvec3 direction = glm::normalize(directionInput);
        const glm::dvec3 offset = origin - center;
        const double b = glm::dot(offset, direction);
        const double c = glm::dot(offset, offset) - radius * radius;
        const double discriminant = b * b - c;
        if (discriminant < 0.0) return false;

        const double root = std::sqrt(discriminant);
        const double nearT = -b - root;
        const double farT = -b + root;
        const double t = nearT > 1e-8 ? nearT : farT;
        if (t <= 1e-8) return false;
        hit = origin + direction * t;
        return true;
    }

    bool saveTexturePfm(
        const std::filesystem::path& path,
        const unsigned int texture,
        const int width,
        const int height
    ) {
        if (!texture || width <= 0 || height <= 0) return false;
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());

        std::vector<float> rgba(static_cast<std::size_t>(width) * height * 4);
        glGetTextureImage(
            texture,
            0,
            GL_RGBA,
            GL_FLOAT,
            static_cast<GLsizei>(rgba.size() * sizeof(float)),
            rgba.data()
        );

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream << "PF\n" << width << ' ' << height << "\n-1.0\n";
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
                stream.write(reinterpret_cast<const char*>(&rgba[index]), sizeof(float) * 3);
            }
        }
        return stream.good();
    }

    class SkyResearch final : public gfx::research::Application {
    public:
        explicit SkyResearch(LaunchOptions launchOptions) :
            Application({
                .title = "gfx-research-sky",
                .width = 1600,
                .height = 900,
                .vsync = false
            }),
            launchOptions_(std::move(launchOptions)) {}

    private:
        bool on_init() override {
            root_ = GFX_RESEARCH_SOURCE_DIR;

            const gfx::research::ShaderFile sceneFiles[] = {
                {gfx::research::ShaderStage::Vertex, root_ / "shaders/scene.vert"},
                {gfx::research::ShaderStage::Fragment, root_ / "shaders/scene.frag"}
            };
            const gfx::research::ShaderFile skyFiles[] = {
                {gfx::research::ShaderStage::Vertex, root_ / "shaders/fullscreen.vert"},
                {gfx::research::ShaderStage::Fragment, root_ / "shaders/sky.frag"}
            };
            if (!sceneShader_.load(sceneFiles) || !skyShader_.load(skyFiles)) return false;

            const gfx::research::ColorAttachmentDesc sceneAttachments[] = {
                {GL_RGBA16F, GL_LINEAR, GL_LINEAR},
                {GL_RGBA16F, GL_NEAREST, GL_NEAREST}
            };
            if (!sceneTarget_.create(config().width, config().height, sceneAttachments, true, GL_DEPTH_COMPONENT32F)) return false;

            const gfx::research::ColorAttachmentDesc postAttachments[] = {
                {GL_RGBA16F, GL_NEAREST, GL_NEAREST},
                {GL_RGBA32F, GL_NEAREST, GL_NEAREST},
                {GL_RGBA16F, GL_NEAREST, GL_NEAREST}
            };
            if (!postTarget_.create(config().width, config().height, postAttachments, false)) return false;

            reflectiveSphere_ = gfx::research::make_uv_sphere(1.0f, 96, 48);
            plane_ = gfx::research::make_plane(24.0f, 16);
            loadResearchModels();

            cameraBaseTarget_ = {0.0f, 0.95f, -0.55f};
            camera_.set_target(cameraBaseTarget_);
            camera_.set_distance(7.2f);
            camera_.set_yaw(0.54f);
            camera_.set_pitch(0.29f);
            camera_.set_fov(54.0f);
            camera_.set_clip(0.02f, 6000.0f);

            glEnable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);

            if (launchOptions_.publicationSuite || launchOptions_.publicationQuick) {
                startPublicationSuite(!launchOptions_.publicationQuick);
            }
            return true;
        }

        void on_resize(const int width, const int height) override {
            sceneTarget_.resize(width, height);
            postTarget_.resize(width, height);
        }

        void loadResearchModels() {
            const std::filesystem::path models = root_ / "assets/models";
            const std::filesystem::path legacyAssets = root_ / "assets";

            dragonLoaded_ = loadFirstPresent(dragon_, {
                models / "dragon.obj",
                models / "stanford_dragon.obj",
                legacyAssets / "dragon.obj",
                legacyAssets / "stanford_dragon.obj"
            });
            suzanneLoaded_ = loadFirstPresent(suzanne_, {
                models / "suzanne.obj",
                legacyAssets / "suzanne.obj"
            });
            teapotLoaded_ = loadFirstPresent(teapot_, {
                models / "teapot.obj",
                models / "utah_teapot.obj",
                legacyAssets / "teapot.obj",
                legacyAssets / "utah_teapot.obj"
            });
        }

        static bool loadFirstPresent(
            gfx::research::Model& model,
            const std::initializer_list<std::filesystem::path> paths
        ) {
            model.clear();
            for (const std::filesystem::path& path : paths) {
                if (std::filesystem::exists(path) && model.load(path)) return true;
            }
            return false;
        }

        void drawMesh(
            const gfx::research::Mesh& mesh,
            const glm::mat4& model,
            const glm::vec3& color,
            const float roughness,
            const glm::mat4& viewProjection
        ) {
            prepareObject(model, color, roughness, viewProjection);
            mesh.draw();
        }

        void drawModel(
            const gfx::research::Model& modelData,
            const glm::mat4& model,
            const glm::vec3& color,
            const float roughness,
            const glm::mat4& viewProjection
        ) {
            prepareObject(model, color, roughness, viewProjection);
            modelData.draw();
        }

        void prepareObject(
            const glm::mat4& model,
            const glm::vec3& color,
            const float roughness,
            const glm::mat4& viewProjection
        ) {
            sceneShader_.bind();
            sceneShader_.set_mat4("uModel", model);
            sceneShader_.set_mat4("uMvp", viewProjection * model);
            sceneShader_.set_vec3("uBaseColor", color);
            sceneShader_.set_float("uRoughness", roughness);
            sceneShader_.set_vec3("uEye", camera_.position());
        }

        void drawScene(const glm::mat4& viewProjection) {
            drawMesh(
                plane_,
                glm::translate(glm::mat4(1.0f), {0.0f, -0.015f, 0.0f}),
                {0.115f, 0.13f, 0.15f},
                floorRoughness_,
                viewProjection
            );

            if (showReflectiveSphere_) {
                glm::mat4 transform(1.0f);
                transform = glm::translate(transform, {reflectiveSphereX_, 0.95f, 0.15f});
                transform = glm::scale(transform, glm::vec3(0.78f));
                drawMesh(reflectiveSphere_, transform, {0.055f, 0.065f, 0.085f}, sphereRoughness_, viewProjection);
            }

            if (showDragon_ && dragonLoaded_) {
                drawModel(
                    dragon_,
                    fittedModelTransform(dragon_, {-2.35f, 0.0f, -0.65f}, 1.75f, 0.38f),
                    {0.29f, 0.18f, 0.115f},
                    0.28f,
                    viewProjection
                );
            }

            if (showSuzanne_ && suzanneLoaded_) {
                drawModel(
                    suzanne_,
                    fittedModelTransform(suzanne_, {2.22f, 0.0f, -0.35f}, 1.55f, -0.42f),
                    {0.16f, 0.29f, 0.23f},
                    0.34f,
                    viewProjection
                );
            }

            if (showTeapot_ && teapotLoaded_) {
                drawModel(
                    teapot_,
                    fittedModelTransform(teapot_, {0.15f, 0.0f, -2.25f}, 1.45f, -0.12f),
                    {0.24f, 0.18f, 0.31f},
                    0.20f,
                    viewProjection
                );
            }
        }

        void on_frame(const gfx::research::FrameInfo& frame) override {
            preparePublicationFrame(frame);

            sceneTarget_.bind();
            glViewport(0, 0, frame.width, frame.height);
            const float clearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
            const float clearNormal[] = {0.5f, 0.5f, 1.0f, 1.0f};
            glClearBufferfv(GL_COLOR, 0, clearColor);
            glClearBufferfv(GL_COLOR, 1, clearNormal);
            glClear(GL_DEPTH_BUFFER_BIT);

            const float aspect = static_cast<float>(frame.width) / static_cast<float>(frame.height);
            const glm::mat4 viewProjection = camera_.view_projection(aspect);
            drawScene(viewProjection);

            postTarget_.bind();
            glViewport(0, 0, frame.width, frame.height);
            glDisable(GL_DEPTH_TEST);
            const float clearPost[] = {0.0f, 0.0f, 0.0f, 1.0f};
            const float clearHit[] = {0.0f, 0.0f, 0.0f, 0.0f};
            glClearBufferfv(GL_COLOR, 0, clearPost);
            glClearBufferfv(GL_COLOR, 1, clearHit);
            glClearBufferfv(GL_COLOR, 2, clearPost);

            const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
            skyShader_.bind();
            glBindTextureUnit(0, sceneTarget_.color(0));
            glBindTextureUnit(1, sceneTarget_.color(1));
            glBindTextureUnit(2, sceneTarget_.depth());
            skyShader_.set_int("uSceneColor", 0);
            skyShader_.set_int("uNormalRoughness", 1);
            skyShader_.set_int("uDepth", 2);
            skyShader_.set_mat4("uInvViewProjection", inverseViewProjection);
            skyShader_.set_vec3("uEye", camera_.position());
            skyShader_.set_vec3("uSkyCenter", skyCenter_);
            skyShader_.set_float("uSkyRadius", skyRadius_);
            skyShader_.set_float("uSunElevation", sunElevation_);
            skyShader_.set_float("uCloudCoverage", cloudCoverage_);
            skyShader_.set_float("uCloudScale", cloudScale_);
            skyShader_.set_float("uCloudDensity", cloudDensity_);
            skyShader_.set_float("uStarIntensity", starIntensity_);
            skyShader_.set_float("uAuroraIntensity", auroraIntensity_);
            skyShader_.set_float("uLocalInfluence", nullTest_ ? 0.0f : localInfluence_);
            skyShader_.set_float("uTime", animateSky_ ? static_cast<float>(frame.time_seconds) : frozenTime_);
            skyShader_.set_int("uSkyMode", skyMode_);
            skyShader_.set_int("uMethod", proposed_ ? 1 : 0);

            gpuTimer_.begin();
            fullscreen_.draw();
            gpuTimer_.end();

            gfx::research::Framebuffer::bind_default();
            glNamedFramebufferReadBuffer(postTarget_.id(), GL_COLOR_ATTACHMENT0);
            glBlitNamedFramebuffer(
                postTarget_.id(),
                0,
                0,
                0,
                frame.width,
                frame.height,
                0,
                0,
                frame.width,
                frame.height,
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST
            );
            glEnable(GL_DEPTH_TEST);

            recordManualTiming(frame);
            processPublicationFrame(frame, viewProjection);
        }

        void recordManualTiming(const gfx::research::FrameInfo& frame) {
            if (!record_ || !gpuTimer_.has_result()) return;
            if (!csv_.is_open()) {
                csv_.open("results/sky-timing.csv");
                csv_.row(
                    "frame", "method", "sky_mode", "gpu_ms", "width", "height",
                    "sky_radius", "cloud_coverage", "cloud_scale", "cloud_density",
                    "star_intensity", "aurora_intensity", "local_influence", "null_test"
                );
            }
            csv_.row(
                frame.frame_index,
                proposed_ ? "local-ray" : "direction-only",
                SKY_MODE_IDS[static_cast<std::size_t>(skyMode_)],
                gpuTimer_.milliseconds(),
                frame.width,
                frame.height,
                skyRadius_,
                cloudCoverage_,
                cloudScale_,
                cloudDensity_,
                starIntensity_,
                auroraIntensity_,
                nullTest_ ? 0.0f : localInfluence_,
                nullTest_ ? 1 : 0
            );
        }

        void on_gui(const gfx::research::FrameInfo& frame) override {
            ImGui::Begin("Sky experiment");
            ImGui::Text("CPU %.3f ms | GPU postprocess %.3f ms", cpu_frame_milliseconds(), gpuTimer_.milliseconds());

            if (publicationRunning_) {
                const PublicationCase& current = publicationCases_[publicationCaseIndex_];
                ImGui::SeparatorText("Publication suite");
                ImGui::Text("Case %zu / %zu", publicationCaseIndex_ + 1, publicationCases_.size());
                ImGui::TextWrapped("%s", current.pairId.c_str());
                ImGui::ProgressBar(
                    publicationCases_.empty() ? 0.0f : static_cast<float>(publicationCaseIndex_) / publicationCases_.size(),
                    ImVec2(-1.0f, 0.0f)
                );
                if (ImGui::Button("Stop publication suite")) stopPublicationSuite(false);
            } else {
                ImGui::SeparatorText("Publication suite");
                if (ImGui::Button("Run quick publication suite")) startPublicationSuite(false);
                if (ImGui::Button("Run full publication suite")) startPublicationSuite(true);
            }

            ImGui::SeparatorText("Method");
            ImGui::Checkbox("Proposed local-ray sky", &proposed_);
            ImGui::Checkbox("Spatially invariant null test", &nullTest_);
            ImGui::Checkbox("Record CSV", &record_);
            ImGui::SliderFloat("Local influence", &localInfluence_, 0.0f, 1.5f, "%.2f");
            ImGui::SliderFloat("Sky radius", &skyRadius_, 10.0f, 5000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat3("Sky center", &skyCenter_.x, -100.0f, 100.0f);

            ImGui::SeparatorText("Procedural sky");
            ImGui::Combo("Sky mode", &skyMode_, SKY_MODE_LABELS.data(), static_cast<int>(SKY_MODE_LABELS.size()));
            ImGui::SliderAngle("Sun / moon elevation", &sunElevation_, 5.0f, 85.0f);
            if (skyMode_ != 2) {
                ImGui::SliderFloat("Cloud coverage", &cloudCoverage_, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Cloud scale", &cloudScale_, 0.25f, 3.0f, "%.2f");
                ImGui::SliderFloat("Cloud density", &cloudDensity_, 0.0f, 1.35f, "%.2f");
            }
            if (skyMode_ != 0) ImGui::SliderFloat("Star intensity", &starIntensity_, 0.0f, 3.0f, "%.2f");
            if (skyMode_ == 2) ImGui::SliderFloat("Aurora intensity", &auroraIntensity_, 0.0f, 3.0f, "%.2f");
            ImGui::Checkbox("Animate sky", &animateSky_);
            if (!animateSky_) ImGui::SliderFloat("Frozen time", &frozenTime_, 0.0f, 120.0f, "%.1f");

            ImGui::SeparatorText("Research scene");
            ImGui::Checkbox("Reflective sphere", &showReflectiveSphere_);
            ImGui::SliderFloat("Sphere X", &reflectiveSphereX_, -2.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Sphere roughness", &sphereRoughness_, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Floor roughness", &floorRoughness_, 0.0f, 1.0f, "%.2f");
            ImGui::Checkbox("Stanford Dragon", &showDragon_);
            ImGui::SameLine();
            assetStatus(dragonLoaded_);
            ImGui::Checkbox("Suzanne", &showSuzanne_);
            ImGui::SameLine();
            assetStatus(suzanneLoaded_);
            ImGui::Checkbox("Utah Teapot", &showTeapot_);
            ImGui::SameLine();
            assetStatus(teapotLoaded_);
            if (ImGui::Button("Reload models")) loadResearchModels();
            if (!dragonLoaded_ || !suzanneLoaded_ || !teapotLoaded_) {
                ImGui::TextWrapped("Missing classic assets. Run the CMake target fetch-assets or: python tools/fetch_assets.py");
            }

            ImGui::SeparatorText("Camera");
            ImGui::SliderFloat("Camera distance", &camera_.distance(), 2.0f, 20.0f);
            ImGui::SliderAngle("Camera yaw", &camera_.yaw(), -180.0f, 180.0f);
            ImGui::SliderAngle("Camera pitch", &camera_.pitch(), -80.0f, 80.0f);
            ImGui::SliderFloat3("Camera target", &camera_.target().x, -4.0f, 4.0f);

            ImGui::Separator();
            if (ImGui::Button("Reload shaders")) {
                sceneShader_.reload();
                skyShader_.reload();
            }
            ImGui::SameLine();
            if (ImGui::Button("Screenshot")) saveScreenshot(frame);
            ImGui::End();
        }

        static void assetStatus(const bool loaded) {
            if (loaded) ImGui::TextDisabled("[loaded]");
            else ImGui::Text("[missing]");
        }

        void saveScreenshot(const gfx::research::FrameInfo& frame) const {
            const std::string filename = std::string("results/sky-")
                + SKY_MODE_IDS[static_cast<std::size_t>(skyMode_)]
                + (proposed_ ? "-local-ray.png" : "-direction-only.png");
            gfx::research::save_framebuffer_png(filename, frame.width, frame.height);
        }

        void startPublicationSuite(const bool full) {
            stopPublicationSuite(false);
            publicationCases_ = makePublicationCases(full);
            publicationCaseIndex_ = 0;
            publicationCaseApplied_ = false;
            publicationRunning_ = !publicationCases_.empty();
            publicationFull_ = full;
            publicationOutput_ = launchOptions_.publicationOutput;
            std::filesystem::create_directories(publicationOutput_ / "images");
            std::filesystem::create_directories(publicationOutput_ / "hdr");

            publicationRaw_.open(publicationOutput_ / "raw.csv", std::ios::out | std::ios::trunc);
            publicationSummary_.open(publicationOutput_ / "summary.csv", std::ios::out | std::ios::trunc);
            publicationCaptures_.open(publicationOutput_ / "captures.csv", std::ios::out | std::ios::trunc);
            publicationManifest_.open(publicationOutput_ / "manifest.txt", std::ios::out | std::ios::trunc);

            publicationRaw_ << "case_id,pair_id,sweep,step,method,sky_mode,sample,gpu_ms,width,height,target_width,target_height,sky_radius,camera_offset_x,sphere_x,null_test\n";
            publicationSummary_ << "case_id,pair_id,sweep,step,method,sky_mode,samples,mean_ms,median_ms,stddev_ms,p95_ms,min_ms,max_ms,width,height,target_width,target_height,resolution_matched,sky_radius,camera_offset_x,sphere_x,null_test,background_hit_error,reflection_hit_error\n";
            publicationCaptures_ << "case_id,pair_id,sweep,step,method,sky_mode,png,pfm\n";
            publicationManifest_
                << "gfx-research-sky publication suite\n"
                << "profile=" << (full ? "full" : "quick") << '\n'
                << "frozen_time=" << frozenTime_ << '\n'
                << "cloud_coverage=" << cloudCoverage_ << '\n'
                << "cloud_scale=" << cloudScale_ << '\n'
                << "cloud_density=" << cloudDensity_ << '\n'
                << "star_intensity=" << starIntensity_ << '\n'
                << "aurora_intensity=" << auroraIntensity_ << '\n'
                << "local_influence=" << localInfluence_ << '\n'
                << "dragon_loaded=" << dragonLoaded_ << '\n'
                << "suzanne_loaded=" << suzanneLoaded_ << '\n'
                << "teapot_loaded=" << teapotLoaded_ << '\n';

            std::cout << "Publication suite started: " << publicationCases_.size() << " cases -> "
                      << publicationOutput_.string() << '\n';
        }

        void stopPublicationSuite(const bool completed) {
            if (publicationRaw_.is_open()) publicationRaw_.close();
            if (publicationSummary_.is_open()) publicationSummary_.close();
            if (publicationCaptures_.is_open()) publicationCaptures_.close();
            if (publicationManifest_.is_open()) {
                publicationManifest_ << "completed=" << (completed ? 1 : 0) << '\n';
                publicationManifest_.close();
            }
            publicationRunning_ = false;
            publicationCaseApplied_ = false;
            publicationSamples_.clear();
        }

        std::vector<PublicationCase> makePublicationCases(const bool full) const {
            std::vector<PublicationCase> cases;
            const std::array<std::pair<int, int>, 2> fullResolutions = {{{1920, 1080}, {2560, 1440}}};
            const std::array<std::pair<int, int>, 1> quickResolutions = {{{1920, 1080}}};

            const int timingWarmup = full ? 120 : 30;
            const int timingSamples = full ? 600 : 120;
            const int sweepWarmup = full ? 16 : 8;
            const int sweepSamples = full ? 64 : 24;
            const int radiusWarmup = full ? 48 : 16;
            const int radiusSamples = full ? 180 : 60;
            const int nullWarmup = full ? 64 : 24;
            const int nullSamples = full ? 240 : 80;

            const auto appendMethodPair = [&](PublicationCase base) {
                for (const bool proposed : {false, true}) {
                    PublicationCase test = base;
                    test.proposed = proposed;
                    cases.push_back(std::move(test));
                }
            };

            const auto appendTimingForResolution = [&](const int width, const int height) {
                for (int mode = 0; mode < 3; ++mode) {
                    PublicationCase test;
                    test.pairId = std::string("timing-") + SKY_MODE_IDS[mode] + '-' + std::to_string(width) + 'x' + std::to_string(height);
                    test.sweep = "timing";
                    test.skyMode = mode;
                    test.targetWidth = width;
                    test.targetHeight = height;
                    test.warmupFrames = timingWarmup;
                    test.sampleFrames = timingSamples;
                    test.capture = true;
                    appendMethodPair(test);
                }
            };

            if (full) {
                for (const auto [width, height] : fullResolutions) appendTimingForResolution(width, height);
            } else {
                for (const auto [width, height] : quickResolutions) appendTimingForResolution(width, height);
            }

            const int translationSteps = full ? 21 : 11;
            for (int mode = 0; mode < 3; ++mode) {
                for (int step = 0; step < translationSteps; ++step) {
                    const float t = translationSteps == 1 ? 0.0f : static_cast<float>(step) / static_cast<float>(translationSteps - 1);
                    const float offset = -1.5f + t * 3.0f;
                    PublicationCase test;
                    test.pairId = std::string("camera-") + SKY_MODE_IDS[mode] + "-x-" + scalarToken(offset);
                    test.sweep = "camera_translation";
                    test.step = step;
                    test.skyMode = mode;
                    test.cameraOffsetX = offset;
                    test.warmupFrames = sweepWarmup;
                    test.sampleFrames = sweepSamples;
                    test.capture = step == 0 || step == translationSteps / 2 || step == translationSteps - 1;
                    appendMethodPair(test);
                }
            }

            for (int mode = 0; mode < 3; ++mode) {
                for (int step = 0; step < translationSteps; ++step) {
                    const float t = translationSteps == 1 ? 0.0f : static_cast<float>(step) / static_cast<float>(translationSteps - 1);
                    const float offset = -1.5f + t * 3.0f;
                    PublicationCase test;
                    test.pairId = std::string("object-") + SKY_MODE_IDS[mode] + "-x-" + scalarToken(offset);
                    test.sweep = "object_translation";
                    test.step = step;
                    test.skyMode = mode;
                    test.sphereX = offset;
                    test.warmupFrames = sweepWarmup;
                    test.sampleFrames = sweepSamples;
                    test.capture = step == 0 || step == translationSteps / 2 || step == translationSteps - 1;
                    appendMethodPair(test);
                }
            }

            const std::vector<float> radii = full
                ? std::vector<float>{75.0f, 150.0f, 300.0f, 600.0f, 1200.0f}
                : std::vector<float>{100.0f, 300.0f, 900.0f};
            for (int mode = 0; mode < 3; ++mode) {
                for (std::size_t step = 0; step < radii.size(); ++step) {
                    PublicationCase test;
                    test.pairId = std::string("radius-") + SKY_MODE_IDS[mode] + '-' + scalarToken(radii[step]);
                    test.sweep = "radius";
                    test.step = static_cast<int>(step);
                    test.skyMode = mode;
                    test.skyRadius = radii[step];
                    test.warmupFrames = radiusWarmup;
                    test.sampleFrames = radiusSamples;
                    appendMethodPair(test);
                }
            }

            const auto appendNullAtResolution = [&](const int width, const int height) {
                for (int mode = 0; mode < 3; ++mode) {
                    PublicationCase test;
                    test.pairId = std::string("null-") + SKY_MODE_IDS[mode] + '-' + std::to_string(width) + 'x' + std::to_string(height);
                    test.sweep = "null";
                    test.skyMode = mode;
                    test.nullTest = true;
                    test.targetWidth = width;
                    test.targetHeight = height;
                    test.warmupFrames = nullWarmup;
                    test.sampleFrames = nullSamples;
                    test.capture = true;
                    appendMethodPair(test);
                }
            };

            if (full) {
                for (const auto [width, height] : fullResolutions) appendNullAtResolution(width, height);
            } else {
                for (const auto [width, height] : quickResolutions) appendNullAtResolution(width, height);
            }
            return cases;
        }

        void preparePublicationFrame(const gfx::research::FrameInfo& frame) {
            if (!publicationRunning_ || publicationCaseIndex_ >= publicationCases_.size()) return;
            if (publicationCaseApplied_) return;

            const PublicationCase& test = publicationCases_[publicationCaseIndex_];
            skyMode_ = test.skyMode;
            proposed_ = test.proposed;
            nullTest_ = test.nullTest;
            skyRadius_ = test.skyRadius;
            reflectiveSphereX_ = test.sphereX;
            camera_.set_target(cameraBaseTarget_ + glm::vec3(test.cameraOffsetX, 0.0f, 0.0f));
            animateSky_ = false;

            publicationSamples_.clear();
            publicationValidation_ = {};
            publicationWarmupRemaining_ = test.warmupFrames;
            publicationSampleIndex_ = 0;
            publicationResizeWait_ = 0;
            publicationResolutionMatched_ = false;
            requestFramebufferSize(test.targetWidth, test.targetHeight, frame.width, frame.height);
            publicationCaseApplied_ = true;
        }

        void requestFramebufferSize(const int targetWidth, const int targetHeight, const int framebufferWidth, const int framebufferHeight) const {
            int windowWidth = 1;
            int windowHeight = 1;
            glfwGetWindowSize(window(), &windowWidth, &windowHeight);
            const double scaleX = windowWidth > 0 ? static_cast<double>(framebufferWidth) / windowWidth : 1.0;
            const double scaleY = windowHeight > 0 ? static_cast<double>(framebufferHeight) / windowHeight : 1.0;
            const int requestedWidth = std::max(1, static_cast<int>(std::lround(targetWidth / std::max(scaleX, 0.01))));
            const int requestedHeight = std::max(1, static_cast<int>(std::lround(targetHeight / std::max(scaleY, 0.01))));
            glfwSetWindowSize(window(), requestedWidth, requestedHeight);
        }

        bool publicationResolutionReady(const gfx::research::FrameInfo& frame, const PublicationCase& test) {
            const bool exact = frame.width == test.targetWidth && frame.height == test.targetHeight;
            if (exact) {
                publicationResolutionMatched_ = true;
                return true;
            }
            ++publicationResizeWait_;
            if (publicationResizeWait_ < 180) return false;

            if (publicationResizeWait_ == 180) {
                std::cerr << "Publication suite: requested " << test.targetWidth << 'x' << test.targetHeight
                          << " but framebuffer is " << frame.width << 'x' << frame.height
                          << ". Continuing and recording actual resolution.\n";
            }
            return true;
        }

        void processPublicationFrame(const gfx::research::FrameInfo& frame, const glm::mat4& viewProjection) {
            if (!publicationRunning_ || !publicationCaseApplied_ || publicationCaseIndex_ >= publicationCases_.size()) return;
            const PublicationCase& test = publicationCases_[publicationCaseIndex_];
            if (!publicationResolutionReady(frame, test)) return;

            if (publicationWarmupRemaining_ > 0) {
                --publicationWarmupRemaining_;
                return;
            }
            if (!gpuTimer_.has_result()) return;

            if (publicationSampleIndex_ == 0) publicationValidation_ = validateCurrentFrame(frame, viewProjection);

            const double gpuMs = gpuTimer_.milliseconds();
            publicationSamples_.push_back(gpuMs);
            publicationRaw_
                << caseId(test) << ',' << test.pairId << ',' << test.sweep << ',' << test.step << ','
                << methodId(test.proposed) << ',' << SKY_MODE_IDS[static_cast<std::size_t>(test.skyMode)] << ','
                << publicationSampleIndex_ << ',' << std::setprecision(12) << gpuMs << ','
                << frame.width << ',' << frame.height << ',' << test.targetWidth << ',' << test.targetHeight << ','
                << test.skyRadius << ',' << test.cameraOffsetX << ',' << test.sphereX << ',' << (test.nullTest ? 1 : 0) << '\n';
            ++publicationSampleIndex_;

            if (publicationSampleIndex_ < test.sampleFrames) return;

            if (test.capture) capturePublicationCase(test, frame);
            finishPublicationCase(test, frame);
            ++publicationCaseIndex_;
            publicationCaseApplied_ = false;

            if (publicationCaseIndex_ >= publicationCases_.size()) {
                std::cout << "Publication suite complete. Analyze with: python tools/analyze_publication.py "
                          << publicationOutput_.string() << '\n';
                stopPublicationSuite(true);
                if (launchOptions_.publicationSuite || launchOptions_.publicationQuick) close();
            }
        }

        void finishPublicationCase(const PublicationCase& test, const gfx::research::FrameInfo& frame) {
            const TimingStats stats = calculateStats(publicationSamples_);
            publicationSummary_
                << caseId(test) << ',' << test.pairId << ',' << test.sweep << ',' << test.step << ','
                << methodId(test.proposed) << ',' << SKY_MODE_IDS[static_cast<std::size_t>(test.skyMode)] << ','
                << publicationSamples_.size() << ','
                << std::setprecision(12) << stats.mean << ',' << stats.median << ',' << stats.stddev << ','
                << stats.p95 << ',' << stats.minimum << ',' << stats.maximum << ','
                << frame.width << ',' << frame.height << ',' << test.targetWidth << ',' << test.targetHeight << ','
                << (publicationResolutionMatched_ ? 1 : 0) << ',' << test.skyRadius << ','
                << test.cameraOffsetX << ',' << test.sphereX << ',' << (test.nullTest ? 1 : 0) << ','
                << publicationValidation_.backgroundHitError << ',' << publicationValidation_.reflectionHitError << '\n';
            publicationRaw_.flush();
            publicationSummary_.flush();
        }

        void capturePublicationCase(const PublicationCase& test, const gfx::research::FrameInfo& frame) {
            const std::string id = caseId(test);
            const std::filesystem::path pngRelative = std::filesystem::path("images") / (id + ".png");
            const std::filesystem::path pfmRelative = std::filesystem::path("hdr") / (id + ".pfm");
            const std::filesystem::path png = publicationOutput_ / pngRelative;
            const std::filesystem::path pfm = publicationOutput_ / pfmRelative;
            gfx::research::Framebuffer::bind_default();
            const bool pngSaved = gfx::research::save_framebuffer_png(png, frame.width, frame.height);
            const bool pfmSaved = saveTexturePfm(pfm, postTarget_.color(2), frame.width, frame.height);
            publicationCaptures_
                << id << ',' << test.pairId << ',' << test.sweep << ',' << test.step << ','
                << methodId(test.proposed) << ',' << SKY_MODE_IDS[static_cast<std::size_t>(test.skyMode)] << ','
                << (pngSaved ? pngRelative.generic_string() : "") << ',' << (pfmSaved ? pfmRelative.generic_string() : "") << '\n';
            publicationCaptures_.flush();
        }

        ValidationResult validateCurrentFrame(const gfx::research::FrameInfo& frame, const glm::mat4& viewProjection) const {
            ValidationResult result;
            result.backgroundHitError = validateBackgroundHit(frame, viewProjection);
            result.reflectionHitError = validateReflectionHit(frame, viewProjection);
            return result;
        }

        double validateBackgroundHit(const gfx::research::FrameInfo& frame, const glm::mat4& viewProjection) const {
            constexpr std::array<glm::vec2, 6> candidates = {
                glm::vec2(0.82f, 0.82f), glm::vec2(0.18f, 0.82f), glm::vec2(0.86f, 0.62f),
                glm::vec2(0.14f, 0.62f), glm::vec2(0.72f, 0.92f), glm::vec2(0.28f, 0.92f)
            };

            int x = 0;
            int y = 0;
            bool found = false;
            for (const glm::vec2& uv : candidates) {
                x = std::clamp(static_cast<int>(uv.x * frame.width), 0, frame.width - 1);
                y = std::clamp(static_cast<int>(uv.y * frame.height), 0, frame.height - 1);
                if (readDepth(x, y) >= 0.99999f) {
                    found = true;
                    break;
                }
            }
            if (!found) return std::numeric_limits<double>::quiet_NaN();

            const glm::vec4 gpuHit = readRgba(postTarget_.color(1), x, y);
            if (gpuHit.w < 0.5f) return std::numeric_limits<double>::quiet_NaN();

            const glm::dmat4 inverseViewProjection = glm::inverse(glm::dmat4(viewProjection));
            const glm::dvec3 farPoint = reconstructWorldReference(x, y, 1.0, frame, inverseViewProjection);
            const glm::dvec3 origin = glm::dvec3(camera_.position());
            const glm::dvec3 direction = glm::normalize(farPoint - origin);
            glm::dvec3 expected;
            if (!intersectSphereReference(origin, direction, glm::dvec3(skyCenter_), skyRadius_, expected)) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            return glm::length(glm::dvec3(gpuHit) - expected);
        }

        double validateReflectionHit(const gfx::research::FrameInfo& frame, const glm::mat4& viewProjection) const {
            if (!showReflectiveSphere_) return std::numeric_limits<double>::quiet_NaN();
            const glm::vec4 clip = viewProjection * glm::vec4(reflectiveSphereX_, 0.95f, 0.15f, 1.0f);
            if (clip.w <= 0.0f) return std::numeric_limits<double>::quiet_NaN();
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f) return std::numeric_limits<double>::quiet_NaN();

            const int x = std::clamp(static_cast<int>((ndc.x * 0.5f + 0.5f) * frame.width), 0, frame.width - 1);
            const int y = std::clamp(static_cast<int>((ndc.y * 0.5f + 0.5f) * frame.height), 0, frame.height - 1);
            const float depth = readDepth(x, y);
            if (depth >= 0.99999f) return std::numeric_limits<double>::quiet_NaN();

            const glm::vec4 normalRoughness = readRgba(sceneTarget_.color(1), x, y);
            const glm::dvec3 normal = glm::normalize(glm::dvec3(normalRoughness) * 2.0 - glm::dvec3(1.0));
            const glm::dmat4 inverseViewProjection = glm::inverse(glm::dmat4(viewProjection));
            const glm::dvec3 world = reconstructWorldReference(x, y, depth, frame, inverseViewProjection);
            const glm::dvec3 eye = glm::dvec3(camera_.position());
            const glm::dvec3 incident = glm::normalize(world - eye);
            const glm::dvec3 reflected = glm::reflect(incident, normal);
            const glm::dvec3 origin = world + normal * 0.002;

            glm::dvec3 expected;
            if (!intersectSphereReference(origin, reflected, glm::dvec3(skyCenter_), skyRadius_, expected)) {
                return std::numeric_limits<double>::quiet_NaN();
            }

            const glm::vec4 gpuHit = readRgba(postTarget_.color(1), x, y);
            if (gpuHit.w < 0.5f) return std::numeric_limits<double>::quiet_NaN();
            return glm::length(glm::dvec3(gpuHit) - expected);
        }

        float readDepth(const int x, const int y) const {
            float depth = 1.0f;
            glGetTextureSubImage(
                sceneTarget_.depth(),
                0,
                x,
                y,
                0,
                1,
                1,
                1,
                GL_DEPTH_COMPONENT,
                GL_FLOAT,
                sizeof(depth),
                &depth
            );
            return depth;
        }

        static glm::vec4 readRgba(const unsigned int texture, const int x, const int y) {
            glm::vec4 result(0.0f);
            glGetTextureSubImage(
                texture,
                0,
                x,
                y,
                0,
                1,
                1,
                1,
                GL_RGBA,
                GL_FLOAT,
                sizeof(result),
                &result
            );
            return result;
        }

        static glm::dvec3 reconstructWorldReference(
            const int x,
            const int y,
            const double depth,
            const gfx::research::FrameInfo& frame,
            const glm::dmat4& inverseViewProjection
        ) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(frame.width);
            const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(frame.height);
            const glm::dvec4 clip(u * 2.0 - 1.0, v * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
            const glm::dvec4 world = inverseViewProjection * clip;
            return glm::dvec3(world) / world.w;
        }

        static const char* methodId(const bool proposed) {
            return proposed ? "local-ray" : "direction-only";
        }

        static std::string caseId(const PublicationCase& test) {
            return test.pairId + '-' + methodId(test.proposed);
        }

        LaunchOptions launchOptions_;
        std::filesystem::path root_;
        gfx::research::Shader sceneShader_;
        gfx::research::Shader skyShader_;
        gfx::research::Framebuffer sceneTarget_;
        gfx::research::Framebuffer postTarget_;
        gfx::research::FullscreenTriangle fullscreen_;
        gfx::research::GpuTimer gpuTimer_;
        gfx::research::Mesh reflectiveSphere_;
        gfx::research::Mesh plane_;
        gfx::research::Model dragon_;
        gfx::research::Model suzanne_;
        gfx::research::Model teapot_;
        gfx::research::OrbitCamera camera_;
        gfx::research::CsvWriter csv_;

        glm::vec3 cameraBaseTarget_{0.0f, 0.95f, -0.55f};
        glm::vec3 skyCenter_{0.0f, 0.0f, 0.0f};
        float skyRadius_ = 300.0f;
        float sunElevation_ = 0.58f;
        float cloudCoverage_ = 0.52f;
        float cloudScale_ = 1.0f;
        float cloudDensity_ = 1.0f;
        float starIntensity_ = 1.0f;
        float auroraIntensity_ = 1.15f;
        float localInfluence_ = 1.0f;
        float frozenTime_ = 12.0f;
        float reflectiveSphereX_ = 0.0f;
        float sphereRoughness_ = 0.05f;
        float floorRoughness_ = 0.38f;
        int skyMode_ = 0;
        bool proposed_ = true;
        bool nullTest_ = false;
        bool record_ = false;
        bool animateSky_ = false;
        bool showReflectiveSphere_ = true;
        bool showDragon_ = true;
        bool showSuzanne_ = true;
        bool showTeapot_ = true;
        bool dragonLoaded_ = false;
        bool suzanneLoaded_ = false;
        bool teapotLoaded_ = false;

        std::vector<PublicationCase> publicationCases_;
        std::vector<double> publicationSamples_;
        ValidationResult publicationValidation_;
        std::filesystem::path publicationOutput_;
        std::ofstream publicationRaw_;
        std::ofstream publicationSummary_;
        std::ofstream publicationCaptures_;
        std::ofstream publicationManifest_;
        std::size_t publicationCaseIndex_ = 0;
        int publicationWarmupRemaining_ = 0;
        int publicationSampleIndex_ = 0;
        int publicationResizeWait_ = 0;
        bool publicationRunning_ = false;
        bool publicationFull_ = false;
        bool publicationCaseApplied_ = false;
        bool publicationResolutionMatched_ = false;
    };

    LaunchOptions parseLaunchOptions(const int argc, char** argv) {
        LaunchOptions options;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--publication-suite") {
                options.publicationSuite = true;
            } else if (argument == "--publication-quick") {
                options.publicationQuick = true;
            } else if (argument.starts_with("--publication-output=")) {
                options.publicationOutput = std::string(argument.substr(std::string_view("--publication-output=").size()));
            }
        }
        return options;
    }
}

int main(const int argc, char** argv) {
    return SkyResearch{parseLaunchOptions(argc, argv)}.run();
}

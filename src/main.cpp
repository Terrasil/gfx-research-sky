#include <gfx/research/application.hpp>
#include <gfx/research/assets.hpp>
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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
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
    constexpr std::array<const char*, 4> SKY_MODE_LABELS = {
        "Day clouds",
        "Night clouds",
        "Aurora",
        "Synthetic world field"
    };

    constexpr std::array<const char*, 4> SKY_MODE_IDS = {
        "day-clouds",
        "night-clouds",
        "aurora",
        "world-field"
    };

    constexpr std::array<const char*, 3> METHOD_LABELS = {
        "Direction-only",
        "Fixed world sphere (legacy)",
        "Per-origin virtual sphere"
    };

    constexpr int METHOD_DIRECTION_ONLY = 0;
    constexpr int METHOD_FIXED_DOMAIN = 1;
    constexpr int METHOD_PER_ORIGIN_SPHERE = 2;
    constexpr int METHOD_EXPLICIT_POSITION_REFERENCE = 3;

    struct LaunchOptions {
        bool publicationSuite = false;
        bool publicationQuick = false;
        std::filesystem::path publicationOutput = "results/publication";
    };

    struct PublicationCase {
        std::string pairId;
        std::string groupId;
        std::string sweep;
        int step = 0;
        int repeat = 0;
        int skyMode = 0;
        int method = METHOD_PER_ORIGIN_SPHERE;
        bool reflectionsEnabled = true;
        bool quality = false;
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
        double worldPositionRmse = std::numeric_limits<double>::quiet_NaN();
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

    struct ImageMetrics {
        double rmse = 0.0;
        double relativeRmse = 0.0;
        double psnr = 0.0;
        double maximumAbsolute = 0.0;
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

    glm::dvec3 perOriginSphereReference(
        const glm::dvec3& origin,
        const glm::dvec3& directionInput,
        const double radius
    ) {
        return origin + glm::normalize(directionInput) * radius;
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

    std::vector<float> readTextureRgba(const unsigned int texture, const int width, const int height) {
        std::vector<float> rgba(static_cast<std::size_t>(width) * height * 4);
        glGetTextureImage(
            texture,
            0,
            GL_RGBA,
            GL_FLOAT,
            static_cast<GLsizei>(rgba.size() * sizeof(float)),
            rgba.data()
        );
        return rgba;
    }

    ImageMetrics calculateImageMetricsRegion(
        const std::vector<float>& image,
        const std::vector<float>& reference,
        const int width,
        const int height,
        const int x0,
        const int y0,
        const int x1,
        const int y1
    ) {
        ImageMetrics result;
        if (image.size() != reference.size() || image.empty() || width <= 0 || height <= 0) {
            result.rmse = result.relativeRmse = result.psnr = result.maximumAbsolute = std::numeric_limits<double>::quiet_NaN();
            return result;
        }

        const int minX = std::clamp(x0, 0, width);
        const int minY = std::clamp(y0, 0, height);
        const int maxX = std::clamp(x1, minX, width);
        const int maxY = std::clamp(y1, minY, height);
        long double squaredError = 0.0;
        long double squaredReference = 0.0;
        double peak = 1.0;
        double maximumAbsolute = 0.0;
        std::size_t channelCount = 0;
        for (int y = minY; y < maxY; ++y) {
            for (int x = minX; x < maxX; ++x) {
                const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
                for (int channel = 0; channel < 3; ++channel) {
                    const double a = image[index + channel];
                    const double b = reference[index + channel];
                    const double difference = a - b;
                    squaredError += difference * difference;
                    squaredReference += b * b;
                    peak = std::max(peak, std::abs(b));
                    maximumAbsolute = std::max(maximumAbsolute, std::abs(difference));
                    ++channelCount;
                }
            }
        }
        if (channelCount == 0) {
            result.rmse = result.relativeRmse = result.psnr = result.maximumAbsolute = std::numeric_limits<double>::quiet_NaN();
            return result;
        }

        const double mse = static_cast<double>(squaredError / channelCount);
        const double referenceMse = static_cast<double>(squaredReference / channelCount);
        result.rmse = std::sqrt(mse);
        result.relativeRmse = result.rmse / std::max(std::sqrt(referenceMse), 1e-12);
        result.psnr = mse <= 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10((peak * peak) / mse);
        result.maximumAbsolute = maximumAbsolute;
        return result;
    }

    ImageMetrics calculateImageMetrics(
        const std::vector<float>& image,
        const std::vector<float>& reference,
        const int width,
        const int height
    ) {
        return calculateImageMetricsRegion(image, reference, width, height, 0, 0, width, height);
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
                {GL_RGBA16F, GL_NEAREST, GL_NEAREST},
                {GL_RGBA32F, GL_NEAREST, GL_NEAREST}
            };
            if (!sceneTarget_.create(config().width, config().height, sceneAttachments, true, GL_DEPTH_COMPONENT32F)) return false;

            const gfx::research::ColorAttachmentDesc postAttachments[] = {
                {GL_RGBA16F, GL_NEAREST, GL_NEAREST},
                {GL_RGBA32F, GL_NEAREST, GL_NEAREST},
                {GL_RGBA16F, GL_NEAREST, GL_NEAREST}
            };
            if (!postTarget_.create(config().width, config().height, postAttachments, false)) return false;
            renderWidth_ = config().width;
            renderHeight_ = config().height;

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
            if (publicationRunning_) return;
            renderWidth_ = width;
            renderHeight_ = height;
            sceneTarget_.resize(width, height);
            postTarget_.resize(width, height);
        }

        void loadResearchModels() {
            dragonLoaded_ = dragon_.load(gfx::research::model_path(gfx::research::ModelAsset::Dragon));
            suzanneLoaded_ = suzanne_.load(gfx::research::model_path(gfx::research::ModelAsset::Suzanne));
            teapotLoaded_ = teapot_.load(gfx::research::model_path(gfx::research::ModelAsset::Teapot));
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
            const int renderWidth = publicationRunning_ ? renderWidth_ : frame.width;
            const int renderHeight = publicationRunning_ ? renderHeight_ : frame.height;

            sceneTarget_.bind();
            glViewport(0, 0, renderWidth, renderHeight);
            const float clearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
            const float clearNormal[] = {0.5f, 0.5f, 1.0f, 1.0f};
            const float clearWorld[] = {0.0f, 0.0f, 0.0f, 0.0f};
            glClearBufferfv(GL_COLOR, 0, clearColor);
            glClearBufferfv(GL_COLOR, 1, clearNormal);
            glClearBufferfv(GL_COLOR, 2, clearWorld);
            glClear(GL_DEPTH_BUFFER_BIT);

            const float aspect = static_cast<float>(renderWidth) / static_cast<float>(renderHeight);
            const glm::mat4 viewProjection = camera_.view_projection(aspect);
            drawScene(viewProjection);

            postTarget_.bind();
            glViewport(0, 0, renderWidth, renderHeight);
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
            glBindTextureUnit(3, sceneTarget_.color(2));
            skyShader_.set_int("uSceneColor", 0);
            skyShader_.set_int("uNormalRoughness", 1);
            skyShader_.set_int("uDepth", 2);
            skyShader_.set_int("uWorldPosition", 3);
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
            skyShader_.set_int("uMethod", method_);
            skyShader_.set_int("uObjectReflections", objectReflections_ ? 1 : 0);

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
                renderWidth,
                renderHeight,
                0,
                0,
                frame.width,
                frame.height,
                GL_COLOR_BUFFER_BIT,
                GL_NEAREST
            );
            glEnable(GL_DEPTH_TEST);

            recordManualTiming(frame);
            processPublicationFrame(frame, viewProjection, renderWidth, renderHeight);
        }

        void recordManualTiming(const gfx::research::FrameInfo& frame) {
            if (!record_ || !gpuTimer_.has_result()) return;
            if (!csv_.is_open()) {
                csv_.open("results/sky-timing.csv");
                csv_.row(
                    "frame", "method", "sky_mode", "gpu_ms", "width", "height",
                    "sky_radius", "cloud_coverage", "cloud_scale", "cloud_density",
                    "star_intensity", "aurora_intensity", "local_influence", "null_test", "object_reflections"
                );
            }
            csv_.row(
                frame.frame_index,
                methodId(method_),
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
                nullTest_ ? 1 : 0,
                objectReflections_ ? 1 : 0
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
            ImGui::Combo("Environment sampling", &method_, METHOD_LABELS.data(), static_cast<int>(METHOD_LABELS.size()));
            ImGui::Checkbox("Object sky reflections", &objectReflections_);
            ImGui::Checkbox("Spatially invariant null test", &nullTest_);
            ImGui::Checkbox("Record CSV", &record_);
            ImGui::SliderFloat("Local influence", &localInfluence_, 0.0f, 1.5f, "%.2f");
            ImGui::SliderFloat("Sky radius", &skyRadius_, 10.0f, 5000.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat3("Sky center", &skyCenter_.x, -100.0f, 100.0f);

            ImGui::SeparatorText("Procedural sky");
            ImGui::Combo("Sky mode", &skyMode_, SKY_MODE_LABELS.data(), static_cast<int>(SKY_MODE_LABELS.size()));
            ImGui::SliderAngle("Sun / moon elevation", &sunElevation_, 5.0f, 85.0f);
            if (skyMode_ == 0 || skyMode_ == 1) {
                ImGui::SliderFloat("Cloud coverage", &cloudCoverage_, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Cloud scale", &cloudScale_, 0.25f, 3.0f, "%.2f");
                ImGui::SliderFloat("Cloud density", &cloudDensity_, 0.0f, 1.35f, "%.2f");
            }
            if (skyMode_ == 1 || skyMode_ == 2) ImGui::SliderFloat("Star intensity", &starIntensity_, 0.0f, 3.0f, "%.2f");
            if (skyMode_ == 2) ImGui::SliderFloat("Aurora intensity", &auroraIntensity_, 0.0f, 3.0f, "%.2f");
            if (skyMode_ == 3) ImGui::TextDisabled("Synthetic deterministic world-space test field");
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
                ImGui::TextWrapped("Bundled research models from gfx-research-base could not be loaded.");
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
                + SKY_MODE_IDS[static_cast<std::size_t>(skyMode_)] + '-' + methodId(method_)
                + (objectReflections_ ? "-reflections-on.png" : "-reflections-off.png");
            gfx::research::save_framebuffer_png(filename, frame.width, frame.height);
        }

        void startPublicationSuite(const bool full) {
            stopPublicationSuite(false);
            publicationCases_ = makePublicationCases(full);
            publicationCaseIndex_ = 0;
            publicationCaseApplied_ = false;
            publicationRunning_ = !publicationCases_.empty();
            publicationOutput_ = launchOptions_.publicationOutput;
            std::filesystem::create_directories(publicationOutput_ / "images");
            std::filesystem::create_directories(publicationOutput_ / "hdr");

            publicationRaw_.open(publicationOutput_ / "raw.csv", std::ios::out | std::ios::trunc);
            publicationSummary_.open(publicationOutput_ / "summary.csv", std::ios::out | std::ios::trunc);
            publicationCaptures_.open(publicationOutput_ / "captures.csv", std::ios::out | std::ios::trunc);
            publicationQuality_.open(publicationOutput_ / "quality.csv", std::ios::out | std::ios::trunc);
            publicationNull_.open(publicationOutput_ / "null.csv", std::ios::out | std::ios::trunc);
            publicationManifest_.open(publicationOutput_ / "manifest.txt", std::ios::out | std::ios::trunc);

            publicationRaw_ << "case_id,pair_id,group_id,sweep,step,repeat,method,sky_mode,sample,gpu_ms,width,height,target_width,target_height,sky_radius,camera_offset_x,sphere_x,null_test,reflections_enabled\n";
            publicationSummary_ << "case_id,pair_id,group_id,sweep,step,repeat,method,sky_mode,samples,mean_ms,median_ms,stddev_ms,p95_ms,min_ms,max_ms,width,height,target_width,target_height,resolution_matched,sky_radius,camera_offset_x,sphere_x,null_test,reflections_enabled,world_position_rmse,background_hit_error,reflection_hit_error\n";
            publicationCaptures_ << "case_id,pair_id,group_id,sweep,step,repeat,method,sky_mode,width,height,reflections_enabled,png,pfm\n";
            publicationQuality_ << "pair_id,group_id,sweep,step,sky_mode,width,height,sky_radius,camera_offset_x,sphere_x,reflections_enabled,metric_region,baseline_rmse,proposed_rmse,baseline_relative_rmse,proposed_relative_rmse,baseline_psnr_db,proposed_psnr_db,baseline_max_abs,proposed_max_abs\n";
            publicationNull_ << "pair_id,sky_mode,width,height,reflections_enabled,linear_byte_identical,display_byte_identical,linear_rmse,linear_max_abs,display_rmse,display_max_abs\n";
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
                << "timing_repeats=" << (full ? 10 : 2) << '\n'
                << "offscreen_exact_resolution=1\n"
                << "explicit_position_reference=1\n"
                << "proposed_method=per-origin-virtual-sphere\n"
                << "legacy_fixed_domain_available=1\n"
                << "reflection_toggle_timing=1\n"
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
            if (publicationQuality_.is_open()) publicationQuality_.close();
            if (publicationNull_.is_open()) publicationNull_.close();
            if (publicationManifest_.is_open()) {
                publicationManifest_ << "completed=" << (completed ? 1 : 0) << '\n';
                publicationManifest_.close();
            }
            publicationRunning_ = false;
            publicationCaseApplied_ = false;
            method_ = METHOD_PER_ORIGIN_SPHERE;
            objectReflections_ = true;
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(window(), &framebufferWidth, &framebufferHeight);
            if (framebufferWidth > 0 && framebufferHeight > 0) {
                renderWidth_ = framebufferWidth;
                renderHeight_ = framebufferHeight;
                sceneTarget_.resize(renderWidth_, renderHeight_);
                postTarget_.resize(renderWidth_, renderHeight_);
            }
            publicationSamples_.clear();
            publicationBaselineImage_.clear();
            publicationLocalImage_.clear();
            publicationQualityPairId_.clear();
            publicationNullPairId_.clear();
            publicationNullBaselineLinear_.clear();
            publicationNullBaselineDisplay_.clear();
        }

        std::vector<PublicationCase> makePublicationCases(const bool full) const {
            std::vector<PublicationCase> cases;
            const std::array<std::pair<int, int>, 3> fullResolutions = {{{1920, 1080}, {2560, 1440}, {3840, 2160}}};
            const std::array<std::pair<int, int>, 1> quickResolutions = {{{1920, 1080}}};

            const int timingWarmup = full ? 120 : 30;
            const int timingSamples = full ? 600 : 120;
            const int timingRepeats = full ? 10 : 2;
            const int sweepWarmup = full ? 16 : 8;
            const int sweepSamples = full ? 64 : 24;
            const int radiusWarmup = full ? 48 : 16;
            const int radiusSamples = full ? 180 : 60;
            const int nullWarmup = full ? 64 : 24;
            const int nullSamples = full ? 240 : 80;

            const auto appendMethodPair = [&](PublicationCase base, const bool reverse = false) {
                if (base.groupId.empty()) base.groupId = base.pairId;
                const std::array<int, 2> order = reverse
                    ? std::array<int, 2>{METHOD_PER_ORIGIN_SPHERE, METHOD_DIRECTION_ONLY}
                    : std::array<int, 2>{METHOD_DIRECTION_ONLY, METHOD_PER_ORIGIN_SPHERE};
                for (const int method : order) {
                    PublicationCase test = base;
                    test.method = method;
                    cases.push_back(std::move(test));
                }
            };

            const auto appendQualityTriple = [&](PublicationCase base) {
                if (base.groupId.empty()) base.groupId = base.pairId;
                base.quality = true;
                for (const int method : {
                    METHOD_DIRECTION_ONLY,
                    METHOD_PER_ORIGIN_SPHERE,
                    METHOD_EXPLICIT_POSITION_REFERENCE
                }) {
                    PublicationCase test = base;
                    test.method = method;
                    cases.push_back(std::move(test));
                }
            };

            const auto appendTimingForResolution = [&](const int width, const int height, const bool reflectionsEnabled) {
                for (int mode = 0; mode < 3; ++mode) {
                    const std::string reflectionId = reflectionsEnabled ? "reflections-on" : "reflections-off";
                    const std::string group = std::string("timing-") + SKY_MODE_IDS[mode] + '-' + reflectionId + '-'
                        + std::to_string(width) + 'x' + std::to_string(height);
                    for (int repeat = 0; repeat < timingRepeats; ++repeat) {
                        PublicationCase test;
                        test.groupId = group;
                        test.pairId = group + "-run-" + std::to_string(repeat);
                        test.sweep = "timing";
                        test.repeat = repeat;
                        test.skyMode = mode;
                        test.reflectionsEnabled = reflectionsEnabled;
                        test.targetWidth = width;
                        test.targetHeight = height;
                        test.warmupFrames = timingWarmup;
                        test.sampleFrames = timingSamples;
                        test.capture = false;
                        appendMethodPair(test, (repeat & 1) != 0);
                    }
                }
            };

            const auto appendTimingSet = [&](const auto& resolutions) {
                for (const auto [width, height] : resolutions) {
                    appendTimingForResolution(width, height, false);
                    appendTimingForResolution(width, height, true);
                }
            };

            if (full) appendTimingSet(fullResolutions);
            else appendTimingSet(quickResolutions);

            // Background position test: reflections are disabled so the result measures only the sky path.
            const int translationSteps = full ? 21 : 11;
            for (int mode = 0; mode < 3; ++mode) {
                for (int step = 0; step < translationSteps; ++step) {
                    const float t = translationSteps == 1 ? 0.0f : static_cast<float>(step) / static_cast<float>(translationSteps - 1);
                    const float offset = -1.5f + t * 3.0f;
                    PublicationCase test;
                    test.pairId = std::string("camera-") + SKY_MODE_IDS[mode] + "-x-" + scalarToken(offset);
                    test.groupId = test.pairId;
                    test.sweep = "camera_translation";
                    test.step = step;
                    test.skyMode = mode;
                    test.reflectionsEnabled = false;
                    test.cameraOffsetX = offset;
                    test.warmupFrames = sweepWarmup;
                    test.sampleFrames = sweepSamples;
                    test.capture = mode == 0 && (step == 0 || step == translationSteps / 2 || step == translationSteps - 1);
                    appendMethodPair(test);
                }
            }

            // Reflection position test: object reflections are enabled by definition.
            for (int mode = 0; mode < 3; ++mode) {
                for (int step = 0; step < translationSteps; ++step) {
                    const float t = translationSteps == 1 ? 0.0f : static_cast<float>(step) / static_cast<float>(translationSteps - 1);
                    const float offset = -1.5f + t * 3.0f;
                    PublicationCase test;
                    test.pairId = std::string("object-") + SKY_MODE_IDS[mode] + "-x-" + scalarToken(offset);
                    test.groupId = test.pairId;
                    test.sweep = "object_translation";
                    test.step = step;
                    test.skyMode = mode;
                    test.reflectionsEnabled = true;
                    test.sphereX = offset;
                    test.warmupFrames = sweepWarmup;
                    test.sampleFrames = sweepSamples;
                    test.capture = false;
                    appendMethodPair(test);
                }
            }

            const std::vector<float> radii = full
                ? std::vector<float>{15.0f, 30.0f, 75.0f, 150.0f, 300.0f}
                : std::vector<float>{15.0f, 30.0f, 150.0f};
            for (int mode = 0; mode < 3; ++mode) {
                for (std::size_t step = 0; step < radii.size(); ++step) {
                    PublicationCase test;
                    test.pairId = std::string("radius-") + SKY_MODE_IDS[mode] + '-' + scalarToken(radii[step]);
                    test.groupId = test.pairId;
                    test.sweep = "radius";
                    test.step = static_cast<int>(step);
                    test.skyMode = mode;
                    test.reflectionsEnabled = true;
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
                    test.groupId = test.pairId;
                    test.sweep = "null";
                    test.skyMode = mode;
                    test.reflectionsEnabled = true;
                    test.nullTest = true;
                    test.targetWidth = width;
                    test.targetHeight = height;
                    test.warmupFrames = nullWarmup;
                    test.sampleFrames = nullSamples;
                    test.capture = false;
                    appendMethodPair(test);
                }
            };

            if (full) {
                for (const auto [width, height] : fullResolutions) appendNullAtResolution(width, height);
            } else {
                for (const auto [width, height] : quickResolutions) appendNullAtResolution(width, height);
            }

            // Full-frame image-space ablation keeps reflections enabled so the explicit-position variant
            // still exercises geometry reconstruction. Background pixels are identical between the
            // proposed and explicit-position variants by construction; geometry isolates reconstruction error.
            const int qualitySteps = full ? 7 : 3;
            for (int mode = 0; mode < 3; ++mode) {
                for (int step = 0; step < qualitySteps; ++step) {
                    const float t = qualitySteps == 1 ? 0.0f : static_cast<float>(step) / static_cast<float>(qualitySteps - 1);
                    const float offset = -6.0f + t * 12.0f;
                    PublicationCase test;
                    test.pairId = std::string("quality-") + SKY_MODE_IDS[mode] + "-x-" + scalarToken(offset);
                    test.groupId = test.pairId;
                    test.sweep = "image_quality";
                    test.step = step;
                    test.skyMode = mode;
                    test.reflectionsEnabled = true;
                    test.cameraOffsetX = offset;
                    test.skyRadius = 30.0f;
                    test.targetWidth = 1920;
                    test.targetHeight = 1080;
                    test.warmupFrames = sweepWarmup;
                    test.sampleFrames = sweepSamples;
                    test.capture = mode == 0
                        ? (step == 0 || step == qualitySteps / 2 || step == qualitySteps - 1)
                        : (step == qualitySteps / 2);
                    appendQualityTriple(test);
                }
            }

            const int reflectionQualitySteps = full ? 5 : 3;
            for (int mode = 0; mode < 3; ++mode) {
                for (int step = 0; step < reflectionQualitySteps; ++step) {
                    const float t = reflectionQualitySteps == 1 ? 0.0f : static_cast<float>(step) / static_cast<float>(reflectionQualitySteps - 1);
                    const float offset = -1.5f + t * 3.0f;
                    PublicationCase test;
                    test.pairId = std::string("reflection-quality-") + SKY_MODE_IDS[mode] + "-x-" + scalarToken(offset);
                    test.groupId = test.pairId;
                    test.sweep = "reflection_image_quality";
                    test.step = step;
                    test.skyMode = mode;
                    test.reflectionsEnabled = true;
                    test.sphereX = offset;
                    test.skyRadius = 30.0f;
                    test.targetWidth = 1920;
                    test.targetHeight = 1080;
                    test.warmupFrames = sweepWarmup;
                    test.sampleFrames = sweepSamples;
                    test.capture = mode == 0
                        ? (step == 0 || step == reflectionQualitySteps / 2 || step == reflectionQualitySteps - 1)
                        : (step == reflectionQualitySteps / 2);
                    appendQualityTriple(test);
                }
            }

            // Controlled world-space signal. This synthetic field is intentionally simple and strongly
            // position-dependent so camera/object translation cannot be hidden by a visually smooth sky.
            const int syntheticSteps = full ? 13 : 5;
            for (int step = 0; step < syntheticSteps; ++step) {
                const float t = syntheticSteps == 1 ? 0.0f : static_cast<float>(step) / static_cast<float>(syntheticSteps - 1);
                const float offset = -3.0f + t * 6.0f;
                PublicationCase background;
                background.pairId = std::string("synthetic-camera-x-") + scalarToken(offset);
                background.groupId = background.pairId;
                background.sweep = "synthetic_camera_translation";
                background.step = step;
                background.skyMode = 3;
                background.reflectionsEnabled = false;
                background.cameraOffsetX = offset;
                background.skyRadius = 30.0f;
                background.warmupFrames = sweepWarmup;
                background.sampleFrames = sweepSamples;
                background.capture = step == 0 || step == syntheticSteps / 2 || step == syntheticSteps - 1;
                appendMethodPair(background);

                PublicationCase reflection = background;
                reflection.pairId = std::string("synthetic-object-x-") + scalarToken(offset);
                reflection.groupId = reflection.pairId;
                reflection.sweep = "synthetic_object_translation";
                reflection.reflectionsEnabled = true;
                reflection.cameraOffsetX = 0.0f;
                reflection.sphereX = offset * 0.5f;
                reflection.capture = false;
                appendMethodPair(reflection);
            }

            return cases;
        }

        void preparePublicationFrame(const gfx::research::FrameInfo&) {
            if (!publicationRunning_ || publicationCaseIndex_ >= publicationCases_.size()) return;
            if (publicationCaseApplied_) return;

            const PublicationCase& test = publicationCases_[publicationCaseIndex_];
            skyMode_ = test.skyMode;
            method_ = test.method;
            objectReflections_ = test.reflectionsEnabled;
            nullTest_ = test.nullTest;
            skyRadius_ = test.skyRadius;
            reflectiveSphereX_ = test.sphereX;
            camera_.set_target(cameraBaseTarget_ + glm::vec3(test.cameraOffsetX, 0.0f, 0.0f));
            animateSky_ = false;

            publicationSamples_.clear();
            publicationValidation_ = {};
            publicationWarmupRemaining_ = test.warmupFrames;
            publicationSampleIndex_ = 0;
            renderWidth_ = test.targetWidth;
            renderHeight_ = test.targetHeight;
            sceneTarget_.resize(renderWidth_, renderHeight_);
            postTarget_.resize(renderWidth_, renderHeight_);
            publicationResolutionMatched_ = true;
            publicationCaseApplied_ = true;
        }


        bool publicationResolutionReady(const gfx::research::FrameInfo&, const PublicationCase&) {
            return publicationResolutionMatched_;
        }

        void processPublicationFrame(
            const gfx::research::FrameInfo& frame,
            const glm::mat4& viewProjection,
            const int renderWidth,
            const int renderHeight
        ) {
            if (!publicationRunning_ || !publicationCaseApplied_ || publicationCaseIndex_ >= publicationCases_.size()) return;
            const PublicationCase& test = publicationCases_[publicationCaseIndex_];
            if (!publicationResolutionReady(frame, test)) return;

            if (publicationWarmupRemaining_ > 0) {
                --publicationWarmupRemaining_;
                return;
            }
            if (!gpuTimer_.has_result()) return;

            if (publicationSampleIndex_ == 0) publicationValidation_ = validateCurrentFrame(renderWidth, renderHeight, viewProjection);

            const double gpuMs = gpuTimer_.milliseconds();
            publicationSamples_.push_back(gpuMs);
            publicationRaw_
                << caseId(test) << ',' << test.pairId << ',' << test.groupId << ',' << test.sweep << ',' << test.step << ',' << test.repeat << ','
                << methodId(test) << ',' << SKY_MODE_IDS[static_cast<std::size_t>(test.skyMode)] << ','
                << publicationSampleIndex_ << ',' << std::setprecision(12) << gpuMs << ','
                << renderWidth << ',' << renderHeight << ',' << test.targetWidth << ',' << test.targetHeight << ','
                << test.skyRadius << ',' << test.cameraOffsetX << ',' << test.sphereX << ',' << (test.nullTest ? 1 : 0) << ','
                << (test.reflectionsEnabled ? 1 : 0) << '\n';
            ++publicationSampleIndex_;

            if (publicationSampleIndex_ < test.sampleFrames) return;

            if (test.capture) capturePublicationCase(test, renderWidth, renderHeight);
            finishPublicationCase(test, renderWidth, renderHeight);
            ++publicationCaseIndex_;
            publicationCaseApplied_ = false;

            if (publicationCaseIndex_ >= publicationCases_.size()) {
                std::cout << "Publication suite complete. Analyze with: python tools/analyze_publication.py "
                          << publicationOutput_.string() << '\n';
                stopPublicationSuite(true);
                if (launchOptions_.publicationSuite || launchOptions_.publicationQuick) close();
            }
        }

        void finishPublicationCase(const PublicationCase& test, const int renderWidth, const int renderHeight) {
            const TimingStats stats = calculateStats(publicationSamples_);
            publicationSummary_
                << caseId(test) << ',' << test.pairId << ',' << test.groupId << ',' << test.sweep << ',' << test.step << ',' << test.repeat << ','
                << methodId(test) << ',' << SKY_MODE_IDS[static_cast<std::size_t>(test.skyMode)] << ','
                << publicationSamples_.size() << ','
                << std::setprecision(12) << stats.mean << ',' << stats.median << ',' << stats.stddev << ','
                << stats.p95 << ',' << stats.minimum << ',' << stats.maximum << ','
                << renderWidth << ',' << renderHeight << ',' << test.targetWidth << ',' << test.targetHeight << ','
                << (publicationResolutionMatched_ ? 1 : 0) << ',' << test.skyRadius << ','
                << test.cameraOffsetX << ',' << test.sphereX << ',' << (test.nullTest ? 1 : 0) << ','
                << (test.reflectionsEnabled ? 1 : 0) << ','
                << publicationValidation_.worldPositionRmse << ',' << publicationValidation_.backgroundHitError << ','
                << publicationValidation_.reflectionHitError << '\n';

            if (test.quality) processQualityImage(test, renderWidth, renderHeight);
            if (test.nullTest) processNullImage(test, renderWidth, renderHeight);
            publicationRaw_.flush();
            publicationSummary_.flush();
        }

        void capturePublicationCase(const PublicationCase& test, const int renderWidth, const int renderHeight) {
            const std::string id = caseId(test);
            const std::filesystem::path pfmRelative = std::filesystem::path("hdr") / (id + ".pfm");
            const std::filesystem::path pfm = publicationOutput_ / pfmRelative;
            const bool pfmSaved = saveTexturePfm(pfm, postTarget_.color(2), renderWidth, renderHeight);
            publicationCaptures_
                << id << ',' << test.pairId << ',' << test.groupId << ',' << test.sweep << ',' << test.step << ',' << test.repeat << ','
                << methodId(test) << ',' << SKY_MODE_IDS[static_cast<std::size_t>(test.skyMode)] << ','
                << renderWidth << ',' << renderHeight << ',' << (test.reflectionsEnabled ? 1 : 0) << ',' << "" << ','
                << (pfmSaved ? pfmRelative.generic_string() : "") << '\n';
            publicationCaptures_.flush();
        }

        void processQualityImage(const PublicationCase& test, const int width, const int height) {
            const std::vector<float> image = readTextureRgba(postTarget_.color(2), width, height);
            if (test.method == METHOD_DIRECTION_ONLY) {
                publicationQualityPairId_ = test.pairId;
                publicationBaselineImage_ = image;
                publicationLocalImage_.clear();
                return;
            }
            if (test.method == METHOD_PER_ORIGIN_SPHERE) {
                if (publicationQualityPairId_ == test.pairId) publicationLocalImage_ = image;
                return;
            }
            if (test.method != METHOD_EXPLICIT_POSITION_REFERENCE || publicationQualityPairId_ != test.pairId) return;
            if (publicationBaselineImage_.empty() || publicationLocalImage_.empty()) return;

            ImageMetrics baseline;
            ImageMetrics local;
            const bool reflectionRegion = test.sweep == "reflection_image_quality";
            if (reflectionRegion) {
                const float aspect = static_cast<float>(width) / static_cast<float>(height);
                const glm::mat4 viewProjection = camera_.view_projection(aspect);
                const glm::vec4 clip = viewProjection * glm::vec4(test.sphereX, 0.95f, 0.15f, 1.0f);
                const glm::vec3 ndc = clip.w > 0.0f ? glm::vec3(clip) / clip.w : glm::vec3(0.0f);
                const int centerX = std::clamp(static_cast<int>((ndc.x * 0.5f + 0.5f) * width), 0, width - 1);
                const int centerY = std::clamp(static_cast<int>((ndc.y * 0.5f + 0.5f) * height), 0, height - 1);
                const int halfExtent = std::max(32, height / 7);
                baseline = calculateImageMetricsRegion(
                    publicationBaselineImage_, image, width, height,
                    centerX - halfExtent, centerY - halfExtent, centerX + halfExtent, centerY + halfExtent
                );
                local = calculateImageMetricsRegion(
                    publicationLocalImage_, image, width, height,
                    centerX - halfExtent, centerY - halfExtent, centerX + halfExtent, centerY + halfExtent
                );
            } else {
                baseline = calculateImageMetrics(publicationBaselineImage_, image, width, height);
                local = calculateImageMetrics(publicationLocalImage_, image, width, height);
            }
            publicationQuality_
                << test.pairId << ',' << test.groupId << ',' << test.sweep << ',' << test.step << ','
                << SKY_MODE_IDS[static_cast<std::size_t>(test.skyMode)] << ',' << width << ',' << height << ','
                << test.skyRadius << ',' << test.cameraOffsetX << ',' << test.sphereX << ','
                << (test.reflectionsEnabled ? 1 : 0) << ','
                << (reflectionRegion ? "reflection-roi" : "full-frame") << ','
                << std::setprecision(12) << baseline.rmse << ',' << local.rmse << ','
                << baseline.relativeRmse << ',' << local.relativeRmse << ','
                << baseline.psnr << ',' << local.psnr << ','
                << baseline.maximumAbsolute << ',' << local.maximumAbsolute << '\n';
            publicationQuality_.flush();
            publicationBaselineImage_.clear();
            publicationLocalImage_.clear();
            publicationQualityPairId_.clear();
        }

        void processNullImage(const PublicationCase& test, const int width, const int height) {
            const std::vector<float> linear = readTextureRgba(postTarget_.color(2), width, height);
            const std::vector<float> display = readTextureRgba(postTarget_.color(0), width, height);
            if (test.method == METHOD_DIRECTION_ONLY) {
                publicationNullPairId_ = test.pairId;
                publicationNullBaselineLinear_ = linear;
                publicationNullBaselineDisplay_ = display;
                return;
            }
            if (test.method != METHOD_PER_ORIGIN_SPHERE || publicationNullPairId_ != test.pairId) return;
            const ImageMetrics linearMetrics = calculateImageMetrics(publicationNullBaselineLinear_, linear, width, height);
            const ImageMetrics displayMetrics = calculateImageMetrics(publicationNullBaselineDisplay_, display, width, height);
            const bool linearIdentical = publicationNullBaselineLinear_.size() == linear.size()
                && std::memcmp(publicationNullBaselineLinear_.data(), linear.data(), linear.size() * sizeof(float)) == 0;
            const bool displayIdentical = publicationNullBaselineDisplay_.size() == display.size()
                && std::memcmp(publicationNullBaselineDisplay_.data(), display.data(), display.size() * sizeof(float)) == 0;
            publicationNull_
                << test.pairId << ',' << SKY_MODE_IDS[static_cast<std::size_t>(test.skyMode)] << ','
                << width << ',' << height << ',' << (test.reflectionsEnabled ? 1 : 0) << ','
                << (linearIdentical ? 1 : 0) << ',' << (displayIdentical ? 1 : 0) << ','
                << std::setprecision(12) << linearMetrics.rmse << ',' << linearMetrics.maximumAbsolute << ','
                << displayMetrics.rmse << ',' << displayMetrics.maximumAbsolute << '\n';
            publicationNull_.flush();
            publicationNullPairId_.clear();
            publicationNullBaselineLinear_.clear();
            publicationNullBaselineDisplay_.clear();
        }

        ValidationResult validateCurrentFrame(const int width, const int height, const glm::mat4& viewProjection) const {
            ValidationResult result;
            result.worldPositionRmse = validateWorldPositionReconstruction(width, height, viewProjection);
            result.backgroundHitError = validateBackgroundHit(width, height, viewProjection);
            result.reflectionHitError = validateReflectionHit(width, height, viewProjection);
            return result;
        }

        double validateWorldPositionReconstruction(const int width, const int height, const glm::mat4& viewProjection) const {
            const glm::dmat4 inverseViewProjection = glm::inverse(glm::dmat4(viewProjection));
            long double squaredError = 0.0;
            std::size_t count = 0;
            constexpr int GRID_X = 12;
            constexpr int GRID_Y = 7;
            for (int gy = 0; gy < GRID_Y; ++gy) {
                for (int gx = 0; gx < GRID_X; ++gx) {
                    const int x = std::clamp((gx * 2 + 1) * width / (GRID_X * 2), 0, width - 1);
                    const int y = std::clamp((gy * 2 + 1) * height / (GRID_Y * 2), 0, height - 1);
                    const float depth = readDepth(x, y);
                    if (depth >= 0.99999f) continue;
                    const glm::vec4 explicitWorld = readRgba(sceneTarget_.color(2), x, y);
                    if (explicitWorld.w < 0.5f) continue;
                    const glm::dvec3 reconstructed = reconstructWorldReference(
                        x, y, depth, width, height, inverseViewProjection
                    );
                    const double error = glm::length(reconstructed - glm::dvec3(explicitWorld));
                    squaredError += error * error;
                    ++count;
                }
            }
            if (count == 0) return std::numeric_limits<double>::quiet_NaN();
            return std::sqrt(static_cast<double>(squaredError / count));
        }

        double validateBackgroundHit(const int width, const int height, const glm::mat4& viewProjection) const {
            constexpr std::array<glm::vec2, 6> candidates = {
                glm::vec2(0.82f, 0.82f), glm::vec2(0.18f, 0.82f), glm::vec2(0.86f, 0.62f),
                glm::vec2(0.14f, 0.62f), glm::vec2(0.72f, 0.92f), glm::vec2(0.28f, 0.92f)
            };

            int x = 0;
            int y = 0;
            bool found = false;
            for (const glm::vec2& uv : candidates) {
                x = std::clamp(static_cast<int>(uv.x * width), 0, width - 1);
                y = std::clamp(static_cast<int>(uv.y * height), 0, height - 1);
                if (readDepth(x, y) >= 0.99999f) {
                    found = true;
                    break;
                }
            }
            if (!found) return std::numeric_limits<double>::quiet_NaN();

            const glm::vec4 gpuHit = readRgba(postTarget_.color(1), x, y);
            if (gpuHit.w < 0.5f) return std::numeric_limits<double>::quiet_NaN();

            const glm::dmat4 inverseViewProjection = glm::inverse(glm::dmat4(viewProjection));
            const glm::dvec3 farPoint = reconstructWorldReference(x, y, 1.0, width, height, inverseViewProjection);
            const glm::dvec3 origin = glm::dvec3(camera_.position());
            const glm::dvec3 direction = glm::normalize(farPoint - origin);
            const glm::dvec3 expected = perOriginSphereReference(origin, direction, skyRadius_);
            return glm::length(glm::dvec3(gpuHit) - expected);
        }

        double validateReflectionHit(const int width, const int height, const glm::mat4& viewProjection) const {
            if (!showReflectiveSphere_ || !objectReflections_) return std::numeric_limits<double>::quiet_NaN();
            const glm::vec4 clip = viewProjection * glm::vec4(reflectiveSphereX_, 0.95f, 0.15f, 1.0f);
            if (clip.w <= 0.0f) return std::numeric_limits<double>::quiet_NaN();
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f) return std::numeric_limits<double>::quiet_NaN();

            const int x = std::clamp(static_cast<int>((ndc.x * 0.5f + 0.5f) * width), 0, width - 1);
            const int y = std::clamp(static_cast<int>((ndc.y * 0.5f + 0.5f) * height), 0, height - 1);
            const float depth = readDepth(x, y);
            if (depth >= 0.99999f) return std::numeric_limits<double>::quiet_NaN();

            const glm::vec4 normalRoughness = readRgba(sceneTarget_.color(1), x, y);
            const glm::dvec3 normal = glm::normalize(glm::dvec3(normalRoughness) * 2.0 - glm::dvec3(1.0));
            const glm::vec4 explicitWorld = readRgba(sceneTarget_.color(2), x, y);
            if (explicitWorld.w < 0.5f) return std::numeric_limits<double>::quiet_NaN();
            const glm::dvec3 world = glm::dvec3(explicitWorld);
            const glm::dvec3 eye = glm::dvec3(camera_.position());
            const glm::dvec3 incident = glm::normalize(world - eye);
            const glm::dvec3 reflected = glm::reflect(incident, normal);
            const glm::dvec3 origin = world + normal * 0.002;

            const glm::dvec3 expected = perOriginSphereReference(origin, reflected, skyRadius_);

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
            const int width,
            const int height,
            const glm::dmat4& inverseViewProjection
        ) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(width);
            const double v = (static_cast<double>(y) + 0.5) / static_cast<double>(height);
            const glm::dvec4 clip(u * 2.0 - 1.0, v * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
            const glm::dvec4 world = inverseViewProjection * clip;
            return glm::dvec3(world) / world.w;
        }

        static const char* methodId(const int method) {
            switch (method) {
                case METHOD_FIXED_DOMAIN: return "fixed-domain-local-ray";
                case METHOD_PER_ORIGIN_SPHERE: return "per-origin-sphere";
                case METHOD_EXPLICIT_POSITION_REFERENCE: return "explicit-position-reference";
                default: return "direction-only";
            }
        }

        static const char* methodId(const PublicationCase& test) {
            return methodId(test.method);
        }

        static std::string caseId(const PublicationCase& test) {
            return test.pairId + '-' + methodId(test);
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
        int method_ = METHOD_PER_ORIGIN_SPHERE;
        bool objectReflections_ = true;
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
        std::ofstream publicationQuality_;
        std::ofstream publicationNull_;
        std::ofstream publicationManifest_;
        std::vector<float> publicationBaselineImage_;
        std::vector<float> publicationLocalImage_;
        std::string publicationQualityPairId_;
        std::vector<float> publicationNullBaselineLinear_;
        std::vector<float> publicationNullBaselineDisplay_;
        std::string publicationNullPairId_;
        std::size_t publicationCaseIndex_ = 0;
        int publicationWarmupRemaining_ = 0;
        int publicationSampleIndex_ = 0;
        int renderWidth_ = 0;
        int renderHeight_ = 0;
        bool publicationRunning_ = false;
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

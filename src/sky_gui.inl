// Class-local UI and parameter plumbing for SkyResearch.
        bool loadSkyShader() {
            const gfx::research::ShaderSource sources[] = {
                {gfx::research::ShaderStage::Vertex, shaderSource(root_/"shaders/fullscreen.vert")},
                {gfx::research::ShaderStage::Fragment, shaderSource(root_/"shaders/sky.frag")}
            };
            return skyShader_.load(sources);
        }

        void bindVolumeParameters() {
            cloudNoise_.bind();
            skyShader_.set_int("uCloudShape",4);
            skyShader_.set_int("uCloudDetail",5);
            glUniform4i(glGetUniformLocation(skyShader_.id(),"uVolumeBudget"),
                volume_.cloudSamples,volume_.lightSamples,volume_.emptyStep,volume_.auroraSamples);
            skyShader_.set_vec4("uVolumeField",{volume_.scale,volume_.coverage,volume_.density,volume_.depth});
            skyShader_.set_vec4("uVolumeShape",{volume_.detail,volume_.softness,volume_.selfShadow,volume_.type});
            skyShader_.set_vec4("uVolumeOptics",{volume_.anisotropy,volume_.multipleScattering,volume_.powder,volume_.detailScale});
            skyShader_.set_vec4("uVolumeMotion",{12.0f,4.0f,0.8f,volume_.weather});
            skyShader_.set_vec2("uVolumeSampling",{volume_.lightCone,volume_.jitter});
            skyShader_.set_vec4("uAuroraVolume",{volume_.auroraBase,volume_.auroraHeight,volume_.auroraWidth,volume_.auroraSpeed});
            skyShader_.set_float("uVolumeAuroraIntensity",volume_.auroraIntensity);
            const auto sun=glm::normalize(glm::vec3(std::cos(sunElevation_),std::sin(sunElevation_),0.24f));
            const auto atmosphere=atmosphereConstants(sun.y,volume_.turbidity,volume_.atmosphereExposure);
            skyShader_.set_vec4("uAtmosphere",atmosphere.zenith);
            skyShader_.set_vec4("uAtmosphereNorm",atmosphere.normalization);
        }

        void resetBenchmarkParameters() {
            volume_={}; volumeBudgetUi_=1; fixedFieldScale_=false;
            sunElevation_=0.58f; cloudCoverage_=0.52f; cloudScale_=1.0f; cloudDensity_=1.0f;
            starIntensity_=1.0f; auroraIntensity_=1.15f; localInfluence_=1.0f; frozenTime_=12.0f;
            skyCenter_={0,0,0}; sphereRoughness_=0.05f; floorRoughness_=0.38f;
            showReflectiveSphere_=showDragon_=showSuzanne_=showTeapot_=true;
            camera_.set_distance(7.2f); camera_.set_yaw(0.54f); camera_.set_pitch(0.29f); camera_.set_fov(54.0f);
        }

        static void configureGui() {
            auto& style=ImGui::GetStyle();
            style.WindowPadding={9,8}; style.FramePadding={5,3}; style.ItemSpacing={7,4};
            style.WindowRounding=2; style.FrameRounding=1; style.GrabRounding=1;
            style.WindowBorderSize=1; style.FrameBorderSize=0; style.ScrollbarSize=11;
            style.Colors[ImGuiCol_WindowBg]={0.035f,0.043f,0.055f,0.97f};
            style.Colors[ImGuiCol_TitleBg]={0.09f,0.16f,0.25f,1};
            style.Colors[ImGuiCol_TitleBgActive]={0.16f,0.28f,0.43f,1};
            style.Colors[ImGuiCol_FrameBg]={0.10f,0.16f,0.24f,1};
            style.Colors[ImGuiCol_FrameBgHovered]={0.16f,0.26f,0.38f,1};
            style.Colors[ImGuiCol_FrameBgActive]={0.20f,0.33f,0.49f,1};
            style.Colors[ImGuiCol_SliderGrab]={0.24f,0.53f,0.85f,1};
            style.Colors[ImGuiCol_CheckMark]={0.40f,0.72f,1,1};
            style.Colors[ImGuiCol_Header]={0.12f,0.23f,0.36f,1};
            style.Colors[ImGuiCol_HeaderHovered]={0.20f,0.36f,0.54f,1};
        }

        static bool parameterTable(const char* id) {
            if (!ImGui::BeginTable(id,2,ImGuiTableFlags_SizingStretchProp)) return false;
            ImGui::TableSetupColumn("Parameter",ImGuiTableColumnFlags_WidthStretch,0.42f);
            ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch,0.58f);
            return true;
        }

        template<class Control> static void parameter(const char* label,Control&& control) {
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1); ImGui::PushID(label);
            ImGui::SetNextItemWidth(-1.0f); control(); ImGui::PopID();
        }

        void handleShortcuts() {
            const auto& io=ImGui::GetIO();
            if (io.WantTextInput) return;
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H,false)) guiVisible_=!guiVisible_;
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F,false)) {
                if (!fullscreenActive_) {
                    glfwGetWindowPos(window(),&windowedX_,&windowedY_);
                    glfwGetWindowSize(window(),&windowedWidth_,&windowedHeight_);
                    auto* monitor=glfwGetPrimaryMonitor();
                    const auto* mode=monitor ? glfwGetVideoMode(monitor) : nullptr;
                    if (mode) {
                        glfwSetWindowMonitor(window(),monitor,0,0,mode->width,mode->height,mode->refreshRate);
                        fullscreenActive_=true;
                    }
                } else {
                    glfwSetWindowMonitor(window(),nullptr,windowedX_,windowedY_,windowedWidth_,windowedHeight_,0);
                    fullscreenActive_=false;
                }
            }
        }

        void on_gui(const gfx::research::FrameInfo& frame) override {
            handleShortcuts();
            if (!guiVisible_) return;
            const auto* viewport=ImGui::GetMainViewport();
            ImGui::SetNextWindowPos({viewport->WorkPos.x+12,viewport->WorkPos.y+12},ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({448,std::min(800.0f,viewport->WorkSize.y-24)},ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints({380,320},{650,std::max(320.0f,viewport->WorkSize.y-24)});
            if (!ImGui::Begin("Sky Research")) {ImGui::End();return;}
            if (parameterTable("status")) {
                parameter("Frame rate",[&]{ImGui::Text("%.0f FPS (%.2f ms)",fps(),cpu_frame_milliseconds());});
                parameter("GPU sky pass",[&]{ImGui::Text("%.3f ms",gpuTimer_.milliseconds());});
                parameter("Render target",[&]{ImGui::Text("%d x %d",renderWidth_,renderHeight_);});
                ImGui::EndTable();
            }
            if (publicationRunning_) {
                const auto& current=publicationCases_[publicationCaseIndex_];
                ImGui::SeparatorText("Benchmark running");
                ImGui::Text("Case %zu / %zu",publicationCaseIndex_+1,publicationCases_.size());
                ImGui::TextWrapped("%s",current.pairId.c_str());
                ImGui::ProgressBar(float(publicationCaseIndex_)/float(publicationCases_.size()),{-1,0});
                if (ImGui::Button("Stop benchmark",{-1,0})) {
                    stopPublicationSuite(false);
                    publicationMessage_ = "Benchmark stopped.";
                    if (launchOptions_.runsPublication()) close();
                }
            }
            ImGui::BeginDisabled(publicationRunning_);
            ImGui::SeparatorText("Environment");
            if (parameterTable("environment")) {
                parameter("Sky variant",[&]{ImGui::Combo("##sky",&skyMode_,SKY_MODE_LABELS.data(),int(SKY_MODE_LABELS.size()));});
                parameter("Ray sampling",[&]{
                    int selected=method_;
                    if(selected>=3) selected=selected==3 ? 2 : (selected==4 ? 1 : 0);
                    const char* labels[]{"Direction only","Fixed world sphere","Origin-aware distance"};
                    if(ImGui::Combo("##method",&selected,labels,3)) method_=selected;
                });
                parameter("Sky reflections",[&]{ImGui::Checkbox("##reflections",&objectReflections_);});
                ImGui::EndTable();
            }
            if (ImGui::BeginTabBar("Sections")) {
                if (ImGui::BeginTabItem("Sky")) {
                    if (parameterTable("sky-main")) {
                        parameter("Sampling R",[&]{ImGui::SliderFloat("##R",&skyRadius_,10,20000,"%.0f",ImGuiSliderFlags_Logarithmic);});
                        parameter("Fixed field scale",[&]{ImGui::Checkbox("##lockfield",&fixedFieldScale_);});
                        if(fixedFieldScale_) parameter("Field scale distance",[&]{ImGui::SliderFloat("##fieldR",&fieldScaleRadius_,10,20000,"%.0f",ImGuiSliderFlags_Logarithmic);});
                        parameter("Field anchor",[&]{ImGui::DragFloat3("##center",&skyCenter_.x,0.1f);});
                        parameter("Sun / moon height",[&]{ImGui::SliderAngle("##sun",&sunElevation_,5,85);});
                        parameter("Animate",[&]{ImGui::Checkbox("##animate",&animateSky_);});
                        if(!animateSky_) parameter("Frozen time",[&]{ImGui::SliderFloat("##time",&frozenTime_,0,120,"%.1f s");});
                        ImGui::EndTable();
                    }
                    if(skyMode_>=4 && skyMode_<=6) drawVolumeGui();
                    else if(parameterTable("cheap-sky")) {
                        if(skyMode_==0 || skyMode_==1) {
                            parameter("Cloud coverage",[&]{ImGui::SliderFloat("##coverage",&cloudCoverage_,0,1,"%.3f");});
                            parameter("Cloud scale",[&]{ImGui::SliderFloat("##scale",&cloudScale_,0.25f,3,"%.3f");});
                            parameter("Cloud density",[&]{ImGui::SliderFloat("##density",&cloudDensity_,0,1.35f,"%.3f");});
                        }
                        if(skyMode_==2) parameter("Aurora intensity",[&]{ImGui::SliderFloat("##aurora",&auroraIntensity_,0,3,"%.3f");});
                        ImGui::EndTable();
                    }
                    if(skyMode_==1 || skyMode_==2 || (skyMode_>=5 && skyMode_<=6)) {
                        if(parameterTable("stars")) {
                            parameter("Star intensity",[&]{ImGui::SliderFloat("##stars",&starIntensity_,0,3,"%.3f");});
                            ImGui::EndTable();
                        }
                    }
                    if(skyMode_==7) ImGui::TextWrapped("Uniform extinction with sinusoidal emission. The analytic reference is evaluated by tools/validate_slab.py.");
                    if(skyMode_==3) ImGui::TextWrapped("Deterministic world-space bands and checker cells.");
                    ImGui::EndTabItem();
                }
                if(ImGui::BeginTabItem("Scene")) {
                    ImGui::SeparatorText("Reflective objects");
                    if(parameterTable("scene")) {
                        parameter("Sphere",[&]{ImGui::Checkbox("##sphere",&showReflectiveSphere_);});
                        parameter("Sphere X",[&]{ImGui::SliderFloat("##x",&reflectiveSphereX_,-2,2,"%.3f");});
                        parameter("Sphere roughness",[&]{ImGui::SliderFloat("##roughness",&sphereRoughness_,0,1,"%.3f");});
                        parameter("Floor roughness",[&]{ImGui::SliderFloat("##roughness",&floorRoughness_,0,1,"%.3f");});
                        parameter("Stanford Dragon",[&]{ImGui::Checkbox("##show",&showDragon_);ImGui::SameLine();assetStatus(dragonLoaded_);});
                        parameter("Suzanne",[&]{ImGui::Checkbox("##show",&showSuzanne_);ImGui::SameLine();assetStatus(suzanneLoaded_);});
                        parameter("Utah Teapot",[&]{ImGui::Checkbox("##show",&showTeapot_);ImGui::SameLine();assetStatus(teapotLoaded_);});
                        ImGui::EndTable();
                    }
                    if(ImGui::Button("Reload models",{-1,0})) loadResearchModels();
                    ImGui::EndTabItem();
                }
                if(ImGui::BeginTabItem("Camera")) {
                    const auto position=camera_.position();
                    ImGui::Text("Position: (%.2f, %.2f, %.2f)",position.x,position.y,position.z);
                    if(parameterTable("camera")) {
                        parameter("Orbit distance",[&]{ImGui::SliderFloat("##distance",&camera_.distance(),2,20,"%.3f");});
                        parameter("Yaw",[&]{ImGui::SliderAngle("##yaw",&camera_.yaw(),-180,180);});
                        parameter("Pitch",[&]{ImGui::SliderAngle("##pitch",&camera_.pitch(),-80,80);});
                        parameter("Target",[&]{ImGui::DragFloat3("##target",&camera_.target().x,0.05f);});
                        parameter("Field of view",[&]{ImGui::SliderFloat("##fov",&camera_.fov(),20,110,"%.1f deg");});
                        ImGui::EndTable();
                    }
                    ImGui::EndTabItem();
                }
                if(ImGui::BeginTabItem("Tests")) {
                    ImGui::SeparatorText("Validation");
                    if(parameterTable("tests")) {
                        parameter("Invariant null test",[&]{ImGui::Checkbox("##null",&nullTest_);});
                        parameter("Local influence",[&]{ImGui::SliderFloat("##influence",&localInfluence_,0,1,"%.3f");});
                        parameter("Record timing CSV",[&]{ImGui::Checkbox("##record",&record_);});
                        parameter("Suite family",[&]{
                            const char* choices[]{"All variants", "Cheap", "Volumetric", "Synthetic"};
                            constexpr std::array<std::string_view, 4> families{"all", "cheap", "volumetric", "synthetic"};
                            const auto selected = std::find(families.begin(), families.end(), launchOptions_.publicationFamily);
                            int family = static_cast<int>(selected - families.begin());
                            if (ImGui::Combo("##family", &family, choices, IM_ARRAYSIZE(choices)))
                                launchOptions_.publicationFamily = families[family];
                        });
                        ImGui::EndTable();
                    }
                    ImGui::SeparatorText("Publication benchmarks");
                    const auto launch = [&](const char* label, bool full, bool smoke = false, bool nullOnly = false) {
                        if (!ImGui::Button(label, {-1, 0})) return;
                        try {
                            startPublicationSuite(full, smoke, nullOnly);
                        } catch (const std::exception& error) {
                            publicationMessage_ = error.what();
                        }
                    };
                    launch("Run smoke test", false, true);
                    launch("Run quick suite", false);
                    launch("Run full suite", true);
                    launch("Run null checks", true, false, true);
                    if (!publicationMessage_.empty()) ImGui::TextWrapped("%s", publicationMessage_.c_str());
                    if (!publicationOutput_.empty()) ImGui::TextWrapped("Output: %s", publicationOutput_.string().c_str());
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::Separator();
            if(ImGui::Button("Reload shaders")) {sceneShader_.reload();loadSkyShader();}
            ImGui::SameLine();
            if(ImGui::Button("Screenshot")) saveScreenshot(frame);
            ImGui::EndDisabled();
            ImGui::TextDisabled("Ctrl+H: show / hide panel   Ctrl+F: fullscreen");
            ImGui::End();
        }

        void drawVolumeGui() {
            ImGui::SeparatorText("Volume quality");
            if(parameterTable("volume-quality")) {
                parameter("Sampling budget",[&]{
                    const char* choices[]{"Low (32 / 3)","LOM (64 / 6)","High (128 / 12)","Custom"};
                    if(ImGui::Combo("##budget",&volumeBudgetUi_,choices,4) && volumeBudgetUi_<3) volume_.budget(volumeBudgetUi_);
                });
                parameter("Cloud view steps",[&]{if(ImGui::SliderInt("##view",&volume_.cloudSamples,8,256)) volumeBudgetUi_=3;});
                parameter("Cloud light steps",[&]{if(ImGui::SliderInt("##light",&volume_.lightSamples,1,16)) volumeBudgetUi_=3;});
                parameter("Empty-space stride",[&]{if(ImGui::SliderInt("##empty",&volume_.emptyStep,1,4)) volumeBudgetUi_=3;});
                if(skyMode_==6) parameter("Aurora view steps",[&]{if(ImGui::SliderInt("##aurora",&volume_.auroraSamples,16,512)) volumeBudgetUi_=3;});
                ImGui::EndTable();
            }
            if(ImGui::CollapsingHeader("Cloud shape",ImGuiTreeNodeFlags_DefaultOpen) && parameterTable("volume-shape")) {
                parameter("Coverage",[&]{ImGui::SliderFloat("##c",&volume_.coverage,0,1,"%.3f");});
                parameter("Density",[&]{ImGui::SliderFloat("##d",&volume_.density,0,4,"%.3f");});
                parameter("Scale",[&]{ImGui::SliderFloat("##s",&volume_.scale,0.25f,3,"%.3f");});
                parameter("Depth / R",[&]{ImGui::SliderFloat("##h",&volume_.depth,0.04f,0.6f,"%.3f");});
                parameter("Cloud type",[&]{ImGui::SliderFloat("##t",&volume_.type,0,1,"%.3f");});
                parameter("Detail erosion",[&]{ImGui::SliderFloat("##e",&volume_.detail,0,1,"%.3f");});
                parameter("Weather variation",[&]{ImGui::SliderFloat("##w",&volume_.weather,0,0.8f,"%.3f");});
                ImGui::EndTable();
            }
            if(ImGui::CollapsingHeader("Cloud lighting and atmosphere") && parameterTable("volume-optics")) {
                parameter("Self-shadow",[&]{ImGui::SliderFloat("##s",&volume_.selfShadow,0,3,"%.3f");});
                parameter("Anisotropy",[&]{ImGui::SliderFloat("##g",&volume_.anisotropy,0,0.9f,"%.3f");});
                parameter("Multiple scattering",[&]{ImGui::SliderFloat("##m",&volume_.multipleScattering,0,1,"%.3f");});
                parameter("Powder",[&]{ImGui::SliderFloat("##p",&volume_.powder,0,1,"%.3f");});
                parameter("Light cone",[&]{ImGui::SliderFloat("##c",&volume_.lightCone,0,0.4f,"%.3f");});
                parameter("Jitter",[&]{ImGui::SliderFloat("##j",&volume_.jitter,0,1,"%.3f");});
                parameter("Softness",[&]{ImGui::SliderFloat("##soft",&volume_.softness,0.001f,0.15f,"%.3f");});
                parameter("Detail frequency",[&]{ImGui::SliderFloat("##freq",&volume_.detailScale,1,5,"%.3f");});
                parameter("Turbidity",[&]{ImGui::SliderFloat("##t",&volume_.turbidity,2,6,"%.3f");});
                parameter("Atmosphere exposure",[&]{ImGui::SliderFloat("##exp",&volume_.atmosphereExposure,0.005f,0.1f,"%.3f");});
                ImGui::EndTable();
            }
            if(skyMode_==6 && ImGui::CollapsingHeader("Aurora curtains",ImGuiTreeNodeFlags_DefaultOpen) && parameterTable("aurora-volume")) {
                parameter("Emission",[&]{ImGui::SliderFloat("##i",&volume_.auroraIntensity,0,5,"%.3f");});
                parameter("Base height / R",[&]{ImGui::SliderFloat("##b",&volume_.auroraBase,0.7f,4,"%.3f");});
                parameter("Layer depth / R",[&]{ImGui::SliderFloat("##h",&volume_.auroraHeight,0.5f,5,"%.3f");});
                parameter("Curtain width / R",[&]{ImGui::SliderFloat("##w",&volume_.auroraWidth,0.01f,0.15f,"%.3f");});
                parameter("Advection speed",[&]{ImGui::SliderFloat("##v",&volume_.auroraSpeed,0,0.4f,"%.3f");});
                ImGui::EndTable();
            }
        }

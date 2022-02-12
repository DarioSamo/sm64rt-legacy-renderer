//
// RT64
//

#ifndef RT64_MINIMAL

#include "rt64_inspector.h"

#include "rt64_device.h"
#include "rt64_scene.h"
#include "rt64_view.h"

#include "im3d/im3d.h"
#include "im3d/im3d_math.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>

std::string dateAsFilename() {
    std::time_t time = std::time(nullptr);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%F_%T");
    std::string s = ss.str();
    std::replace(s.begin(), s.end(), ':', '-');
    return s;
}

RT64::Inspector::Inspector(Device* device) {
    this->device = device;
    prevCursorX = prevCursorY = 0;
    cameraControl = false;
    cameraPanX = 0.0f;
    cameraPanY = 0.0f;
    dumpFrameCount = 0;
    sceneDesc = nullptr;
    material = nullptr;
    lights = nullptr;
    lightCount = 0;
    maxLightCount = 0;

    // Im3D
    Im3d::AppData &appData = Im3d::GetAppData();

    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();


    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    D3D12_CHECK(device->getD3D12Device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&d3dSrvDescHeap)));

    ImGui_ImplWin32_Init(device->getHwnd());
    ImGui_ImplDX12_Init(device->getD3D12Device(), 2, DXGI_FORMAT_R8G8B8A8_UNORM, d3dSrvDescHeap, d3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(), d3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

    device->addInspector(this);
}

RT64::Inspector::~Inspector() {
    device->removeInspector(this);
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    d3dSrvDescHeap->Release();
}

void RT64::Inspector::render(View *activeView, int cursorX, int cursorY) {
    setupWithView(activeView, cursorX, cursorY);
    
    // Start the frame.
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    Im3d::NewFrame();

    renderViewParams(activeView);
    renderSceneInspector();
    renderMaterialInspector();
    renderLightInspector();
    renderCameraControl(activeView, cursorX, cursorY);
    renderPrint();
    renderPostInspector(activeView);

    Im3d::EndFrame();

    // If dumping frames is active, save the current state of the RTV into a file.
    if (!dumpPath.empty()) {
        const int LeadingZeroes = 8;
        std::ostringstream oss;
        oss << dumpPath << "/" << std::setw(LeadingZeroes) << std::setfill('0') << dumpFrameCount++ << ".bmp";
        device->dumpRenderTarget(oss.str());
    }

    activeView->renderInspector(this);

    // Send the commands to D3D12.
    device->getD3D12CommandList()->SetDescriptorHeaps(1, &d3dSrvDescHeap);
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), device->getD3D12CommandList());
}

void RT64::Inspector::resize() {
    ImGui_ImplDX12_InvalidateDeviceObjects();
    ImGui_ImplDX12_CreateDeviceObjects();
}

void RT64::Inspector::renderViewParams(View *view) {
    assert(view != nullptr);

    ImGui::Begin("View Params Inspector");
    int diSamples = view->getDISamples();
    int giSamples = view->getGISamples();
    int maxLights = view->getMaxLights();
    int maxReflections = view->getMaxReflections();
    float motionBlurStrength = view->getMotionBlurStrength();
    int motionBlurSamples = view->getMotionBlurSamples();
    int visualizationMode = view->getVisualizationMode();
    int resScale = lround(view->getResolutionScale() * 100.0f);
    int upscaleMode = (int)(view->getUpscaleMode());
    bool denoiser = view->getDenoiserEnabled();
    bool volumetricEnabled = view->getVolumetricEnabledFlag();
    bool alternateSpecularLight = view->getAlternateSpecularEnabled();

    ImGui::DragInt("DI samples", &diSamples, 0.1f, 0, 32);
    ImGui::DragInt("GI samples", &giSamples, 0.1f, 0, 32);
    ImGui::DragInt("Max lights", &maxLights, 0.1f, 0, 16);
    ImGui::DragInt("Max reflections", &maxReflections, 0.1f, 0, 32);
    ImGui::DragFloat("Motion blur strength", &motionBlurStrength, 0.1f, 0.0f, 10.0f);
    ImGui::DragInt("Motion blur samples", &motionBlurSamples, 0.1f, 0, 256);
    ImGui::Combo("Visualization Mode", &visualizationMode, "Final\0Shading position\0Shading normal\0Shading specular\0Color\0Instance ID\0Direct light raw\0Direct light filtered\0Specular light\0Indirect light raw\0Indirect light filtered\0Reflection\0Refraction\0Transparent\0Motion vectors\0Depth\0Volumetrics\0Scene Fog\0Base Reflective Color\0");

#ifdef RT64_DLSS
    // Only show DLSS option if supported by the hardware.
    // FIXME: Concatenating these strings can be annoying due to the \0 characters, so just write out the two possible strings instead.
    bool dlssInitialized = view->getDlssInitialized();
    if (dlssInitialized) 
    {
        ImGui::Combo("Upscale Mode", &upscaleMode, "Bilinear\0AMD FidelityFX Super Resolution 1.0\0NVIDIA DLSS 2.3\0");
    }
    else
#endif
    {
        ImGui::Combo("Upscale Mode", &upscaleMode, "Bilinear\0AMD FidelityFX Super Resolution 1.0\0");
    }

#ifdef RT64_DLSS
    if ((RT64::UpscaleMode)(upscaleMode) == RT64::UpscaleMode::DLSS) 
    {
        int dlssQualityMode = (int)(view->getDlssQualityMode());
        float dlssSharpness = view->getDlssSharpness();
        bool dlssResolutionOverride = view->getDlssResolutionOverride();
        bool dlssAutoExposure = view->getDlssAutoExposure();

        ImGui::Combo("DLSS Quality", &dlssQualityMode, "Ultra Performance\0Max Performance\0Balanced\0Max Quality\0Auto\0");
        ImGui::DragFloat("DLSS Sharpness", &dlssSharpness, 0.01f, -1.0f, 1.0f);
        ImGui::Checkbox("DLSS Auto Exposure", &dlssAutoExposure);
        ImGui::Checkbox("DLSS Resolution Override", &dlssResolutionOverride);

        if (dlssResolutionOverride) {
            ImGui::SameLine();
            ImGui::DragInt("Resolution %", &resScale, 1, 1, 200);
        }

        view->setDlssQualityMode((DLSS::QualityMode)(dlssQualityMode));
        view->setDlssSharpness(dlssSharpness);
        view->setDlssAutoExposure(dlssAutoExposure);
        view->setDlssResolutionOverride(dlssResolutionOverride);
    }
    else
#endif
    {
        ImGui::DragInt("Resolution %", &resScale, 1, 1, 200);
    }

    ImGui::Checkbox("Denoiser", &denoiser);
    ImGui::Checkbox("Volumetrics", &volumetricEnabled);
    ImGui::Checkbox("Experimental Specular Lighting", &alternateSpecularLight);
    if (giSamples > 0) {
        bool alternateIndirect = view->getAlternateIndirectFlag();
        ImGui::Checkbox("Experimental Indirect Lighting", &alternateIndirect);
        view->setAlternateIndirectFlag(alternateIndirect);
    }

    if (volumetricEnabled)
    {
        int volumetricMaxSamples = view->getVolumetricMaxSamples();
        float volumetricIntensity = view->getVolumetricIntensity();

        ImGui::DragInt("Volumetric Samples", &volumetricMaxSamples, 0.1f, 32, 1028);
        ImGui::DragFloat("Volumetric Intensity", &volumetricIntensity, 0.01f, 0.0, 1.0);

        view->setVolumetricMaxSamples(volumetricMaxSamples);
        view->setVolumetricIntensity(volumetricIntensity);
    }

    // Dumping toggle.
    bool isDumping = !dumpPath.empty();
    if (ImGui::Button(isDumping ? "Stop dump" : "Dump frames")) {
        if (isDumping) {
            dumpPath = std::string();
        }
        else {
            dumpPath = "dump/" + dateAsFilename();
            std::filesystem::create_directories(dumpPath);
            dumpFrameCount = 0;
        }
    }

    // Update viewport parameters.
    view->setDISamples(diSamples);
    view->setGISamples(giSamples);
    view->setMaxLights(maxLights);
    view->setMaxReflections(maxReflections);
    view->setMotionBlurStrength(motionBlurStrength);
    view->setMotionBlurSamples(motionBlurSamples);
    view->setVisualizationMode(visualizationMode);
    view->setResolutionScale(resScale / 100.0f);
    view->setUpscaleMode((UpscaleMode)(upscaleMode));
    view->setDenoiserEnabled(denoiser);
    view->setVolumetricEnabledFlag(volumetricEnabled);
    view->setAlternateSpecularEnabled(alternateSpecularLight);

    ImGui::End();
}

// Render the Post-processing window
void RT64::Inspector::renderPostInspector(View* view) {
    assert(view != nullptr);

    ImGui::Begin("Post Processing");

    int tonemapMode = view->getToneMappingMode();
    float tonemapExposure = view->getToneMapExposure();
    float tonemapWhite = view->getToneMapWhitePoint();
    float tonemapBlack = view->getToneMapBlackLevel();
    float tonemapSaturation = view->getToneMapSaturation();
    float tonemapGamma = view->getToneMapGamma();
    bool eyeAdaption = view->getEyeAdaptionEnabledFlag();

    ImGui::Combo("Tonemapping Mode", &tonemapMode, "Raw Image\0Reinhard Tonemapper\0Reinhard-Luma\0Reinhard-Jodie\0Uncharted 2\0ACES Filmic\0Simple\0");
    ImGui::DragFloat("Exposure", &tonemapExposure, 0.01f, 0.0f, 20.0f);
    ImGui::DragFloat("White Point", &tonemapWhite, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Black Level", &tonemapBlack, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Saturation", &tonemapSaturation, 0.001f, 0.0f, 5.0f);
    ImGui::DragFloat("Gamma", &tonemapGamma, 0.001f, 0.0f, 2.0f);
    ImGui::Checkbox("Eye Adaption", &eyeAdaption);

    if (eyeAdaption)
    {
        float minLogLuminance = view->getMinLogLuminance();
        float logLuminanceRange = view->getLogLuminanceRange();
        float lumaUpdateTime = view->getLuminanceUpdateTime();
        float eyeAdaptionBrightnessFactor = view->getEyeAdaptionBrightnessFactor();

        ImGui::DragFloat("Eye Adaption Minimum", &minLogLuminance, 0.01f, -20.0f, 20.0f);
        ImGui::DragFloat("Eye Adaption Range", &logLuminanceRange, 0.01f, 0.0f, 20.0f);
        ImGui::DragFloat("Eye Adaption Update Time", &lumaUpdateTime, 0.01f, 0.0f, 4.0f);
        ImGui::DragFloat("Eye Adaption Brightening Factor", &eyeAdaptionBrightnessFactor, 0.01f, 1.0f, 20.0f);

        view->setMinLogLuminance(minLogLuminance);
        view->setLogLuminanceRange(logLuminanceRange);
        view->setLuminanceUpdateTime(lumaUpdateTime);
        view->setEyeAdaptionBrightnessFactor(eyeAdaptionBrightnessFactor);
    }

    view->setToneMappingMode(tonemapMode);
    view->setTonemapperValues(tonemapExposure, tonemapWhite, tonemapBlack, tonemapSaturation, tonemapGamma);
    view->setEyeAdaptionEnabledFlag(eyeAdaption);
    ImGui::End();
}

void RT64::Inspector::renderSceneInspector() {
    if (sceneDesc != nullptr) {
        ImGui::Begin("Scene Inspector");
        ImGui::DragFloat3("Ambient Base Color", &sceneDesc->ambientBaseColor.x, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat3("Ambient No GI Color", &sceneDesc->ambientNoGIColor.x, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat3("Eye Light Diffuse Color", &sceneDesc->eyeLightDiffuseColor.x, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat3("Eye Light Specular Color", &sceneDesc->eyeLightSpecularColor.x, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat3("Sky Diffuse Multiplier", &sceneDesc->skyDiffuseMultiplier.x, 0.01f, 0.0f, 5.0f);
        ImGui::DragFloat3("Sky HSL Modifier", &sceneDesc->skyHSLModifier.x, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat("Sky Yaw Offset", &sceneDesc->skyYawOffset, 0.01f, 0.0f, Im3d::TwoPi);
        ImGui::DragFloat("GI Diffuse Strength", &sceneDesc->giDiffuseStrength, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat("GI Sky Strength", &sceneDesc->giSkyStrength, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat3("Ambient Fog Color", &sceneDesc->ambientFogColor.x, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat("Ambient Fog Alpha", &sceneDesc->ambientFogAlpha, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Ambient Fog Factors", &sceneDesc->ambientFogFactors.x, 1.00f, 0.0f, 1000000.0f);
        ImGui::DragFloat3("Ground Fog Color", &sceneDesc->groundFogColor.x, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat("Ground Fog Alpha", &sceneDesc->groundFogAlpha, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Ground Fog Factors", &sceneDesc->groundFogFactors.x, 1.00f, -1000000.0f, 1000000.0f);
        ImGui::DragFloat2("Ground Fog Height Factors", &sceneDesc->groundFogHeightFactors.x, 1.00f, -1000000.0f, 1000000.0f);
        ImGui::End();
    }
}

void RT64::Inspector::renderMaterialInspector() {
    if (material != nullptr) {
        ImGui::Begin("Material Inspector");
        ImGui::Text("Name: %s", materialName.c_str());

        auto pushCommon = [](const char* name, int mask, int* attributes) {
            bool checkboxValue = *attributes & mask;
            ImGui::PushID(name);

            ImGui::Checkbox("", &checkboxValue);
            if (checkboxValue) {
                *attributes |= mask;
            }
            else {
                *attributes &= ~(mask);
            }

            ImGui::SameLine();

            if (checkboxValue) {
                ImGui::Text(name);
            }
            else {
                ImGui::TextDisabled(name);
            }

            return checkboxValue;
        };

        auto pushFloat = [pushCommon](const char* name, int mask, float *v, int *attributes, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f) {
            if (pushCommon(name, mask, attributes)) {
                ImGui::SameLine();
                ImGui::DragFloat("V", v, v_speed, v_min, v_max);
            }

            ImGui::PopID();
        };

        auto pushVector3 = [pushCommon](const char *name, int mask, RT64_VECTOR3 *v, int* attributes, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f) {
            if (pushCommon(name, mask, attributes)) {
                ImGui::SameLine();
                ImGui::DragFloat3("V", &v->x, v_speed, v_min, v_max);
            }

            ImGui::PopID();
        };

        auto pushVector4 = [pushCommon](const char *name, int mask, RT64_VECTOR4 *v, int *attributes, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f) {
            if (pushCommon(name, mask, attributes)) {
                ImGui::SameLine();
                ImGui::DragFloat4("V", &v->x, v_speed, v_min, v_max);
            }

            ImGui::PopID();
        };

        auto pushInt = [pushCommon](const char *name, int mask, int *v, int *attributes) {
            if (pushCommon(name, mask, attributes)) {
                ImGui::SameLine();
                ImGui::InputInt("V", v);
            }

            ImGui::PopID();
        };

        pushFloat("Ignore normal factor", RT64_ATTRIBUTE_IGNORE_NORMAL_FACTOR, &material->ignoreNormalFactor, &material->enabledAttributes, 1.0f, 0.0f, 1.0f);
        pushFloat("UV detail scale", RT64_ATTRIBUTE_UV_DETAIL_SCALE, &material->uvDetailScale, &material->enabledAttributes, 0.01f, -50.0f, 50.0f);
        pushFloat("Reflection factor", RT64_ATTRIBUTE_REFLECTION_FACTOR, &material->reflectionFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushFloat("Reflection fresnel factor", RT64_ATTRIBUTE_REFLECTION_FRESNEL_FACTOR, &material->reflectionFresnelFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushFloat("Reflection shine factor", RT64_ATTRIBUTE_REFLECTION_SHINE_FACTOR, &material->reflectionShineFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushFloat("Refraction factor", RT64_ATTRIBUTE_REFRACTION_FACTOR, &material->refractionFactor, &material->enabledAttributes, 0.01f, 0.0f, 2.0f);
        pushVector3("Specular color", RT64_ATTRIBUTE_SPECULAR_COLOR, &material->specularColor, &material->enabledAttributes, 0.01f, 0.0f, 100.0f);
        pushFloat("Specular fresnel factor", RT64_ATTRIBUTE_SPECULAR_FRESNEL_FACTOR, &material->specularFresnelFactor, &material->enabledAttributes, 0.001f, 0.0f, 1.0f);
        pushFloat("Specular exponent", RT64_ATTRIBUTE_SPECULAR_EXPONENT, &material->specularExponent, &material->enabledAttributes, 0.1f, 0.0f, 1000.0f);
        pushFloat("Roughness factor", RT64_ATTRIBUTE_ROUGHNESS_FACTOR, &material->roughnessFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushFloat("Metallic factor", RT64_ATTRIBUTE_METALLIC_FACTOR, &material->metallicFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushFloat("Solid alpha multiplier", RT64_ATTRIBUTE_SOLID_ALPHA_MULTIPLIER, &material->solidAlphaMultiplier, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
        pushFloat("Shadow alpha multiplier", RT64_ATTRIBUTE_SHADOW_ALPHA_MULTIPLIER, &material->shadowAlphaMultiplier, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
        pushFloat("Depth bias", RT64_ATTRIBUTE_DEPTH_BIAS, &material->depthBias, &material->enabledAttributes, 1.0f, -1000.0f, 1000.0f);
        pushFloat("Shadow ray bias", RT64_ATTRIBUTE_SHADOW_RAY_BIAS, &material->shadowRayBias, &material->enabledAttributes, 1.0f, 0.0f, 1000.0f);
        pushVector3("Self light", RT64_ATTRIBUTE_SELF_LIGHT, &material->selfLight, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
        pushVector4("Diffuse color mix", RT64_ATTRIBUTE_DIFFUSE_COLOR_MIX, &material->diffuseColorMix, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushInt("Light group mask bits", RT64_ATTRIBUTE_LIGHT_GROUP_MASK_BITS, (int *)(&material->lightGroupMaskBits), &material->enabledAttributes);

        ImGui::End();
    }
}

void RT64::Inspector::renderLightInspector() {
    if (lights != nullptr) {
        ImGui::Begin("Light Inspector");
        ImGui::InputInt("Light count", lightCount);
        *lightCount = std::min(std::max(*lightCount, 1), maxLightCount);

        for (int i = 0; i < *lightCount; i++) {
            ImGui::PushID(i);
            Im3d::PushId(i);

            if (ImGui::CollapsingHeader("Point light")) {
                const int SphereDetail = 64;
                ImGui::DragFloat3("Position", &lights[i].position.x);
                Im3d::GizmoTranslation("GizmoPosition", &lights[i].position.x);
                ImGui::DragFloat3("Diffuse color", &lights[i].diffuseColor.x, 0.01f);
                ImGui::DragFloat("Attenuation radius", &lights[i].attenuationRadius);
                Im3d::SetColor(lights[i].diffuseColor.x, lights[i].diffuseColor.y, lights[i].diffuseColor.z);
                Im3d::DrawSphere(Im3d::Vec3(lights[i].position.x, lights[i].position.y, lights[i].position.z), lights[i].attenuationRadius, SphereDetail);
                ImGui::DragFloat("Point radius", &lights[i].pointRadius);
                Im3d::SetColor(lights[i].diffuseColor.x * 0.5f, lights[i].diffuseColor.y * 0.5f, lights[i].diffuseColor.z * 0.5f);
                Im3d::DrawSphere(Im3d::Vec3(lights[i].position.x, lights[i].position.y, lights[i].position.z), lights[i].pointRadius, SphereDetail);
                ImGui::DragFloat3("Specular highlight color", &lights[i].specularColor.x, 0.01f);
                ImGui::DragFloat("Shadow offset", &lights[i].shadowOffset);
                Im3d::SetColor(Im3d::Color_Black);
                Im3d::DrawSphere(Im3d::Vec3(lights[i].position.x, lights[i].position.y, lights[i].position.z), lights[i].shadowOffset, SphereDetail);
                ImGui::DragFloat("Attenuation exponent", &lights[i].attenuationExponent);
                ImGui::DragFloat("Flicker intensity", &lights[i].flickerIntensity);
                ImGui::InputInt("Group bits", (int *)(&lights[i].groupBits));

                if ((*lightCount) < maxLightCount) {
                    if (ImGui::Button("Duplicate")) {
                        lights[*lightCount] = lights[i];
                        *lightCount = *lightCount + 1;
                    }
                }
            }

            Im3d::PopId();
            ImGui::PopID();
        }
        ImGui::End();
    }
}

void RT64::Inspector::renderCameraControl(View *view, int cursorX, int cursorY) {
    assert(view != nullptr);

    if (ImGui::Begin("Camera controls")) {
        ImGui::Checkbox("Enable", &cameraControl);
        view->setPerspectiveControlActive(cameraControl);
        if (cameraControl) {
            ImGui::DragFloat("Camera pan x", &cameraPanX, 0.1f, -100.0f, 100.0f);
            ImGui::DragFloat("Camera pan y", &cameraPanY, 0.1f, -100.0f, 100.0f);

            if ((cameraPanX != 0.0f) || (cameraPanY != 0.0f)) {
                view->movePerspective({ cameraPanX, cameraPanY, 0.0f });
            }
            else if (!ImGui::GetIO().WantCaptureMouse) {
                float cameraSpeed = (view->getFarDistance() - view->getNearDistance()) / 5.0f;
                bool leftAlt = GetAsyncKeyState(VK_LMENU) & 0x8000;
                bool leftCtrl = GetAsyncKeyState(VK_LCONTROL) & 0x8000;
                float localX = (cursorX - prevCursorX) / (float)(view->getWidth());
                float localY = (cursorY - prevCursorY) / (float)(view->getHeight());
                localX += cameraPanX;
                localY += cameraPanY;

                if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) {
                    if (leftCtrl) {
                        view->movePerspective({ 0.0f, 0.0f, (localX + localY) * cameraSpeed });
                    }
                    else if (leftAlt) {
                        float cameraRotationSpeed = 5.0f;
                        view->rotatePerspective(0.0f, -localX * cameraRotationSpeed, -localY * cameraRotationSpeed);
                    }
                    else {
                        view->movePerspective({ -localX * cameraSpeed, localY * cameraSpeed, 0.0f });
                    }
                }
            }

            prevCursorX = cursorX;
            prevCursorY = cursorY;

            ImGui::Text("Middle Button: Pan");
            ImGui::Text("Ctrl + Middle Button: Zoom");
            ImGui::Text("Alt + Middle Button: Rotate");
        }
        else {
            cameraPanX = 0.0f;
            cameraPanY = 0.0f;
        }

        ImGui::End();
    }
}

void RT64::Inspector::renderPrint() {
    if (!printMessages.empty()) {
        ImGui::Begin("Print");
        for (size_t i = 0; i < printMessages.size(); i++) {
            ImGui::Text("%s", printMessages[i].c_str());
        }
        ImGui::End();
    }
}

void RT64::Inspector::setupWithView(View *view, int cursorX, int cursorY) {
    assert(view != nullptr);
    Im3d::AppData &appData = Im3d::GetAppData();
    RT64_VECTOR3 viewPos = view->getViewPosition();
    RT64_VECTOR3 viewDir = view->getViewDirection();
    RT64_VECTOR3 rayDir = view->getRayDirectionAt(cursorX, cursorY);
    appData.m_deltaTime = 1.0f / 30.0f;
    appData.m_viewportSize = Im3d::Vec2((float)(view->getWidth()), (float)(view->getHeight()));
    appData.m_viewOrigin = Im3d::Vec3(viewPos.x, viewPos.y, viewPos.z);
    appData.m_viewDirection = Im3d::Vec3(viewDir.x, viewDir.y, viewDir.z);
    appData.m_worldUp = Im3d::Vec3(0.0f, 1.0f, 0.0f);
    appData.m_projOrtho = false;
    appData.m_projScaleY = tanf(view->getFOVRadians() * 0.5f) * 2.0f;
    appData.m_snapTranslation = 0.0f;
    appData.m_snapRotation = 0.0f;
    appData.m_snapScale = 0.0f;
    appData.m_cursorRayOrigin = Im3d::Vec3(viewPos.x, viewPos.y, viewPos.z);
    appData.m_cursorRayDirection = Im3d::Vec3(rayDir.x, rayDir.y, rayDir.z);
    appData.m_keyDown[Im3d::Mouse_Left] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

void RT64::Inspector::setSceneDescription(RT64_SCENE_DESC* sceneDesc) {
    this->sceneDesc = sceneDesc;
}

void RT64::Inspector::setMaterial(RT64_MATERIAL* material, const std::string &materialName) {
    this->material = material;
    this->materialName = materialName;
}

void RT64::Inspector::setLights(RT64_LIGHT* lights, int *lightCount, int maxLightCount) {
    this->lights = lights;
    this->lightCount = lightCount;
    this->maxLightCount = maxLightCount;
}

void RT64::Inspector::printClear() {
    printMessages.clear();
}

void RT64::Inspector::printMessage(const std::string& message) {
    printMessages.push_back(message);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool RT64::Inspector::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    return ImGui_ImplWin32_WndProcHandler(device->getHwnd(), msg, wParam, lParam);
}

// Public

DLLEXPORT RT64_INSPECTOR* RT64_CreateInspector(RT64_DEVICE* devicePtr) {
    assert(devicePtr != nullptr);
    RT64::Device* device = (RT64::Device*)(devicePtr);
    RT64::Inspector* inspector = new RT64::Inspector(device);
    return (RT64_INSPECTOR*)(inspector);
}

DLLEXPORT bool RT64_HandleMessageInspector(RT64_INSPECTOR* inspectorPtr, UINT msg, WPARAM wParam, LPARAM lParam) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    return inspector->handleMessage(msg, wParam, lParam);
}

DLLEXPORT void RT64_SetSceneInspector(RT64_INSPECTOR* inspectorPtr, RT64_SCENE_DESC* sceneDesc) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setSceneDescription(sceneDesc);
}

DLLEXPORT void RT64_SetMaterialInspector(RT64_INSPECTOR* inspectorPtr, RT64_MATERIAL* material, const char* materialName) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setMaterial(material, std::string(materialName));
}

DLLEXPORT void RT64_SetLightsInspector(RT64_INSPECTOR* inspectorPtr, RT64_LIGHT* lights, int* lightCount, int maxLightCount) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setLights(lights, lightCount, maxLightCount);
}

DLLEXPORT void RT64_PrintClearInspector(RT64_INSPECTOR* inspectorPtr) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->printClear();
}

DLLEXPORT void RT64_PrintMessageInspector(RT64_INSPECTOR* inspectorPtr, const char* message) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    std::string messageStr(message);
    inspector->printMessage(messageStr);
}

DLLEXPORT void RT64_DestroyInspector(RT64_INSPECTOR* inspectorPtr) {
    delete (RT64::Inspector*)(inspectorPtr);
}

#endif
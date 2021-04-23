//
// RT64
//

#include "rt64_inspector.h"

#include "rt64_device.h"
#include "rt64_scene.h"
#include "rt64_view.h"

#include "im3d/im3d.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"

RT64::Inspector::Inspector(Device* device) {
    this->device = device;
    prevCursorX = prevCursorY = 0;
    cameraControl = false;

    reset();

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
    ThrowIfFailed(device->getD3D12Device()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&d3dSrvDescHeap)));

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

void RT64::Inspector::reset() {
    material = nullptr;
    lights = nullptr;
    lightCount = 0;
    maxLightCount = 0;
    toPrint.clear();
}

void RT64::Inspector::render(View *activeView, int cursorX, int cursorY) {
    setupWithView(activeView, cursorX, cursorY);
    
    // Start the frame.
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    Im3d::NewFrame();

    renderViewParams(activeView);
    renderMaterialInspector();
    renderLightInspector();
    renderCameraControl(activeView, cursorX, cursorY);
    renderPrint();

    Im3d::EndFrame();

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
    int softLightSamples = view->getSoftLightSamples();
    int giBounces = view->getGIBounces();
    int maxLightSamples = view->getMaxLightSamples();
	float ambGIMix = view->getAmbGIMixWeight();
    int resScale = lround(view->getResolutionScale() * 100.0f);
    bool denoiser = view->getDenoiserEnabled();
    ImGui::DragInt("Light samples", &softLightSamples, 0.1f, 0, 32);
    ImGui::DragInt("GI Bounces", &giBounces, 0.1f, 0, 32);
    ImGui::DragInt("Max lights", &maxLightSamples, 0.1f, 0, 16);
	ImGui::DragFloat("Ambient GI Mix", &ambGIMix, 0.01f, 0.0f, 1.0f);
    ImGui::DragInt("Resolution %", &resScale, 1, 1, 200);
    ImGui::Checkbox("NVIDIA OptiX Denoiser", &denoiser);
    view->setSoftLightSamples(softLightSamples);
    view->setGIBounces(giBounces);
    view->setMaxLightSamples(maxLightSamples);
	view->setAmbGIMixWeight(ambGIMix);
    view->setResolutionScale(resScale / 100.0f);
    view->setDenoiserEnabled(denoiser);
    ImGui::End();
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

        auto pushVector4 = [pushCommon](const char* name, int mask, RT64_VECTOR4 *v, int* attributes, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f) {
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
        pushFloat("Normal map scale", RT64_ATTRIBUTE_NORMAL_MAP_SCALE, &material->normalMapScale, &material->enabledAttributes, 0.01f, -50.0f, 50.0f);
        pushFloat("Reflection factor", RT64_ATTRIBUTE_REFLECTION_FACTOR, &material->reflectionFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushFloat("Reflection fresnel factor", RT64_ATTRIBUTE_REFLECTION_FRESNEL_FACTOR, &material->reflectionFresnelFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushFloat("Reflection shine factor", RT64_ATTRIBUTE_REFLECTION_SHINE_FACTOR, &material->reflectionShineFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushFloat("Refraction factor", RT64_ATTRIBUTE_REFRACTION_FACTOR, &material->refractionFactor, &material->enabledAttributes, 0.01f, 0.0f, 2.0f);
        pushFloat("Specular intensity", RT64_ATTRIBUTE_SPECULAR_INTENSITY, &material->specularIntensity, &material->enabledAttributes, 0.01f, 0.0f, 100.0f);
        pushFloat("Specular exponent", RT64_ATTRIBUTE_SPECULAR_EXPONENT, &material->specularExponent, &material->enabledAttributes, 0.1f, 0.0f, 1000.0f);
        pushFloat("Solid alpha multiplier", RT64_ATTRIBUTE_SOLID_ALPHA_MULTIPLIER, &material->solidAlphaMultiplier, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
        pushFloat("Shadow alpha multiplier", RT64_ATTRIBUTE_SHADOW_ALPHA_MULTIPLIER, &material->shadowAlphaMultiplier, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
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

            // Rest of the lights.
            if (i > 0) {
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
                    ImGui::DragFloat("Specular intensity", &lights[i].specularIntensity);
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
            }
            // Ambient light.
            else {
                if (ImGui::CollapsingHeader("Ambient light")) {
                    ImGui::DragFloat3("Diffuse color", &lights[i].diffuseColor.x, 0.01f);
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
            if (!ImGui::GetIO().WantCaptureMouse) {
                float cameraSpeed = (view->getFarDistance() - view->getNearDistance()) / 5.0f;
                bool leftAlt = GetAsyncKeyState(VK_LMENU) & 0x8000;
                bool leftCtrl = GetAsyncKeyState(VK_LCONTROL) & 0x8000;
                float localX = (cursorX - prevCursorX) / (float)(view->getWidth());
                float localY = (cursorY - prevCursorY) / (float)(view->getHeight());
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

        ImGui::End();
    }
}

void RT64::Inspector::renderPrint() {
    if (!toPrint.empty()) {
        ImGui::Begin("Print");
        for (size_t i = 0; i < toPrint.size(); i++) {
            ImGui::Text("%s", toPrint[i].c_str());
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

void RT64::Inspector::setMaterial(RT64_MATERIAL* material, const std::string &materialName) {
    this->material = material;
    this->materialName = materialName;
}

void RT64::Inspector::setLights(RT64_LIGHT* lights, int *lightCount, int maxLightCount) {
    this->lights = lights;
    this->lightCount = lightCount;
    this->maxLightCount = maxLightCount;
}

void RT64::Inspector::print(const std::string& message) {
    toPrint.push_back(message);
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

DLLEXPORT void RT64_SetMaterialInspector(RT64_INSPECTOR* inspectorPtr, RT64_MATERIAL* material, const char *materialName) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setMaterial(material, std::string(materialName));
}

DLLEXPORT void RT64_SetLightsInspector(RT64_INSPECTOR* inspectorPtr, RT64_LIGHT *lights, int *lightCount, int maxLightCount) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setLights(lights, lightCount, maxLightCount);
}

DLLEXPORT void RT64_PrintToInspector(RT64_INSPECTOR* inspectorPtr, const char* message) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    std::string messageStr(message);
    inspector->print(messageStr);
}

DLLEXPORT void RT64_DestroyInspector(RT64_INSPECTOR* inspectorPtr) {
    delete (RT64::Inspector*)(inspectorPtr);
}
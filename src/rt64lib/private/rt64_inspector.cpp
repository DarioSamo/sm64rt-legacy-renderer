//
// RT64
//

#include "rt64_inspector.h"

#include "rt64_device.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_impl_win32.h"

// Inspector

RT64::Inspector::Inspector(Device* device) {
    this->device = device;

    reset();

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

void RT64::Inspector::render() {
    // Start the frame.
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    renderMaterialInspector();
    renderLightInspector();
    renderPrint();

    // Send the commands to D3D12.
    device->getD3D12CommandList()->SetDescriptorHeaps(1, &d3dSrvDescHeap);
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), device->getD3D12CommandList());
}

void RT64::Inspector::resize() {
    ImGui_ImplDX12_InvalidateDeviceObjects();
    ImGui_ImplDX12_CreateDeviceObjects();
}

void RT64::Inspector::renderMaterialInspector() {
    if (material != nullptr) {
        ImGui::Begin("Material Inspector");

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
        
        pushFloat("Ignore normal factor", RT64_ATTRIBUTE_IGNORE_NORMAL_FACTOR, &material->ignoreNormalFactor, &material->enabledAttributes, 1.0f, 0.0f, 1.0f);
        pushFloat("Normal map scale", RT64_ATTRIBUTE_NORMAL_MAP_SCALE, &material->normalMapScale, &material->enabledAttributes, 0.01f, -50.0f, 50.0f);
        pushFloat("Reflection factor", RT64_ATTRIBUTE_REFLECTION_FACTOR, &material->reflectionFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushFloat("Reflection shine factor", RT64_ATTRIBUTE_REFLECTION_SHINE_FACTOR, &material->reflectionShineFactor, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);
        pushFloat("Refraction factor", RT64_ATTRIBUTE_REFRACTION_FACTOR, &material->refractionFactor, &material->enabledAttributes, 0.01f, 0.0f, 2.0f);
        pushFloat("Specular intensity", RT64_ATTRIBUTE_SPECULAR_INTENSITY, &material->specularIntensity, &material->enabledAttributes, 0.01f, 0.0f, 100.0f);
        pushFloat("Specular exponent", RT64_ATTRIBUTE_SPECULAR_EXPONENT, &material->specularExponent, &material->enabledAttributes, 0.1f, 0.0f, 1000.0f);
        pushFloat("Solid alpha multiplier", RT64_ATTRIBUTE_SOLID_ALPHA_MULTIPLIER, &material->solidAlphaMultiplier, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
        pushFloat("Shadow alpha multiplier", RT64_ATTRIBUTE_SHADOW_ALPHA_MULTIPLIER, &material->shadowAlphaMultiplier, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
        pushVector3("Self light", RT64_ATTRIBUTE_SELF_LIGHT, &material->selfLight, &material->enabledAttributes, 0.01f, 0.0f, 10.0f);
        pushVector4("Diffuse color mix", RT64_ATTRIBUTE_DIFFUSE_COLOR_MIX, &material->diffuseColorMix, &material->enabledAttributes, 0.01f, 0.0f, 1.0f);

        ImGui::End();
    }
}

void RT64::Inspector::renderLightInspector() {
    if (lights != nullptr) {
        ImGui::Begin("Light Inspector");
        ImGui::InputInt("Light count", lightCount);
        *lightCount = min(max(*lightCount, 1), maxLightCount);

        for (int i = 0; i < *lightCount; i++) {
            ImGui::PushID(i);

            // Rest of the lights.
            if (i > 0) {
                if (ImGui::CollapsingHeader("Point light")) {
                    ImGui::DragFloat3("Position", &lights[i].position.x);
                    ImGui::DragFloat3("Diffuse color", &lights[i].diffuseColor.x, 0.01f);
                    ImGui::DragFloat("Attenuation radius", &lights[i].attenuationRadius);
                    ImGui::DragFloat("Point radius", &lights[i].pointRadius);
                    ImGui::DragFloat("Specular intensity", &lights[i].specularIntensity);
                    ImGui::DragFloat("Shadow offset", &lights[i].shadowOffset);
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

            ImGui::PopID();
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

void RT64::Inspector::setMaterial(RT64_MATERIAL* material) {
    this->material = material;
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

DLLEXPORT void RT64_SetMaterialInspector(RT64_INSPECTOR* inspectorPtr, RT64_MATERIAL* material) {
    assert(inspectorPtr != nullptr);
    RT64::Inspector* inspector = (RT64::Inspector*)(inspectorPtr);
    inspector->setMaterial(material);
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
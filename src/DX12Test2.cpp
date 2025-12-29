// This app won't run on non-Windows platforms so why bother compiling it there.
#ifdef _WIN32

#include "DXHelper.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <glm/glm.hpp>
#include <Windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#pragma message("d3d12.h path: " __FILE__)
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <d3dx12/d3dx12.h>
//#include <directx/d3dx12/d3dx12.h>
#pragma message("d3dx12.h path: " __FILE__)
#include <GLFW/glfw3.h>
#include "ShaderCompiler.h"

#ifdef _DEBUG
//#define D3DCOMPILE_DEBUG 1
#endif

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
//#include <d3d12sdklayers.h>
using Microsoft::WRL::ComPtr;
struct Vertex {
    glm::vec3 position;
    glm::vec4 color;
};

static constexpr uint16_t FrameCount = 2;
static uint16_t m_width= 0;
static uint16_t m_height = 0;

std::unique_ptr<SlangCompiler> compiler;

CD3DX12_VIEWPORT m_viewport;
CD3DX12_RECT m_scissorRect;
ComPtr<IDXGISwapChain3> m_swapChain;
ComPtr<ID3D12Device> m_device;
ComPtr<ID3D12Resource> m_renderTargets[FrameCount]; 
ComPtr<ID3D12CommandAllocator> m_commandAllocator;
ComPtr<ID3D12CommandQueue> m_commandQueue;
ComPtr<ID3D12RootSignature> m_rootSignature;
ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
ComPtr<ID3D12PipelineState> m_pipelineState;
ComPtr<ID3D12GraphicsCommandList> m_commandList;
uint32_t m_rtvDescriptorSize;

// App resources
ComPtr<ID3D12Resource> m_vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

// Sync objects
int32_t m_FrameIndex;
HANDLE m_fenceEvent;
ComPtr<ID3D12Fence> m_fence;
uint64_t m_fenceValue;


GLFWwindow* window;
HWND windowHandle;

void GetHardwareAdapter(IDXGIFactory1* pFactory,
    IDXGIAdapter1** ppAdapter, bool requestHighPerformanceAdapter = true)
{
    ComPtr<IDXGIAdapter1> adapter;
    *ppAdapter = nullptr;
	ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        for (UINT adapterIndex = 0;
            SUCCEEDED(factory6->EnumAdapterByGpuPreference(adapterIndex, 
                requestHighPerformanceAdapter == true ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE : DXGI_GPU_PREFERENCE_UNSPECIFIED, 
                IID_PPV_ARGS(&adapter)));
            ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select the Basic Render Driver adapter.
                continue;
            }
            // Check to see if the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(
                adapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                __uuidof(ID3D12Device),
                nullptr)))
            {
                break;
            }
        }
    }
    if (adapter.Get() == nullptr) {
        for (uint32_t adapterIndex = 0;
            SUCCEEDED(pFactory->EnumAdapters1(adapterIndex, &adapter)); ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                // Don't select Basic Render Driver adapter
                continue;
            }


            // Check to see whether the adapter supports D3D12
            // Don't create the device yet
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            {
                break;
            }
        }
    }
    *ppAdapter = adapter.Detach();
}

void WaitForPreviousFrame() {
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This code is implemented for simplicity
    // Refer to D3D12HelloFrameBuffering sample for effective fence utilization

    const uint64_t fence = m_fenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
    ++m_fenceValue;

    // Wait until the previous frame is finished
    if (m_fence->GetCompletedValue() < fence)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    m_FrameIndex = m_swapChain->GetCurrentBackBufferIndex();

}

void LoadPipeline()
{
	uint32_t dxgiFactoryFlags = 0;
#ifdef _DEBUG
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif
    ComPtr<IDXGIFactory6> factory;
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

	ComPtr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.Get(), &hardwareAdapter);

    ThrowIfFailed(D3D12CreateDevice(
        hardwareAdapter.Get(),
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&m_device)));

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(
        m_commandQueue.Get(),
        windowHandle,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ));

    ThrowIfFailed(factory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER));

    ThrowIfFailed(swapChain.As(&m_swapChain));
    m_FrameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    }

    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle{m_rtvHeap->GetCPUDescriptorHandleForHeapStart()};

        for (uint32_t n = 0; n < FrameCount; ++n) {
            ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
            m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

// Loads Shader and creates Graphics Pipeline
void LoadAssets(const std::string& shaderPath, const std::vector<std::string>& entryPoints)
{
    // Create empty root Signature
    {
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init(0, nullptr, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    // Create pipeline state
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> fragmentShader;

#ifdef _DEBUG
        uint32_t compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        uint32_t compileFlags = 0;
#endif
        // TODO: load from SlangCompiler
        // ATTENZIONEEEEEE SIGNORE LOAD FROM SLANGCOMPILER THE DEPENDENCIES ARE SET
		//std::vector<std::string> entryPoints = { "VSMain", "FSMain" };
        std::ifstream shaderStream(shaderPath, std::ios::in);
        if (!shaderStream.is_open()) {
			throw std::runtime_error("Failed to open shader file: " + shaderPath);
        }
		std::string shader = std::string(std::istreambuf_iterator<char>(shaderStream), std::istreambuf_iterator<char>());

		std::vector<ShaderOutput> hlslShaders = compiler->compileToHLSL(shader, entryPoints);
        std::string vertexShaderStr = hlslShaders[0].asText();
        std::string fragmentShaderStr = hlslShaders[1].asText();
        // This is set to ignore hlsl includes.
        ThrowIfFailed(D3DCompile(vertexShaderStr.c_str(), vertexShaderStr.size(), nullptr, nullptr, nullptr, entryPoints[0].c_str(), "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
        ThrowIfFailed(D3DCompile(fragmentShaderStr.c_str(), fragmentShaderStr.size(), nullptr, nullptr, nullptr, entryPoints[1].c_str(), "ps_5_0", compileFlags, 0, &fragmentShader, nullptr));

        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = m_rootSignature.Get();
		psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS = CD3DX12_SHADER_BYTECODE(fragmentShader.Get());
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
    }

    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

    // Command lists are created in the recording state, but there is nothing to record
    // Main loop expects it closed
    ThrowIfFailed(m_commandList->Close());

    // Vertex buffer
    {
        // Chill it's just a test program
        Vertex triangleVertices[] =
        {
            Vertex{ glm::vec3{0.f, 0.25f, 0.f}, glm::vec4{1.f, 0.f, 0.f, 1.f} },
            Vertex{ glm::vec3{0.25f, -0.25f, 0.f}, glm::vec4{0.f, 1.f, 0.f, 1.f} },
            Vertex{ glm::vec3{-0.25f, -0.25f, 0.f}, glm::vec4{0.f, 0.f, 1.f, 1.f} }
        };

        const uint32_t vertexBufferSize = sizeof(triangleVertices);


        // Note: using upload heaps to transfer static data like vert buffers is not recommended.
        // Every time thee GPU needs it, the upload heap will be marshalled over.
        // Please read up on Default Heap usage. An upload heap is used here for simplicity.
        auto heapProperties{ CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD) };
        auto resourceDesc{ CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize) };
        ThrowIfFailed(m_device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)
        ));
        uint8_t* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0); // We don't intend to read from cpu
        ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
        m_vertexBuffer->Unmap(0, nullptr);

        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;

    }
    // Create Synchronization objects and wait until assets havee been uploaded to GPU
    {
        ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
        m_fenceValue = 1;

        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr) {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        // Wait for the command list to execute
        // We need to wait until the initialization finishes
        WaitForPreviousFrame();
    }
}

void initWindow(uint16_t width, uint16_t height, const char* title)
{
    m_width = width;
    m_height = height;
    CD3DX12_VIEWPORT viewPort{ 0.f, 0.f, static_cast<float>(width), static_cast<float>(height) };
    m_viewport = viewPort;
    CD3DX12_RECT scissorRect{ 0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height) };
    m_scissorRect = scissorRect;
    m_FrameIndex = 0;
    m_rtvDescriptorSize = 0;
    
    if (!glfwInit())
    {
		throw std::runtime_error("GLFW initialization failed");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); 
    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
		throw std::runtime_error("Window creation failed");
    }
	windowHandle = glfwGetWin32Window(window);

    std::vector<std::string> entryPoints{ "VSMain", "PSMain" };
    // TODO: give them their own little place to live
    LoadPipeline();
    LoadAssets("shaders/shader.slang", entryPoints);
}

void terminateWindow()
{
    glfwDestroyWindow(window);
    glfwTerminate();
}

void terminateRenderer()

{
    WaitForPreviousFrame();
    CloseHandle(m_fenceEvent);
}

void PopulateCommandList()
{
    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU.
    // Apps should use fences to determine GPU execution progress
    ThrowIfFailed(m_commandAllocator->Reset());

    // However when ExecuteCommandLIst() is called on a particular command list,
    // that command list can then be reset at any time and must be before re-recording
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissorRect);

    // Indicate that draw commands will draw on back buffer
    auto resourceBarrier{ CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_FrameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET) };
    m_commandList->ResourceBarrier(1, &resourceBarrier);


    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_FrameIndex, m_rtvDescriptorSize);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Record commands
    const float clearColor[]{ 0.f, .2f, .4f, 1.f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->DrawInstanced(3, 1, 0, 0);
	auto resourceBarrier2{ CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_FrameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT) };
	m_commandList->ResourceBarrier(1, &resourceBarrier2);
    ThrowIfFailed(m_commandList->Close());
}

void render() 
{
    PopulateCommandList();

    ID3D12CommandList* ppCommandLists[]{ m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present
    ThrowIfFailed(m_swapChain->Present(1, 0));
    //HRESULT reason = m_device->GetDeviceRemovedReason();
    //std::cout << reason << std::endl;
    WaitForPreviousFrame();

}

int main()
{
	compiler = std::make_unique<SlangCompiler>();
	initWindow(800, 600, "DX12 Test2");
    while (!glfwWindowShouldClose(window))
    {
        render();
        glfwPollEvents();
	}
	terminateWindow();
}

#endif

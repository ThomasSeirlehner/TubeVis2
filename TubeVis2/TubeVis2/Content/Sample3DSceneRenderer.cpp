#define NOMINMAX

#include "pch.h"
#include "Sample3DSceneRenderer.h"

#include "..\Common\DirectXHelper.h"
#include <ppltasks.h>
#include <synchapi.h>
using namespace std;



using namespace Windows::UI::Core;

using namespace TubeVis2;

using namespace Concurrency;
using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation;
using namespace Windows::Storage;

// Indizes in die Anwendungsstatuszuweisung.
Platform::String^ AngleKey = "Angle";
Platform::String^ TrackingKey = "Tracking";

XMVECTORF32 playerPosition;
XMVECTORF32 lookAt;
XMVECTORF32 up;

ComPtr<ID3D12Fence> m_computeFence;
UINT64 m_computeFenceValue = 0;
HANDLE m_computeFenceEvent;

D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle;
D3D12_CPU_DESCRIPTOR_HANDLE uavHandle;
D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;


// Lädt den Scheitelpunkt und die Pixel-Shader aus den Dateien und instanziiert die Würfelgeometrie.
Sample3DSceneRenderer::Sample3DSceneRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources) :
	m_loadingComplete(false),
	m_radiansPerSecond(XM_PIDIV4),	// 45 Grad pro Sekunde drehen
	m_angle(0),
	m_tracking(false),
	m_mappedConstantBuffer(nullptr),
	m_deviceResources(deviceResources)
{
	LoadState();
	ZeroMemory(&m_constantBufferData, sizeof(m_constantBufferData));

	CreateDeviceDependentResources();
	CreateWindowSizeDependentResources();
}

Sample3DSceneRenderer::~Sample3DSceneRenderer()
{
	m_constantBuffer->Unmap(0, nullptr);
	m_mappedConstantBuffer = nullptr;
	CloseHandle(m_computeFenceEvent);
}

void Sample3DSceneRenderer::CreateDeviceDependentResources()
{
	auto d3dDevice = m_deviceResources->GetD3DDevice();

	DX::ThrowIfFailed(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeFence)));
	m_computeFenceValue = 1;
	m_computeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (m_computeFenceEvent == nullptr)
	{
		DX::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}

	// Stammsignatur mit einem einzelnen konstanten Pufferslot erstellen.
	{
		CD3DX12_DESCRIPTOR_RANGE range[3];
		CD3DX12_ROOT_PARAMETER parameters[3];

		// CBV for vertex shader
		range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
		parameters[0].InitAsDescriptorTable(1, &range[0], D3D12_SHADER_VISIBILITY_VERTEX);

		// CBV for pixel shader
		range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
		parameters[1].InitAsDescriptorTable(1, &range[1], D3D12_SHADER_VISIBILITY_PIXEL);

		// SRV for pixel shader (compute buffer)
		range[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		parameters[2].InitAsDescriptorTable(1, &range[2], D3D12_SHADER_VISIBILITY_PIXEL);

		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;

		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(_countof(parameters), parameters, 0, nullptr, rootSignatureFlags);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		DX::ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		DX::ThrowIfFailed(d3dDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// compute pipeline root signature
	{
		// 1. Define the root parameters
		CD3DX12_DESCRIPTOR_RANGE ranges[1];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
		CD3DX12_ROOT_PARAMETER rootParameters[2];

		rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsConstantBufferView(0);

		// 2. Create the root signature description
		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ComPtr<ID3DBlob> pSignature;
		ComPtr<ID3DBlob> pError;
		DX::ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, pSignature.GetAddressOf(), pError.GetAddressOf()));
		DX::ThrowIfFailed(d3dDevice->CreateRootSignature(0, pSignature->GetBufferPointer(), pSignature->GetBufferSize(), IID_PPV_ARGS(&m_computeRootSignature)));
		NAME_D3D12_OBJECT(m_computeRootSignature);
	}

	// Shader asynchron laden.
	auto createCSTask = DX::ReadDataAsync(L"ComputeShader.cso").then([this](std::vector<byte>& fileData) {
		m_computeShader = fileData;
		});

	auto createVSTask = DX::ReadDataAsync(L"SampleVertexShader.cso").then([this](std::vector<byte>& fileData) {
		m_vertexShader = fileData;
		});

	auto createPSTask = DX::ReadDataAsync(L"SamplePixelShader.cso").then([this](std::vector<byte>& fileData) {
		m_pixelShader = fileData;
		});

	// Pipelinezustand erstellen, sobald die Shader geladen wurden.
	auto createPipelineStateTask = (createPSTask && createVSTask && createCSTask).then([this]() {

		static const D3D12_INPUT_ELEMENT_DESC inputLayout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};



		D3D12_GRAPHICS_PIPELINE_STATE_DESC state = {};
		state.InputLayout = { inputLayout, _countof(inputLayout) };
		state.pRootSignature = m_rootSignature.Get();
		state.VS = CD3DX12_SHADER_BYTECODE(&m_vertexShader[0], m_vertexShader.size());
		state.PS = CD3DX12_SHADER_BYTECODE(&m_pixelShader[0], m_pixelShader.size());
		state.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		state.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		state.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		state.SampleMask = UINT_MAX;
		state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		state.NumRenderTargets = 2;
		state.RTVFormats[0] = m_deviceResources->GetBackBufferFormat();
		state.DSVFormat = m_deviceResources->GetDepthBufferFormat();
		state.SampleDesc.Count = 1;

		DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&m_pipelineState)));

		D3D12_COMPUTE_PIPELINE_STATE_DESC cState = {};
		cState.pRootSignature = m_computeRootSignature.Get();
		cState.CS = CD3DX12_SHADER_BYTECODE(&m_computeShader[0], m_computeShader.size());
		cState.NodeMask = 0;
		//cState.CachedPSO = m_cachedPSO;
		//cState.Flags = m_flags;

		DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateComputePipelineState(&cState, IID_PPV_ARGS(&m_computePipelineState)));


		// Shaderdaten können gelöscht werden, nachdem der Pipelinestatus erstellt wurde.
		m_vertexShader.clear();
		m_pixelShader.clear();
		m_computeShader.clear();
		});

	// Würfelgeometrieressourcen erstellen und in die GPU hochladen.
	auto createAssetsTask = createPipelineStateTask.then([this]() {
		auto d3dDevice = m_deviceResources->GetD3DDevice();

		// Erstellt eine Befehlsliste.
		DX::ThrowIfFailed(d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_deviceResources->GetCommandAllocator(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));
		NAME_D3D12_OBJECT(m_commandList);

		//Compute commandLine
		DX::ThrowIfFailed(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&m_computeCommandAllocator)));
		DX::ThrowIfFailed(d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, m_computeCommandAllocator.Get(), m_computePipelineState.Get(), IID_PPV_ARGS(&m_computeCommandList)));
		NAME_D3D12_OBJECT(m_computeCommandList);

		//new mkBuffer

		// Calculate buffer size
		Size outputSize = m_deviceResources->GetOutputSize();
		const UINT64 kBufferSize = outputSize.Width * outputSize.Height * 16 * sizeof(uint64_t);

		// Describe and create a committed resource for the buffer
		D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		D3D12_RESOURCE_DESC bufferDesc = {};
		bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Width = kBufferSize;
		bufferDesc.Height = 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels = 1;
		bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		
		d3dDevice->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			nullptr,
			IID_PPV_ARGS(&m_mkBuffer)
		);

		
		const UINT constantBufferSize = (sizeof(matrices_and_user_input) + 255) & ~255; // Round up to nearest 256 bytes

		D3D12_HEAP_PROPERTIES heapPropsUp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		D3D12_RESOURCE_DESC bufferDescUP = CD3DX12_RESOURCE_DESC::Buffer(constantBufferSize);

		
		

		/*D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {};
		uavHeapDesc.NumDescriptors = 2; // Adjust this number based on how many UAVs you need
		uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		uavHeapDesc.NodeMask = 0;*/

		// Erstellt einen Deskriptorheap für die Konstantenpuffer.
	

		//NAME_D3D12_OBJECT(m_cbvHeap);

		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

		d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_computeCommandQueue));

		DX::ThrowIfFailed(m_computeCommandList->Close());
		ID3D12CommandList* ppComputeCommandLists[] = { m_computeCommandList.Get() };
		m_computeCommandQueue->ExecuteCommandLists(_countof(ppComputeCommandLists), ppComputeCommandLists);
		//-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

		// Würfelscheitelpunkte. Jeder Scheitelpunkt hat eine Position und eine Farbe.
		VertexPositionColor cubeVertices[] =
		{
			{ XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT3(0.0f, 0.0f, 0.0f) },
			{ XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT3(0.0f, 0.0f, 1.0f) },
			{ XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT3(0.0f, 1.0f, 0.0f) },
			{ XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT3(0.0f, 1.0f, 1.0f) },
			{ XMFLOAT3(0.5f, -0.5f, -0.5f), XMFLOAT3(1.0f, 0.0f, 0.0f) },
			{ XMFLOAT3(0.5f, -0.5f,  0.5f), XMFLOAT3(1.0f, 0.0f, 1.0f) },
			{ XMFLOAT3(0.5f,  0.5f, -0.5f), XMFLOAT3(1.0f, 1.0f, 0.0f) },
			{ XMFLOAT3(0.5f,  0.5f,  0.5f), XMFLOAT3(1.0f, 1.0f, 1.0f) },
		};

		const UINT vertexBufferSize = sizeof(cubeVertices);

		// Vertexpufferressource im Standardheap der GPU erstellen und Indexdaten mit dem Uploadheap dort hinein kopieren.
		// Die Uploadressource darf erst freigegeben werden, wenn sie von der GPU nicht mehr verwendet wird.
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexBufferUpload;

		CD3DX12_HEAP_PROPERTIES defaultHeapProperties(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC vertexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
		DX::ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&defaultHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&vertexBufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)));

		CD3DX12_HEAP_PROPERTIES uploadHeapProperties(D3D12_HEAP_TYPE_UPLOAD);
		DX::ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&vertexBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&vertexBufferUpload)));

		NAME_D3D12_OBJECT(m_vertexBuffer);

		// Vertexpuffer in die GPU hochladen.
		{
			D3D12_SUBRESOURCE_DATA vertexData = {};
			vertexData.pData = reinterpret_cast<BYTE*>(cubeVertices);
			vertexData.RowPitch = vertexBufferSize;
			vertexData.SlicePitch = vertexData.RowPitch;

			UpdateSubresources(m_commandList.Get(), m_vertexBuffer.Get(), vertexBufferUpload.Get(), 0, 0, 1, &vertexData);

			CD3DX12_RESOURCE_BARRIER vertexBufferResourceBarrier =
				CD3DX12_RESOURCE_BARRIER::Transition(m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			m_commandList->ResourceBarrier(1, &vertexBufferResourceBarrier);
		}

		// Mesh-Indizes laden. Jede Indexdreiergruppe stellt ein Dreieck zum Rendern auf dem Bildschirm dar.
		// Beispiel: 0,2,1 bedeutet, dass die Scheitelpunkte mit den Indizes 0, 2 und 1 des Vertexpuffers
		// erstes Dreieck dieses Mesh.
		unsigned short cubeIndices[] =
		{
			0, 2, 1, // -x
			1, 2, 3,

			4, 5, 6, // +x
			5, 7, 6,

			0, 1, 5, // -y
			0, 5, 4,

			2, 6, 7, // +y
			2, 7, 3,

			0, 4, 6, // -z
			0, 6, 2,

			1, 3, 7, // +z
			1, 7, 5,
		};

		const UINT indexBufferSize = sizeof(cubeIndices);

		// Indexpufferressource im Standardheap der GPU erstellen und Indexdaten mit dem Uploadheap dort hinein kopieren.
		// Die Uploadressource darf erst freigegeben werden, wenn sie von der GPU nicht mehr verwendet wird.
		Microsoft::WRL::ComPtr<ID3D12Resource> indexBufferUpload;

		CD3DX12_RESOURCE_DESC indexBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
		DX::ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&defaultHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&indexBufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&m_indexBuffer)));

		DX::ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&indexBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&indexBufferUpload)));

		NAME_D3D12_OBJECT(m_indexBuffer);

		// Indexpuffer in die GPU hochladen.
		{
			D3D12_SUBRESOURCE_DATA indexData = {};
			indexData.pData = reinterpret_cast<BYTE*>(cubeIndices);
			indexData.RowPitch = indexBufferSize;
			indexData.SlicePitch = indexData.RowPitch;

			UpdateSubresources(m_commandList.Get(), m_indexBuffer.Get(), indexBufferUpload.Get(), 0, 0, 1, &indexData);

			CD3DX12_RESOURCE_BARRIER indexBufferResourceBarrier =
				CD3DX12_RESOURCE_BARRIER::Transition(m_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);
			m_commandList->ResourceBarrier(1, &indexBufferResourceBarrier);
		}

		

		CD3DX12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(DX::c_frameCount * c_alignedConstantBufferSize);
		DX::ThrowIfFailed(d3dDevice->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&constantBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_constantBuffer)));

		NAME_D3D12_OBJECT(m_constantBuffer);

		// Create the constant buffer
		UINT descriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		d3dDevice->CreateCommittedResource(
			&heapPropsUp,
			D3D12_HEAP_FLAG_NONE,
			&bufferDescUP,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_mUniformBuffer)
		);

		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = DX::c_frameCount + 2; // +2 for UAV and SRV
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		DX::ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_cbvHeap)));

		NAME_D3D12_OBJECT(m_cbvHeap);


		// Create CBVs
		D3D12_GPU_VIRTUAL_ADDRESS cbvGpuAddress = m_constantBuffer->GetGPUVirtualAddress();
		CD3DX12_CPU_DESCRIPTOR_HANDLE cbvHandle(m_cbvHeap->GetCPUDescriptorHandleForHeapStart());
		for (int i = 0; i < DX::c_frameCount; i++)
		{
			D3D12_CONSTANT_BUFFER_VIEW_DESC desc;
			desc.BufferLocation = cbvGpuAddress;
			desc.SizeInBytes = c_alignedConstantBufferSize;
			d3dDevice->CreateConstantBufferView(&desc, cbvHandle);
			cbvGpuAddress += desc.SizeInBytes;
			cbvHandle.Offset(descriptorSize);
		}

		uavHandle = m_cbvHeap->GetCPUDescriptorHandleForHeapStart();
		uavHandle.ptr += descriptorSize * DX::c_frameCount; // Offset by the number of CBVs

		// Create SRV handle
		srvHandle = m_cbvHeap->GetCPUDescriptorHandleForHeapStart();
		srvHandle.ptr += descriptorSize * (DX::c_frameCount + 1); // Offset by the number of CBVs + 1 for UAV

		// Create UAV
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = 0;
		uavDesc.Buffer.NumElements = outputSize.Width * outputSize.Height;
		uavDesc.Buffer.StructureByteStride = sizeof(float) * 4;
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		d3dDevice->CreateUnorderedAccessView(m_mkBuffer.Get(), nullptr, &uavDesc, uavHandle);


		// Create SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = outputSize.Width * outputSize.Height;
		srvDesc.Buffer.StructureByteStride = sizeof(float) * 4;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		d3dDevice->CreateShaderResourceView(m_mkBuffer.Get(), &srvDesc, srvHandle);
		cbvHandle.Offset(descriptorSize);

		

		// Erstellt Konstantenpufferansichten für den Zugriff auf den Uploadpuffer.
		
		//CD3DX12_CPU_DESCRIPTOR_HANDLE cbvCpuHandle(m_cbvHeap->GetCPUDescriptorHandleForHeapStart());
		//m_cbvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		// Ordnet die Konstantenpuffer zu.
		CD3DX12_RANGE readRange(0, 0);		// Wir beabsichtigen nicht, aus dieser Ressource für die CPU zu lesen.
		DX::ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_mappedConstantBuffer)));
		ZeroMemory(m_mappedConstantBuffer, DX::c_frameCount * c_alignedConstantBufferSize);
		// Wir heben die Zuordnung erst auf, wenn die App geschlossen wird. Es ist ohne Weiteres möglich, die Zuordnung bis zum Ende der Ressourcenlebensdauer beizubehalten.

		// Befehlsliste schließen und ausführen, um mit dem Kopieren des Vertex-/Indexpuffers in den Standardheap der GPU zu beginnen.
		DX::ThrowIfFailed(m_commandList->Close());
		ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
		m_deviceResources->GetCommandQueue()->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		// Vertex-/Indexpufferansichten erstellen.
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes = sizeof(VertexPositionColor);
		m_vertexBufferView.SizeInBytes = sizeof(cubeVertices);

		m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
		m_indexBufferView.SizeInBytes = sizeof(cubeIndices);
		m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;

		// Auf das Beenden der Ausführung der Befehlsliste warten. Die Vertex-/Indexpuffer müssen in die GPU hochgeladen werden, bevor die Uploadressourcen den Bereich überschreiten.
		m_deviceResources->WaitForGpu();
		});

	createAssetsTask.then([this]() {
		m_loadingComplete = true;
		});

	//Input defines:

	m_keyboard = std::make_unique<Keyboard>();
	m_keyboard->SetWindow(CoreWindow::GetForCurrentThread());

	m_mouse = std::make_unique<Mouse>();
	m_mouse->SetWindow(CoreWindow::GetForCurrentThread());
	m_mouse->SetMode(Mouse::MODE_RELATIVE);
	m_pitch = 0.5;
	m_yaw = 0;
	playerPosition = { 0.0f, 0.7f, 1.5f, 0.0f };
}

// Initialisiert Anzeigeparameter, wenn sich die Fenstergröße ändert.
void Sample3DSceneRenderer::CreateWindowSizeDependentResources()
{
	Size outputSize = m_deviceResources->GetOutputSize();
	float aspectRatio = outputSize.Width / outputSize.Height;
	float fovAngleY = 70.0f * XM_PI / 180.0f;

	D3D12_VIEWPORT viewport = m_deviceResources->GetScreenViewport();
	m_scissorRect = { 0, 0, static_cast<LONG>(viewport.Width), static_cast<LONG>(viewport.Height) };

	// Dies ist ein einfaches Beispiel für eine Änderung, die vorgenommen werden kann, wenn die App im
	// Hochformat oder angedockte Ansicht.
	if (aspectRatio < 1.0f)
	{
		fovAngleY *= 2.0f;
	}

	// Die OrientationTransform3D-Matrix wird hier im Nachhinein multipliziert
	// um die Szene in Bezug auf die Bildschirmausrichtung ordnungsgemäß auszurichten.
	// Dieser anschließende Multiplikationsschritt ist für alle Zeichnen-Befehle erforderlich, die
	// auf das Renderziel der Swapchain angewendet werden. Diese Transformation sollte nicht auf Zeichnen-Befehle
	// für andere Ziele angewendet werden.

	// Für dieses Beispiel wird ein rechtshändiges Koordinatensystem mit Zeilenmatrizen verwendet.
	XMMATRIX perspectiveMatrix = XMMatrixPerspectiveFovRH(
		fovAngleY,
		aspectRatio,
		0.01f,
		100.0f
	);

	XMFLOAT4X4 orientation = m_deviceResources->GetOrientationTransform3D();
	XMMATRIX orientationMatrix = XMLoadFloat4x4(&orientation);

	XMStoreFloat4x4(
		&m_constantBufferData.projection,
		XMMatrixTranspose(perspectiveMatrix * orientationMatrix)
	);

	// Das Auge befindet sich bei (0,0.7,1.5) und betrachtet Punkt (0,-0.1,0) mit dem Up-Vektor entlang der Y-Achse.
	static const XMVECTORF32 eye = { 0.0f, 0.7f, 1.5f, 0.0f };
	static const XMVECTORF32 at = { 0.0f, -0.1f, 0.5f, 0.0f };
	static const XMVECTORF32 up = { 0.0f, 1.0f, 0.0f, 0.0f };

	XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixLookAtRH(eye, at, up)));


}

// Wird einmal pro Frame aufgerufen, dreht den Würfel und berechnet das Modell und die Anzeigematrizen.
void Sample3DSceneRenderer::Update(DX::StepTimer const& timer)
{
	if (m_loadingComplete)
	{
		if (!m_tracking)
		{
			// Den Würfel nur gering drehen.

			//m_angle += static_cast<float>(timer.GetElapsedSeconds()) * m_radiansPerSecond;

			// Get Input




			auto mouse = m_mouse->GetState();

			if (mouse.rightButton) {
				m_mouse->SetMode(Mouse::MODE_RELATIVE);
			}
			else {
				m_mouse->SetMode(Mouse::MODE_ABSOLUTE);
			}

			if (mouse.positionMode == Mouse::MODE_RELATIVE)
			{
				XMVECTORF32 delta = { float(mouse.y), float(mouse.x), 0.f };

				m_pitch -= delta[0] * ROTATION_GAIN;

				m_yaw -= delta[1] * ROTATION_GAIN;
			}

			// limit pitch to straight up or straight down
			constexpr float limit = XM_PIDIV2 - 0.01f;
			m_pitch = max(-limit, m_pitch);
			m_pitch = min(+limit, m_pitch);

			// keep longitude in sane range by wrapping
			if (m_yaw > XM_PI)
			{
				m_yaw -= XM_2PI;
			}
			else if (m_yaw < -XM_PI)
			{
				m_yaw += XM_2PI;
			}

			float y = sinf(m_pitch);
			float r = cosf(m_pitch);
			float z = r * cosf(m_yaw);
			float x = r * sinf(m_yaw);

			XMVECTORF32 move = { 0,0,0 };
			auto kb = m_keyboard->GetState();

			if (kb.Up || kb.W)
			{
				move = { move[0], move[1] - MOVEMENT_GAIN, move[2] };
			}
			if (kb.Down || kb.S)
			{
				move = { move[0], move[1] + MOVEMENT_GAIN, move[2] };
			}
			if (kb.Left || kb.A)
			{
				move = { move[0] + MOVEMENT_GAIN, move[1], move[2] };
			}
			if (kb.Right || kb.D)
			{
				move = { move[0] - MOVEMENT_GAIN, move[1], move[2] };
			}
			if (kb.PageUp || kb.E)
			{
				move = { move[0], move[1], move[2] + MOVEMENT_GAIN };
			}
			if (kb.PageDown || kb.Q)
			{
				move = { move[0], move[1], move[2] - MOVEMENT_GAIN };
			}


			float bufferUp[] = { 0, 1.0, 0 };
			float bufferFront[] = { x, y, z };
			float bufferResult[] = { 1.0,0,0 };
			crossProduct(bufferFront, bufferUp, bufferResult);
			normalize(bufferResult);

			playerPosition = { playerPosition[0] - x * move[1], playerPosition[1] - y * move[1], playerPosition[2] - z * move[1], 0.0f };
			playerPosition = { playerPosition[0] - bufferResult[0] * move[0], playerPosition[1] - bufferResult[1] * move[0], playerPosition[2] - bufferResult[2] * move[0], 0.0f };
			playerPosition = { playerPosition[0], playerPosition[1] + move[2], playerPosition[2], 0.0f };

			lookAt = { playerPosition[0] + x, playerPosition[1] + y, playerPosition[2] + z, 0.0f };
			up = { 0.0f, 1.0f, 0.0f, 0.0f };




			XMStoreFloat4x4(&m_constantBufferData.view, XMMatrixTranspose(XMMatrixLookAtRH(playerPosition, lookAt, up)));



			Rotate(m_angle);
		}

		// Die Konstantenpufferressource aktualisieren.
		UINT8* destination = m_mappedConstantBuffer + (m_deviceResources->GetCurrentFrameIndex() * c_alignedConstantBufferSize);
		memcpy(destination, &m_constantBufferData, sizeof(m_constantBufferData));
	}
}

void  TubeVis2::Sample3DSceneRenderer::crossProduct(float v_A[], float v_B[], float c_P[]) {
	c_P[0] = v_A[0] * v_B[2] - v_A[2] * v_B[1];
	c_P[1] = -(v_A[0] * v_B[2] - v_A[2] * v_B[0]);
	c_P[2] = v_A[0] * v_B[1] - v_A[1] * v_B[0];
}

void  TubeVis2::Sample3DSceneRenderer::normalize(float v_A[]) {
	float length = sqrt(pow(v_A[0], 2) + pow(v_A[1], 2) + pow(v_A[2], 2));
	v_A[0] = v_A[0] / length;
	v_A[1] = v_A[1] / length;
	v_A[2] = v_A[2] / length;
}

// Speichert den aktuellen Zustand des Renderers.
void Sample3DSceneRenderer::SaveState()
{
	auto state = ApplicationData::Current->LocalSettings->Values;

	if (state->HasKey(AngleKey))
	{
		state->Remove(AngleKey);
	}
	if (state->HasKey(TrackingKey))
	{
		state->Remove(TrackingKey);
	}

	state->Insert(AngleKey, PropertyValue::CreateSingle(m_angle));
	state->Insert(TrackingKey, PropertyValue::CreateBoolean(m_tracking));
}

// Stellt den vorherigen Zustand des Renderers wieder her.
void Sample3DSceneRenderer::LoadState()
{
	auto state = ApplicationData::Current->LocalSettings->Values;
	if (state->HasKey(AngleKey))
	{
		m_angle = safe_cast<IPropertyValue^>(state->Lookup(AngleKey))->GetSingle();
		state->Remove(AngleKey);
	}
	if (state->HasKey(TrackingKey))
	{
		m_tracking = safe_cast<IPropertyValue^>(state->Lookup(TrackingKey))->GetBoolean();
		state->Remove(TrackingKey);
	}
}

// Das 3D-Würfelmodell um ein festgelegtes Bogenmaß drehen.
void Sample3DSceneRenderer::Rotate(float radians)
{
	// Auf das Übergeben der aktualisierten Modellmatrix an den Shader vorbereiten.
	XMStoreFloat4x4(&m_constantBufferData.model, XMMatrixTranspose(XMMatrixRotationY(radians)));
}

void Sample3DSceneRenderer::StartTracking()
{
	m_tracking = true;
}

// Bei der Nachverfolgung kann der 3D-Würfel um seine Y-Achse gedreht werden, indem die Zeigerposition relativ zur Breite des Ausgabebildschirms nachverfolgt wird.
void Sample3DSceneRenderer::TrackingUpdate(float positionX)
{
	if (m_tracking)
	{
		float radians = XM_2PI * 2.0f * positionX / m_deviceResources->GetOutputSize().Width;
		Rotate(radians);
	}
}

void Sample3DSceneRenderer::StopTracking()
{
	m_tracking = false;
}

// Rendert einen Frame mit dem Scheitelpunkt und den Pixel-Shadern.
bool Sample3DSceneRenderer::Render()
{
	// Der Ladevorgang verläuft asynchron. Geometrien erst nach dem Laden zeichnen.
	if (!m_loadingComplete)
	{
		return false;
	}

	Size outputSize = m_deviceResources->GetOutputSize();

	//set MatricesAndUserInput
	matrices_and_user_input uni;
	uni.mkBufferInfo = XMFLOAT4(outputSize.Width, outputSize.Height, 16, 0);
	//compute Shader part:

	//DX::ThrowIfFailed(m_computeCommandAllocator->Reset());
	
	DX::ThrowIfFailed(m_computeCommandList->Reset(m_computeCommandAllocator.Get(), m_computePipelineState.Get()));
	
	PIXBeginEvent(m_computeCommandList.Get(), 0, L"Draw the cube");
	
	UINT8* pCbvDataBegin;
	m_mUniformBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pCbvDataBegin));
	memcpy(pCbvDataBegin, &uni, sizeof(matrices_and_user_input));
	m_mUniformBuffer->Unmap(0, nullptr);

	m_computeCommandList->SetComputeRootSignature(m_computeRootSignature.Get());
	m_computeCommandList->SetPipelineState(m_computePipelineState.Get());

	ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get()};
	m_computeCommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

	CD3DX12_GPU_DESCRIPTOR_HANDLE additionalHandle(m_cbvHeap->GetGPUDescriptorHandleForHeapStart());
	m_computeCommandList->SetComputeRootDescriptorTable(0, additionalHandle);

	m_computeCommandList->SetComputeRootConstantBufferView(1, m_mUniformBuffer->GetGPUVirtualAddress());

	// Dispatch the compute shader
		
	UINT dispatchX = (outputSize.Width + 15) / 16;
	UINT dispatchY = (outputSize.Height + 15) / 16;
	m_computeCommandList->Dispatch(16, 16, 1);

	PIXEndEvent(m_computeCommandList.Get());

	DX::ThrowIfFailed(m_computeCommandList->Close());
	// Transition the m_mkBuffer from UAV to SRV
	ID3D12CommandList* ppComputeCommandLists[] = { m_computeCommandList.Get() };
	m_computeCommandQueue->ExecuteCommandLists(_countof(ppComputeCommandLists), ppComputeCommandLists);
	
	const UINT64 fenceValueForSignal = m_computeFenceValue;
	DX::ThrowIfFailed(m_computeCommandQueue->Signal(m_computeFence.Get(), fenceValueForSignal));
	m_computeFenceValue++;

	// Wait until the GPU has completed commands up to this fence point
	if (m_computeFence->GetCompletedValue() < fenceValueForSignal)
	{
		DX::ThrowIfFailed(m_computeFence->SetEventOnCompletion(fenceValueForSignal, m_computeFenceEvent));
		WaitForSingleObject(m_computeFenceEvent, INFINITE);
	}

	DX::ThrowIfFailed(m_computeCommandAllocator->Reset());
	// Die Befehlsliste kann jederzeit zurückgesetzt werden, nachdem "ExecuteCommandList()" aufgerufen wurde.
	DX::ThrowIfFailed(m_commandList->Reset(m_deviceResources->GetCommandAllocator(), m_pipelineState.Get()));



	PIXBeginEvent(m_commandList.Get(), 0, L"Draw the cube");
	{

		// Die von diesem Frame verwendete Grafikstammsignatur und die Deskriptorheaps festlegen.
		m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
		ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get() };
		m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

		D3D12_VIEWPORT viewport = m_deviceResources->GetScreenViewport();
		m_commandList->RSSetViewports(1, &viewport);
		m_commandList->RSSetScissorRects(1, &m_scissorRect);

		m_commandList->SetGraphicsRootDescriptorTable(0, m_cbvHeap->GetGPUDescriptorHandleForHeapStart());
		m_commandList->SetGraphicsRootDescriptorTable(1, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetGPUDescriptorHandleForHeapStart(), 1, m_cbvDescriptorSize));
		m_commandList->SetGraphicsRootDescriptorTable(2, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvHeap->GetGPUDescriptorHandleForHeapStart(), 2, m_cbvDescriptorSize));
		m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			m_mkBuffer.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		));
		// Angeben, dass diese Ressource als Renderziel verwendet wird.
		CD3DX12_RESOURCE_BARRIER renderTargetResourceBarrier =
			CD3DX12_RESOURCE_BARRIER::Transition(m_deviceResources->GetRenderTarget(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_commandList->ResourceBarrier(1, &renderTargetResourceBarrier);

		// Zeichenbefehle aufzeichnen.
		D3D12_CPU_DESCRIPTOR_HANDLE renderTargetView = m_deviceResources->GetRenderTargetView();
		D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView = m_deviceResources->GetDepthStencilView();
		m_commandList->ClearRenderTargetView(renderTargetView, DirectX::Colors::CornflowerBlue, 0, nullptr);
		m_commandList->ClearDepthStencilView(depthStencilView, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		m_commandList->OMSetRenderTargets(1, &renderTargetView, false, &depthStencilView);


		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		m_commandList->IASetIndexBuffer(&m_indexBufferView);
		
		//m_commandList->SetGraphicsRootDescriptorTable(2, m_cbvHeap->GetGPUDescriptorHandleForHeapStart());

		m_commandList->DrawIndexedInstanced(36, 1, 0, 0, 0);



		// Angeben, dass das Renderziel nicht zum Präsentieren verwendet wird, wenn die Ausführung der Befehlsliste beendet ist.
		CD3DX12_RESOURCE_BARRIER presentResourceBarrier =
			CD3DX12_RESOURCE_BARRIER::Transition(m_deviceResources->GetRenderTarget(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		m_commandList->ResourceBarrier(1, &presentResourceBarrier);
	}
	PIXEndEvent(m_commandList.Get());

	DX::ThrowIfFailed(m_commandList->Close());

	// Befehlsliste ausführen.	
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_deviceResources->GetCommandQueue()->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	return true;
}

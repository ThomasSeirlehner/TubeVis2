#pragma once
#pragma comment (lib, "dinput8.lib")

#include "..\Common\DeviceResources.h"
#include "ShaderStructures.h"
#include "..\Common\StepTimer.h"
#include <dinput.h>
#include <Keyboard.h>
#include <Mouse.h>

namespace TubeVis2
{
	// Dieser Beispielrenderer instanziiert eine grundlegende Rendering-Pipeline.
	class Sample3DSceneRenderer
	{
	public:
		Sample3DSceneRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
		~Sample3DSceneRenderer();
		void CreateDeviceDependentResources();
		void CreateWindowSizeDependentResources();
		void Update(DX::StepTimer const& timer);
		bool Render();
		void SaveState();

		void StartTracking();
		void TrackingUpdate(float positionX);
		void StopTracking();
		bool IsTracking() { return m_tracking; }

	private:
		void LoadState();
		void Rotate(float radians);
		void crossProduct(float v_A[], float v_B[], float c_P[]);
		void normalize(float v_A[]);

	private:
		// Konstantenpuffer müssen eine 256-Byte-Ausrichtung aufweisen.
		static const UINT c_alignedConstantBufferSize = (sizeof(ModelViewProjectionConstantBuffer) + 255) & ~255;

		// Zeiger in den Geräteressourcen zwischengespeichert.
		std::shared_ptr<DX::DeviceResources> m_deviceResources;

		// Direct3D-Ressourcen für Würfelgeometrie.
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>	m_commandList;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>	m_computeCommandList;
		Microsoft::WRL::ComPtr<ID3D12CommandQueue>			m_computeCommandQueue;
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator>		m_computeCommandAllocator;
		Microsoft::WRL::ComPtr<ID3D12Resource>				m_mkBuffer;
		Microsoft::WRL::ComPtr<ID3D12Resource>				m_mUniformBuffer;
		Microsoft::WRL::ComPtr<ID3D12RootSignature>			m_rootSignature;
		Microsoft::WRL::ComPtr<ID3D12RootSignature>			m_computeRootSignature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState>			m_pipelineState;
		Microsoft::WRL::ComPtr<ID3D12PipelineState>			m_computePipelineState;
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>		m_cbvHeap;
		//Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>		m_uavDescriptorHeap;
		Microsoft::WRL::ComPtr<ID3D12Resource>				m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D12Resource>				m_indexBuffer;
		Microsoft::WRL::ComPtr<ID3D12Resource>				m_constantBuffer;
		ModelViewProjectionConstantBuffer					m_constantBufferData;
		UINT8*												m_mappedConstantBuffer;
		UINT												m_cbvDescriptorSize;
		D3D12_RECT											m_scissorRect;
		std::vector<byte>									m_computeShader;
		std::vector<byte>									m_vertexShader;
		std::vector<byte>									m_pixelShader;
		D3D12_VERTEX_BUFFER_VIEW							m_vertexBufferView;
		D3D12_INDEX_BUFFER_VIEW								m_indexBufferView;
		D3D12_CACHED_PIPELINE_STATE							m_cachedPSO;
		D3D12_PIPELINE_STATE_FLAGS							m_flags;

		// Für die Renderschleife verwendete Variablen.
		bool	m_loadingComplete;
		float	m_radiansPerSecond;
		float	m_angle;
		bool	m_tracking;

		//Inputs:
		std::unique_ptr<DirectX::Keyboard> m_keyboard;
		std::unique_ptr<DirectX::Mouse> m_mouse;

		float m_pitch;
		float m_yaw;

		

		const float ROTATION_GAIN = 0.004f;
		const float MOVEMENT_GAIN = 0.07f;


	};
}


#include "pch.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Display;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml::Controls;
using namespace Platform;

namespace DisplayMetrics
{
	// Auf Anzeigegeräten mit hoher Auflösung sind für das Rendern ggf. ein schneller Grafikprozessor und viel Akkuleistung erforderlich.
	// Hochauflösende Telefone können z. B. eine schlechte Akkubetriebsdauer aufweisen, wenn
	// für Spiele das Rendern mit 60 BpS und in voller Qualität versucht wird.
	// Die Entscheidung für das Rendern in voller Qualität auf allen Plattformen und für alle Formfaktoren
	// sollte wohlüberlegt sein.
	static const bool SupportHighResolutions = true;

	// Die Standardschwellenwerte, die eine "hohe Auflösung" für die Anzeige definieren. Wenn die Schwellenwerte
	// überschritten werden und "SupportHighResolutions" den Wert "false" aufweist, werden die Dimensionen um
	// 50 % skaliert.
	static const float DpiThreshold = 192.0f;		// 200 % der Standarddesktopanzeige skaliert.
	static const float WidthThreshold = 1920.0f;	// 1080p Breite skaliert.
	static const float HeightThreshold = 1080.0f;	// 1080p Höhe skaliert.
};

// Für die Berechnung der Bildschirmdrehungen verwendete Konstanten.
namespace ScreenRotation
{
	// 0-Grad-Drehung um die Z-Achse
	static const XMFLOAT4X4 Rotation0(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
		);

	// 90-Grad-Drehung um die Z-Achse
	static const XMFLOAT4X4 Rotation90(
		0.0f, 1.0f, 0.0f, 0.0f,
		-1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
		);

	// 180-Grad-Drehung um die Z-Achse
	static const XMFLOAT4X4 Rotation180(
		-1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
		);

	// 270-Grad-Drehung um die Z-Achse
	static const XMFLOAT4X4 Rotation270(
		0.0f, -1.0f, 0.0f, 0.0f,
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
		);
};

// Konstruktor für DeviceResources.
DX::DeviceResources::DeviceResources(DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthBufferFormat) :
	m_currentFrame(0),
	m_screenViewport(),
	m_rtvDescriptorSize(0),
	m_fenceEvent(0),
	m_backBufferFormat(backBufferFormat),
	m_depthBufferFormat(depthBufferFormat),
	m_fenceValues{},
	m_d3dRenderTargetSize(),
	m_outputSize(),
	m_logicalSize(),
	m_nativeOrientation(DisplayOrientations::None),
	m_currentOrientation(DisplayOrientations::None),
	m_dpi(-1.0f),
	m_effectiveDpi(-1.0f),
	m_deviceRemoved(false)
{
	CreateDeviceIndependentResources();
	CreateDeviceResources();
}

// Konfiguriert Ressourcen, die nicht vom Direct3D-Gerät abhängig sind.
void DX::DeviceResources::CreateDeviceIndependentResources()
{
}

// Konfiguriert das Direct3D-Gerät und speichert die entsprechenden Handles und den Gerätekontext.
void DX::DeviceResources::CreateDeviceResources()
{
#if defined(_DEBUG)
	// Wenn es sich bei dem Projekt um eine Debugversion handelt, Debugging über SDK Layers aktivieren.
	{
		ComPtr<ID3D12Debug> debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
		}
	}
#endif

	DX::ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_dxgiFactory)));

	ComPtr<IDXGIAdapter1> adapter;
	GetHardwareAdapter(&adapter);

	// Direct3D 12-API-Geräteobjekt erstellen
	HRESULT hr = D3D12CreateDevice(
		adapter.Get(),					// Der Hardwareadapter.
		D3D_FEATURE_LEVEL_11_0,			// Von dieser App unterstützte Mindestfunktionsebene.
		IID_PPV_ARGS(&m_d3dDevice)		// Gibt das erstellte Direct3D-Gerät zurück.
		);

#if defined(_DEBUG)
	if (FAILED(hr))
	{
		// Wenn die Initialisierung fehlschlägt, auf das WARP-Gerät zurückgreifen.
		// Weitere Informationen zu WARP finden Sie unter: 
		// https://go.microsoft.com/fwlink/?LinkId=286690

		ComPtr<IDXGIAdapter> warpAdapter;
		DX::ThrowIfFailed(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		hr = D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_d3dDevice));
	}
#endif

	DX::ThrowIfFailed(hr);

	// Befehlswarteschlange erstellen.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	DX::ThrowIfFailed(m_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
	NAME_D3D12_OBJECT(m_commandQueue);

	// Erstellt Deskriptorheaps für Renderzielansichten und Tiefenschablonenansichten.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = c_frameCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	DX::ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
	NAME_D3D12_OBJECT(m_rtvHeap);

	m_rtvDescriptorSize = m_d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(m_d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));
	NAME_D3D12_OBJECT(m_dsvHeap);

	for (UINT n = 0; n < c_frameCount; n++)
	{
		DX::ThrowIfFailed(
			m_d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n]))
			);
	}

	// Synchronisierungsobjekte erstellen.
	DX::ThrowIfFailed(m_d3dDevice->CreateFence(m_fenceValues[m_currentFrame], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
	m_fenceValues[m_currentFrame]++;

	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (m_fenceEvent == nullptr)
	{
		DX::ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
	}
}

// Diese Ressourcen müssen bei jeder Änderung der Fenstergröße erneut erstellt werden.
void DX::DeviceResources::CreateWindowSizeDependentResources()
{
	// Warten auf Abschluss aller vorherigen GPU-Arbeiten.
	WaitForGpu();

	// Löscht den für die vorherige Fenstergröße spezifischen Inhalt und aktualisiert die nachverfolgten Umgrenzungswerte.
	for (UINT n = 0; n < c_frameCount; n++)
	{
		m_renderTargets[n] = nullptr;
		m_fenceValues[n] = m_fenceValues[m_currentFrame];
	}

	UpdateRenderTargetSize();

	// Die Breite und Höhe der Swapchain muss auf der Breite und Höhe des Fensters
	// mit der nativen Ausrichtung beruhen. Wenn das Fenster nicht im nativen
	// ausgerichtet ist, müssen die Abmessungen umgekehrt werden.
	DXGI_MODE_ROTATION displayRotation = ComputeDisplayRotation();

	bool swapDimensions = displayRotation == DXGI_MODE_ROTATION_ROTATE90 || displayRotation == DXGI_MODE_ROTATION_ROTATE270;
	m_d3dRenderTargetSize.Width = swapDimensions ? m_outputSize.Height : m_outputSize.Width;
	m_d3dRenderTargetSize.Height = swapDimensions ? m_outputSize.Width : m_outputSize.Height;

	UINT backBufferWidth = lround(m_d3dRenderTargetSize.Width);
	UINT backBufferHeight = lround(m_d3dRenderTargetSize.Height);

	if (m_swapChain != nullptr)
	{
		// Die Größe anpassen, wenn die Swapchain bereits vorhanden ist.
		HRESULT hr = m_swapChain->ResizeBuffers(c_frameCount, backBufferWidth, backBufferHeight, m_backBufferFormat, 0);

		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
		{
			// Wenn das Gerät aus einem beliebigen Grund entfernt wurde, müssen ein neues Gerät und eine Swapchain erstellt werden.
			m_deviceRemoved = true;

			// Ausführung dieser Methode nicht fortsetzen. DeviceResources wird gelöscht und neu erstellt.
			return;
		}
		else
		{
			DX::ThrowIfFailed(hr);
		}
	}
	else
	{
		// Andernfalls mit dem Adapter, der auch vom vorhandenen Direct3D-Gerät verwendet wird, eine neue erstellen.
		DXGI_SCALING scaling = DisplayMetrics::SupportHighResolutions ? DXGI_SCALING_NONE : DXGI_SCALING_STRETCH;
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};

		swapChainDesc.Width = backBufferWidth;						// Größe des Fensters anpassen.
		swapChainDesc.Height = backBufferHeight;
		swapChainDesc.Format = m_backBufferFormat;
		swapChainDesc.Stereo = false;
		swapChainDesc.SampleDesc.Count = 1;							// Kein Mehrfachsampling verwenden.
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = c_frameCount;					// Dreifache Pufferung verwenden, um die Wartezeit zu minimieren.
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;	// Alle universellen Windows-Apps müssen "_FLIP_ SwapEffects" verwenden.
		swapChainDesc.Flags = 0;
		swapChainDesc.Scaling = scaling;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

		ComPtr<IDXGISwapChain1> swapChain;
		DX::ThrowIfFailed(
			m_dxgiFactory->CreateSwapChainForCoreWindow(
				m_commandQueue.Get(),								// Swapchains benötigen einen Verweis auf die Befehlswarteschlange in DirectX 12.
				reinterpret_cast<IUnknown*>(m_window.Get()),
				&swapChainDesc,
				nullptr,
				&swapChain
				)
			);

		DX::ThrowIfFailed(swapChain.As(&m_swapChain));
	}

	// Die richtige Ausrichtung der Swapchain festlegen und generieren
	// 3D-Matrixtransformationen generieren, um die gedrehte Swapchain zu rendern.
	// Die 3D-Matrix wird explizit angegeben, um Rundungsfehler zu vermeiden.

	switch (displayRotation)
	{
	case DXGI_MODE_ROTATION_IDENTITY:
		m_orientationTransform3D = ScreenRotation::Rotation0;
		break;

	case DXGI_MODE_ROTATION_ROTATE90:
		m_orientationTransform3D = ScreenRotation::Rotation270;
		break;

	case DXGI_MODE_ROTATION_ROTATE180:
		m_orientationTransform3D = ScreenRotation::Rotation180;
		break;

	case DXGI_MODE_ROTATION_ROTATE270:
		m_orientationTransform3D = ScreenRotation::Rotation90;
		break;

	default:
		throw ref new FailureException();
	}

	DX::ThrowIfFailed(
		m_swapChain->SetRotation(displayRotation)
		);

	// Erstellt Renderzielansichten des Swapchain-Hintergrundpuffers.
	{
		m_currentFrame = m_swapChain->GetCurrentBackBufferIndex();
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvDescriptor(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
		for (UINT n = 0; n < c_frameCount; n++)
		{
			DX::ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_d3dDevice->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvDescriptor);
			rtvDescriptor.Offset(m_rtvDescriptorSize);

			WCHAR name[25];
			if (swprintf_s(name, L"m_renderTargets[%u]", n) > 0)
			{
				DX::SetName(m_renderTargets[n].Get(), name);
			}
		}
	}

	// Erstellt eine Tiefenschablone und -ansicht.
	{
		D3D12_HEAP_PROPERTIES depthHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

		D3D12_RESOURCE_DESC depthResourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(m_depthBufferFormat, backBufferWidth, backBufferHeight, 1, 1);
		depthResourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		CD3DX12_CLEAR_VALUE depthOptimizedClearValue(m_depthBufferFormat, 1.0f, 0);

		ThrowIfFailed(m_d3dDevice->CreateCommittedResource(
			&depthHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&depthResourceDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&m_depthStencil)
			));

		NAME_D3D12_OBJECT(m_depthStencil);

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = m_depthBufferFormat;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

		m_d3dDevice->CreateDepthStencilView(m_depthStencil.Get(), &dsvDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	// Einen 3D-Rendering-Viewport mit dem gesamten Fenster als Ziel erstellen.
	m_screenViewport = { 0.0f, 0.0f, m_d3dRenderTargetSize.Width, m_d3dRenderTargetSize.Height, 0.0f, 1.0f };
}

// Ermittelt die Dimensionen des Renderziels und bestimmt, ob eine zentrale Herunterskalierung erfolgt.
void DX::DeviceResources::UpdateRenderTargetSize()
{
	m_effectiveDpi = m_dpi;

	// Damit die Akkubetriebsdauer auf Geräten mit hoher Auflösung verbessert wird, rendern Sie für ein kleineres Renderziel,
	// und erlauben Sie dem Grafikprozessor die Skalierung der Ausgabe, wenn diese dargestellt wird.
	if (!DisplayMetrics::SupportHighResolutions && m_dpi > DisplayMetrics::DpiThreshold)
	{
		float width = DX::ConvertDipsToPixels(m_logicalSize.Width, m_dpi);
		float height = DX::ConvertDipsToPixels(m_logicalSize.Height, m_dpi);

		// Wenn das Gerät im Hochformat anzeigt, ist die Höhe größer als die Breite. Vergleichen Sie die
		// größere Dimension mit dem Breitenschwellenwert und die kleinere Dimension
		// mit dem Höhenschwellenwert.
		if (max(width, height) > DisplayMetrics::WidthThreshold && min(width, height) > DisplayMetrics::HeightThreshold)
		{
			// Zum Skalieren der App wird der effektive DPI-Wert geändert. Die logische Größe ändert sich nicht.
			m_effectiveDpi /= 2.0f;
		}
	}

	// Erforderliche Renderzielgröße in Pixel berechnen.
	m_outputSize.Width = DX::ConvertDipsToPixels(m_logicalSize.Width, m_effectiveDpi);
	m_outputSize.Height = DX::ConvertDipsToPixels(m_logicalSize.Height, m_effectiveDpi);

	// Verhindern, dass DirectX-Inhalte der Größe NULL erstellt werden.
	m_outputSize.Width = max(m_outputSize.Width, 1);
	m_outputSize.Height = max(m_outputSize.Height, 1);
}

// Diese Methode wird aufgerufen, wenn das CoreWindow-Objekt erstellt (oder neu erstellt) wird.
void DX::DeviceResources::SetWindow(CoreWindow^ window)
{
	DisplayInformation^ currentDisplayInformation = DisplayInformation::GetForCurrentView();

	m_window = window;
	m_logicalSize = Windows::Foundation::Size(window->Bounds.Width, window->Bounds.Height);
	m_nativeOrientation = currentDisplayInformation->NativeOrientation;
	m_currentOrientation = currentDisplayInformation->CurrentOrientation;
	m_dpi = currentDisplayInformation->LogicalDpi;

	CreateWindowSizeDependentResources();
}

// Diese Methode wird im Ereignishandler für das SizeChanged-Ereignis aufgerufen.
void DX::DeviceResources::SetLogicalSize(Windows::Foundation::Size logicalSize)
{
	if (m_logicalSize != logicalSize)
	{
		m_logicalSize = logicalSize;
		CreateWindowSizeDependentResources();
	}
}

// Diese Methode wird im Ereignishandler für das DpiChanged-Ereignis aufgerufen.
void DX::DeviceResources::SetDpi(float dpi)
{
	if (dpi != m_dpi)
	{
		m_dpi = dpi;

		// Wenn die angezeigten DPI geändert werden, wird auch die logische Größe des Fensters (gemessen in DIPs) geändert und muss aktualisiert werden.
		m_logicalSize = Windows::Foundation::Size(m_window->Bounds.Width, m_window->Bounds.Height);

		CreateWindowSizeDependentResources();
	}
}

// Diese Methode wird im Ereignishandler für das OrientationChanged-Ereignis aufgerufen.
void DX::DeviceResources::SetCurrentOrientation(DisplayOrientations currentOrientation)
{
	if (m_currentOrientation != currentOrientation)
	{
		m_currentOrientation = currentOrientation;
		CreateWindowSizeDependentResources();
	}
}

// Diese Methode wird im Ereignishandler für das DisplayContentsInvalidated-Ereignis aufgerufen.
void DX::DeviceResources::ValidateDevice()
{
	// Das D3D-Gerät ist nicht mehr gültig, wenn der Standardadapter geändert wird, nachdem das Gerät
	// erstellt wurde oder wenn das Gerät entfernt wurde.

	// Rufen Sie zuerst die LUID für den Standardadapter ab, die beim Erstellen des Geräts festgelegt wurde.

	DXGI_ADAPTER_DESC previousDesc;
	{
		ComPtr<IDXGIAdapter1> previousDefaultAdapter;
		DX::ThrowIfFailed(m_dxgiFactory->EnumAdapters1(0, &previousDefaultAdapter));

		DX::ThrowIfFailed(previousDefaultAdapter->GetDesc(&previousDesc));
	}

	// Anschließend die Informationen für den aktuellen Standardadapter abrufen.

	DXGI_ADAPTER_DESC currentDesc;
	{
		ComPtr<IDXGIFactory4> currentDxgiFactory;
		DX::ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&currentDxgiFactory)));

		ComPtr<IDXGIAdapter1> currentDefaultAdapter;
		DX::ThrowIfFailed(currentDxgiFactory->EnumAdapters1(0, &currentDefaultAdapter));

		DX::ThrowIfFailed(currentDefaultAdapter->GetDesc(&currentDesc));
	}

	// Wenn die Adapter-LUIDs nicht übereinstimmen oder das Gerät meldet, dass es entfernt wurde,
	// ein neues D3D-Gerät muss erstellt werden.

	if (previousDesc.AdapterLuid.LowPart != currentDesc.AdapterLuid.LowPart ||
		previousDesc.AdapterLuid.HighPart != currentDesc.AdapterLuid.HighPart ||
		FAILED(m_d3dDevice->GetDeviceRemovedReason()))
	{
		m_deviceRemoved = true;
	}
}

// Die Inhalte der Swapchain auf dem Bildschirm anzeigen.
void DX::DeviceResources::Present()
{
	// Das erste Argument weist DXGI an, bis zur VSync zu blockieren, sodass die Anwendung
	// bis zur nächsten VSync in den Standbymodus versetzt wird. Dadurch wird sichergestellt, dass beim Rendern von
	// Frames, die nie auf dem Display angezeigt werden, keine unnötigen Zyklen ausgeführt werden.
	HRESULT hr = m_swapChain->Present(1, 0);

	// Wenn das Gerät durch eine Verbindungstrennung oder ein Treiberupgrade entfernt wurde, müssen 
	// muss alle Geräteressourcen verwerfen.
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		m_deviceRemoved = true;
	}
	else
	{
		DX::ThrowIfFailed(hr);

		MoveToNextFrame();
	}
}

// Warten auf Abschluss ausstehender GPU-Arbeiten.
void DX::DeviceResources::WaitForGpu()
{
	// Signalbefehl in der Warteschlange planen.
	DX::ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_currentFrame]));

	// Warten, bis Umgrenzung überquert wurde.
	DX::ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_currentFrame], m_fenceEvent));
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

	// Den Umgrenzungswert für den aktuellen Frame inkrementieren.
	m_fenceValues[m_currentFrame]++;
}

// Rendern des nächsten Frames vorbereiten.
void DX::DeviceResources::MoveToNextFrame()
{
	// Signalbefehl in der Warteschlange planen.
	const UINT64 currentFenceValue = m_fenceValues[m_currentFrame];
	DX::ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), currentFenceValue));

	// Nächstes Objekt im Frameindex.
	m_currentFrame = m_swapChain->GetCurrentBackBufferIndex();

	// Prüfen, ob der nächste Frame gestartet werden kann.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_currentFrame])
	{
		DX::ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_currentFrame], m_fenceEvent));
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	// Den Umgrenzungswert für den nächsten Frame festlegen.
	m_fenceValues[m_currentFrame] = currentFenceValue + 1;
}

// Diese Methode bestimmt die Drehung zwischen der nativen Ausrichtung des Anzeigegeräts und der
// aktuellen Bildschirmausrichtung.
DXGI_MODE_ROTATION DX::DeviceResources::ComputeDisplayRotation()
{
	DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_UNSPECIFIED;

	// Hinweis: NativeOrientation kann nur Landscape oder Portrait sein, obwohl
	// die DisplayOrientations-Enumeration hat andere Werte.
	switch (m_nativeOrientation)
	{
	case DisplayOrientations::Landscape:
		switch (m_currentOrientation)
		{
		case DisplayOrientations::Landscape:
			rotation = DXGI_MODE_ROTATION_IDENTITY;
			break;

		case DisplayOrientations::Portrait:
			rotation = DXGI_MODE_ROTATION_ROTATE270;
			break;

		case DisplayOrientations::LandscapeFlipped:
			rotation = DXGI_MODE_ROTATION_ROTATE180;
			break;

		case DisplayOrientations::PortraitFlipped:
			rotation = DXGI_MODE_ROTATION_ROTATE90;
			break;
		}
		break;

	case DisplayOrientations::Portrait:
		switch (m_currentOrientation)
		{
		case DisplayOrientations::Landscape:
			rotation = DXGI_MODE_ROTATION_ROTATE90;
			break;

		case DisplayOrientations::Portrait:
			rotation = DXGI_MODE_ROTATION_IDENTITY;
			break;

		case DisplayOrientations::LandscapeFlipped:
			rotation = DXGI_MODE_ROTATION_ROTATE270;
			break;

		case DisplayOrientations::PortraitFlipped:
			rotation = DXGI_MODE_ROTATION_ROTATE180;
			break;
		}
		break;
	}
	return rotation;
}

// Diese Methode ruft den ersten verfügbaren Hardwareadapter an, der Direct3D 12 unterstützt.
// Wenn kein solcher Adapter gefunden wird, wird "*ppAdapter" auf "nullptr" festgelegt.
void DX::DeviceResources::GetHardwareAdapter(IDXGIAdapter1** ppAdapter)
{
	ComPtr<IDXGIAdapter1> adapter;
	*ppAdapter = nullptr;

	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != m_dxgiFactory->EnumAdapters1(adapterIndex, &adapter); adapterIndex++)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Den Basic Render Driver-Adapter nicht auswählen.
			continue;
		}

		// Überprüft, ob der Adapter 12 Direct3D unterstützt, erstellt aber das
		// tatsächliche Gerät noch nicht.
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}

	*ppAdapter = adapter.Detach();
}
